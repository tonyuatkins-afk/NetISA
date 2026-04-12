/*
 * render_dc.c - Discord client renderer
 *
 * Renders title bar, channel list, message area with word wrap,
 * compose bar, and status bar.
 */

#include "discord.h"
#include <string.h>

#define ATTR_TITLE      SCR_ATTR(SCR_BLACK, SCR_GREEN)
#define ATTR_CHAN_READ   SCR_ATTR(SCR_DARKGRAY, SCR_BLACK)
#define ATTR_CHAN_UNREAD SCR_ATTR(SCR_WHITE, SCR_BLACK)
#define ATTR_CHAN_SEL    SCR_ATTR(SCR_BLACK, SCR_CYAN)
#define ATTR_CHAN_HASH   SCR_ATTR(SCR_GREEN, SCR_BLACK)
#define ATTR_TIMESTAMP  SCR_ATTR(SCR_DARKGRAY, SCR_BLACK)
#define ATTR_MSG_TEXT   SCR_ATTR(SCR_LIGHTGRAY, SCR_BLACK)
#define ATTR_COMPOSE    SCR_ATTR(SCR_YELLOW, SCR_BLACK)
#define ATTR_PROMPT     SCR_ATTR(SCR_GREEN, SCR_BLACK)
#define ATTR_STATUS     SCR_ATTR(SCR_BLACK, SCR_GREEN)
#define ATTR_BORDER     SCR_ATTR(SCR_GREEN, SCR_BLACK)
#define ATTR_DIM        SCR_ATTR(SCR_DARKGRAY, SCR_BLACK)

void dc_render_titlebar(dc_state_t *s)
{
    scr_fill(0, DC_TITLE_ROW, 80, 1, ' ', ATTR_TITLE);
    scr_puts(2, DC_TITLE_ROW, "DISCORD", ATTR_TITLE);
    scr_putc(10, DC_TITLE_ROW, (char)0xB3, ATTR_TITLE);
    scr_putsn(12, DC_TITLE_ROW, s->server_name, 30, ATTR_TITLE);
    scr_puts(65, DC_TITLE_ROW, "Connected", ATTR_TITLE);
}

void dc_render_channels(dc_state_t *s)
{
    int i, y;
    int focused = (s->focus == DC_FOCUS_CHANNELS);

    scr_fill(0, DC_CONTENT_TOP, DC_CHAN_WIDTH, DC_CONTENT_ROWS, ' ',
             SCR_ATTR(SCR_BLACK, SCR_BLACK));

    /* Vertical border */
    for (y = DC_CONTENT_TOP; y <= DC_CONTENT_BOT; y++)
        scr_putc(DC_CHAN_WIDTH, y, (char)0xB3, ATTR_BORDER);

    scr_puts(1, DC_CONTENT_TOP, "CHANNELS", focused ? ATTR_CHAN_UNREAD : ATTR_DIM);

    for (i = 0; i < s->channel_count && i < DC_CONTENT_ROWS - 2; i++) {
        unsigned char attr;
        int row = DC_CONTENT_TOP + 2 + i;

        if (i == s->selected_channel) {
            attr = ATTR_CHAN_SEL;
            scr_fill(0, row, DC_CHAN_WIDTH, 1, ' ', attr);
            scr_putc(1, row, (char)0x10, attr);
        } else {
            attr = s->channels[i].unread ? ATTR_CHAN_UNREAD : ATTR_CHAN_READ;
            scr_fill(0, row, DC_CHAN_WIDTH, 1, ' ',
                     SCR_ATTR(SCR_BLACK, SCR_BLACK));
            scr_putc(1, row, ' ', attr);
        }

        if (i != s->selected_channel)
            scr_putc(2, row, '#', ATTR_CHAN_HASH);
        else
            scr_putc(2, row, '#', attr);

        scr_putsn(3, row, s->channels[i].name, DC_CHAN_WIDTH - 4, attr);
    }
}

/* Calculate display lines for one message with word wrap */
static int msg_lines(dc_message_t *m, int width)
{
    int prefix_len = 6 + (int)strlen(m->author) + 2;
    int text_len = (int)strlen(m->text);
    int first_w = width - prefix_len;
    int lines, remaining;

    if (first_w <= 0) first_w = 1;
    if (text_len == 0 || text_len <= first_w) return 1;

    lines = 1;
    remaining = text_len - first_w;
    while (remaining > 0) {
        int cw = width - prefix_len;
        if (cw <= 0) cw = 1;
        remaining -= cw;
        lines++;
    }
    return lines;
}

/* Render one message, return screen rows consumed */
static int render_msg(dc_message_t *m, int y, int max_rows)
{
    int x, prefix_len, text_pos, text_len, first_w, cont_w, line;

    if (max_rows <= 0) return 0;

    x = DC_MSG_LEFT + 1;
    scr_putsn(x, y, m->timestamp, 5, ATTR_TIMESTAMP);
    x += 6;

    scr_puts(x, y, m->author, m->author_color);
    x += (int)strlen(m->author);

    scr_puts(x, y, ": ", ATTR_MSG_TEXT);
    x += 2;

    prefix_len = x - (DC_MSG_LEFT + 1);
    first_w = DC_MSG_WIDTH - 1 - prefix_len;
    if (first_w <= 0) first_w = 1;

    text_len = (int)strlen(m->text);
    text_pos = 0;

    /* First line */
    {
        int len = text_len;
        if (len > first_w) len = first_w;
        scr_putsn(x, y, m->text, len, ATTR_MSG_TEXT);
        text_pos += len;
    }

    line = 1;
    cont_w = DC_MSG_WIDTH - 1 - prefix_len;
    if (cont_w <= 0) cont_w = 1;

    /* Continuation lines */
    while (text_pos < text_len && line < max_rows) {
        int len = text_len - text_pos;
        if (len > cont_w) len = cont_w;
        y++;
        line++;
        scr_fill(DC_MSG_LEFT + 1, y, DC_MSG_WIDTH - 1, 1, ' ',
                 SCR_ATTR(SCR_BLACK, SCR_BLACK));
        scr_putsn(DC_MSG_LEFT + 1 + prefix_len, y,
                  m->text + text_pos, len, ATTR_MSG_TEXT);
        text_pos += len;
    }
    return line;
}

void dc_render_messages(dc_state_t *s)
{
    int avail = DC_CONTENT_ROWS;
    int total_lines = 0;
    int i, y, start_msg, lines_before;

    scr_fill(DC_MSG_LEFT + 1, DC_CONTENT_TOP, DC_MSG_WIDTH - 1,
             DC_CONTENT_ROWS, ' ', SCR_ATTR(SCR_BLACK, SCR_BLACK));

    if (s->msg_count == 0) {
        scr_puts(DC_MSG_LEFT + 3, DC_CONTENT_TOP + 1,
                 "No messages yet.", ATTR_DIM);
        return;
    }

    /* Total display lines */
    for (i = 0; i < s->msg_count; i++)
        total_lines += msg_lines(&s->messages[i], DC_MSG_WIDTH - 1);

    /* Determine visible start line (bottom-aligned, scroll_pos lines up) */
    {
        int vis_start;
        if (total_lines <= avail) {
            vis_start = 0;
        } else {
            vis_start = total_lines - avail - s->msg_scroll;
            if (vis_start < 0) vis_start = 0;
        }

        /* Clamp scroll */
        if (s->msg_scroll > total_lines - avail)
            s->msg_scroll = total_lines - avail;
        if (s->msg_scroll < 0)
            s->msg_scroll = 0;

        lines_before = 0;
        start_msg = 0;
        for (i = 0; i < s->msg_count; i++) {
            int ml = msg_lines(&s->messages[i], DC_MSG_WIDTH - 1);
            if (lines_before + ml > vis_start) {
                start_msg = i;
                break;
            }
            lines_before += ml;
        }
    }

    /* Render visible messages */
    y = DC_CONTENT_TOP;
    for (i = start_msg; i < s->msg_count && y <= DC_CONTENT_BOT; i++) {
        int rows_left = DC_CONTENT_BOT - y + 1;
        scr_fill(DC_MSG_LEFT + 1, y, DC_MSG_WIDTH - 1,
                 rows_left < 3 ? rows_left : 3, ' ',
                 SCR_ATTR(SCR_BLACK, SCR_BLACK));
        y += render_msg(&s->messages[i], y, rows_left);
    }
}

void dc_render_compose(dc_state_t *s)
{
    int focused = (s->focus == DC_FOCUS_COMPOSE);

    scr_fill(0, DC_COMPOSE_ROW, 80, 1, ' ', SCR_ATTR(SCR_BLACK, SCR_BLACK));
    scr_puts(0, DC_COMPOSE_ROW, "> ", ATTR_PROMPT);

    if (s->compose.len > 0) {
        scr_putsn(2, DC_COMPOSE_ROW, s->compose.buf, 77, ATTR_COMPOSE);
    } else if (focused) {
        scr_puts(2, DC_COMPOSE_ROW, "Type a message...", ATTR_DIM);
    }

    if (focused) {
        int cpos = s->compose.cursor;
        if (cpos > 77) cpos = 77;
        scr_cursor_show();
        scr_cursor_pos(2 + cpos, DC_COMPOSE_ROW);
    } else {
        scr_cursor_hide();
    }
}

void dc_render_statusbar(dc_state_t *s)
{
    char buf[30];

    scr_fill(0, DC_STATUS_ROW, 80, 1, ' ', ATTR_STATUS);

    /* Channel name */
    scr_puts(1, DC_STATUS_ROW, " #", ATTR_STATUS);
    scr_putsn(3, DC_STATUS_ROW, s->channels[s->selected_channel].name,
              18, ATTR_STATUS);

    /* Message count */
    {
        int mc = s->msg_count;
        char tmp[6];
        int j = 0, t = 0;
        buf[j++] = 'M'; buf[j++] = 's'; buf[j++] = 'g'; buf[j++] = 's';
        buf[j++] = ':';
        if (mc == 0) { tmp[t++] = '0'; }
        else { while (mc > 0) { tmp[t++] = (char)('0' + mc % 10); mc /= 10; } }
        while (t > 0) buf[j++] = tmp[--t];
        buf[j] = '\0';
    }
    scr_puts(24, DC_STATUS_ROW, buf, ATTR_STATUS);

    /* Focus indicator */
    {
        const char *foc;
        switch (s->focus) {
        case DC_FOCUS_CHANNELS: foc = "[Channels]"; break;
        case DC_FOCUS_MESSAGES: foc = "[Messages]"; break;
        case DC_FOCUS_COMPOSE:  foc = "[Compose]"; break;
        default: foc = ""; break;
        }
        scr_puts(38, DC_STATUS_ROW, foc, ATTR_STATUS);
    }

    scr_puts(52, DC_STATUS_ROW, "Tab:Focus Ctrl+Q:Quit", ATTR_STATUS);
}
