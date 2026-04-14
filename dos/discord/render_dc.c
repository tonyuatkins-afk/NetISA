/*
 * render_dc.c - Discord v2 DOS client renderer
 *
 * Renders the entire 80x25 screen: title bar, channel list,
 * message area with word wrap, compose bar, status bar,
 * scrollbar, and overlay panels (help, user list).
 *
 * All rendering uses the screen.h API.
 * scr_putc(x, y, ch, attr) where x=column, y=row.
 *
 * Target: 8088 real mode, OpenWatcom C, small memory model.
 */

#include "discord.h"
#include <string.h>

/* External functions from discord.c (not in header) */
extern int dc_get_channel_msg_count(int ch_idx);
extern dc_message_t far *dc_get_channel_msg(int ch_idx, int msg_idx);

/* ================================================================
 * Stub user list for Alt+U overlay
 * ================================================================ */

#if FEAT_USERS
static const char *overlay_users[8] = {
    "VintageNerd", "RetroGamer", "ChipCollector",
    "DOSenthusiast", "BarelyBooting", "PCjrFan",
    "ISAbus_Dave", "SysOpSteve"
};
#endif

/* ================================================================
 * Integer-to-string helper (no sprintf on 8088)
 * ================================================================ */

static void int_to_str(int val, char *buf)
{
    char tmp[8];
    int i = 0, j = 0;

    if (val < 0) { buf[j++] = '-'; val = -val; }
    if (val == 0) { buf[j++] = '0'; buf[j] = '\0'; return; }
    while (val > 0) { tmp[i++] = (char)('0' + val % 10); val /= 10; }
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

/* ================================================================
 * Word-wrap helper
 * ================================================================ */

/*
 * msg_display_lines - Calculate how many screen rows a message needs.
 *
 * First line: timestamp(5) + space(1) + author(N) + ": "(2) + text
 * Continuation lines: indented to text start column.
 * Word-wrap at word boundaries when possible.
 */
static int msg_display_lines(dc_message_t *m)
{
    int author_len, prefix_len, text_len;
    int first_w, cont_w;
    int lines, pos;

    author_len = (int)strlen(m->author);
    prefix_len = 6 + author_len + 2;  /* "HH:MM " + author + ": " */
    text_len = (int)strlen(m->text);

    first_w = DC_MSG_WIDTH - prefix_len;
    if (first_w <= 0) first_w = 1;
    cont_w = DC_MSG_WIDTH - prefix_len;
    if (cont_w <= 0) cont_w = 1;

    if (text_len <= first_w) return 1;

    lines = 1;
    pos = first_w;
    while (pos < text_len) {
        pos += cont_w;
        lines++;
    }

#if FEAT_REACTIONS
    /* Reaction line adds one extra row if present */
    if (m->reaction_idx != 0xFF && m->reaction_count > 0)
        lines++;
#endif

    return lines;
}

/*
 * find_wrap_break - Find a word boundary for wrapping.
 * Scans backwards from pos+width to find a space.
 * Returns the number of chars to print on this line.
 */
static int find_wrap_break(const char *text, int pos, int width, int text_len)
{
    int end = pos + width;
    int i;

    if (end >= text_len) return text_len - pos;

    /* Scan backwards for a space within the last quarter of the line */
    for (i = end - 1; i > pos + (width / 2); i--) {
        if (text[i] == ' ') return i - pos + 1;
    }
    /* No good break point; hard wrap */
    return width;
}

/* ================================================================
 * Separator rendering
 * ================================================================ */

void dc_render_separator(int row)
{
    scr_hline(0, row, 80, (char)BOX_H, ATTR_SEP);
}

/* ================================================================
 * Title bar (row 0)
 * ================================================================ */

void dc_render_titlebar(dc_state_t *s)
{
    unsigned char attr;

    if (s->flash_ticks > 0)
        attr = ATTR_FLASH;
    else
        attr = ATTR_TITLE;

    scr_fill(0, DC_TITLE_ROW, 80, 1, ' ', attr);
    scr_puts(1, DC_TITLE_ROW, "DISCORD",
             s->flash_ticks > 0 ? ATTR_FLASH : ATTR_TITLE);
    scr_putc(9, DC_TITLE_ROW, (char)0xB3, attr);
    scr_putsn(11, DC_TITLE_ROW, s->server_name, DC_MAX_SERVER_NAME,
              s->flash_ticks > 0 ? ATTR_FLASH : ATTR_TITLE_SERVER);
    scr_puts(68, DC_TITLE_ROW, "Connected",
             s->flash_ticks > 0 ? ATTR_FLASH : ATTR_TITLE_STATUS);
}

/* ================================================================
 * Channel list (rows 2-21, cols 0-17)
 * ================================================================ */

void dc_render_channels(dc_state_t *s)
{
    int i, row;
    char line[DC_CHAN_WIDTH + 1];
    char countbuf[8];

    /* Clear channel pane */
    scr_fill(0, DC_CONTENT_TOP, DC_CHAN_WIDTH, DC_CONTENT_ROWS, ' ',
             SCR_ATTR(SCR_BLACK, SCR_BLACK));

    /* Vertical separator */
    scr_vline(DC_CHAN_WIDTH, DC_CONTENT_TOP, DC_CONTENT_ROWS,
              (char)BOX_V, ATTR_SEP);

    for (i = 0; i < s->channel_count && i < DC_CONTENT_ROWS; i++) {
        unsigned char attr;
        int name_len, count_len, max_name;

        row = DC_CONTENT_TOP + i;

        /* Pick attribute */
        if (i == s->selected_channel) {
            attr = ATTR_CHAN_SELECTED;
            scr_fill(0, row, DC_CHAN_WIDTH, 1, ' ', attr);
        } else if (s->channels[i].unread > 0) {
            attr = ATTR_CHAN_UNREAD;
        } else {
            attr = ATTR_CHAN_NORMAL;
        }

        /* Build "#name" */
        scr_putc(1, row, '#', attr);

        /* Unread count in parentheses */
        if (s->channels[i].unread > 0 && i != s->selected_channel) {
            int_to_str(s->channels[i].unread, countbuf);
            count_len = (int)strlen(countbuf) + 2;  /* "(N)" */
            max_name = DC_CHAN_WIDTH - 3 - count_len;
            if (max_name < 1) max_name = 1;

            scr_putsn(2, row, s->channels[i].name, max_name, attr);

            /* Place count at end of channel pane */
            {
                int cx = DC_CHAN_WIDTH - count_len - 1;
                if (cx < 2) cx = 2;
                scr_putc(cx, row, '(', attr);
                scr_puts(cx + 1, row, countbuf, attr);
                scr_putc(cx + 1 + (int)strlen(countbuf), row, ')', attr);
            }
        } else {
            scr_putsn(2, row, s->channels[i].name, DC_CHAN_WIDTH - 3, attr);
        }
    }

    /* Clear remaining rows */
    for (row = DC_CONTENT_TOP + s->channel_count;
         row <= DC_CONTENT_BOT; row++) {
        /* Already cleared by fill above */
    }
}

/* ================================================================
 * Message rendering (rows 2-21, cols 19-78)
 * ================================================================ */

/*
 * render_one_msg - Render a single message starting at screen row y.
 * Returns the number of screen rows consumed.
 */
static int render_one_msg(dc_message_t *m, int y, int max_rows
#if FEAT_SEARCH
    , dc_state_t *s, int msg_idx
#endif
    )
{
    int x, prefix_len, text_pos, text_len;
    int first_w, cont_w, line;
    int author_len;

    if (max_rows <= 0) return 0;

    /* Timestamp */
    x = DC_MSG_LEFT;
    scr_putsn(x, y, m->timestamp, 5, ATTR_MSG_TIME);
    x += 6;

    /* Author name in their color */
    author_len = (int)strlen(m->author);
    scr_putsn(x, y, m->author, author_len,
              m->is_self ? ATTR_MSG_SELF : m->author_color);
    x += author_len;

    /* Separator */
    scr_puts(x, y, ": ", ATTR_MSG_TEXT);
    x += 2;

    prefix_len = x - DC_MSG_LEFT;
    first_w = DC_MSG_WIDTH - prefix_len;
    if (first_w <= 0) first_w = 1;
    cont_w = DC_MSG_WIDTH - prefix_len;
    if (cont_w <= 0) cont_w = 1;

    text_len = (int)strlen(m->text);
    text_pos = 0;
    line = 0;

    /* First line of text */
    {
        int len = find_wrap_break(m->text, 0, first_w, text_len);
        scr_putsn(x, y, m->text, len, ATTR_MSG_TEXT);

#if FEAT_SEARCH
        /* Search highlight on first line */
        if (s->search.active && s->search.match_count > 0) {
            int mi;
            for (mi = 0; mi < s->search.match_count; mi++) {
                if (s->search.matches[mi].msg_idx == msg_idx) {
                    int off = s->search.matches[mi].offset;
                    if (off >= 0 && off < len) {
                        int hlen = s->search.query_len;
                        unsigned char ha;
                        int hi;
                        if (off + hlen > len) hlen = len - off;
                        ha = (mi == s->search.current_match)
                             ? ATTR_SEARCH_CUR : ATTR_SEARCH_HIT;
                        for (hi = 0; hi < hlen; hi++)
                            scr_putc(x + off + hi, y,
                                     m->text[off + hi], ha);
                    }
                }
            }
        }
#endif

        text_pos += len;
        line = 1;
    }

    /* Continuation lines */
    while (text_pos < text_len && line < max_rows) {
        int len = find_wrap_break(m->text, text_pos, cont_w, text_len);
        y++;
        line++;
        scr_putsn(DC_MSG_LEFT + prefix_len, y,
                  m->text + text_pos, len, ATTR_MSG_TEXT);

#if FEAT_SEARCH
        if (s->search.active && s->search.match_count > 0) {
            int mi;
            for (mi = 0; mi < s->search.match_count; mi++) {
                if (s->search.matches[mi].msg_idx == msg_idx) {
                    int off = s->search.matches[mi].offset;
                    if (off >= text_pos && off < text_pos + len) {
                        int loff = off - text_pos;
                        int hlen = s->search.query_len;
                        unsigned char ha;
                        int hi;
                        if (loff + hlen > len) hlen = len - loff;
                        ha = (mi == s->search.current_match)
                             ? ATTR_SEARCH_CUR : ATTR_SEARCH_HIT;
                        for (hi = 0; hi < hlen; hi++)
                            scr_putc(DC_MSG_LEFT + prefix_len + loff + hi,
                                     y, m->text[off + hi], ha);
                    }
                }
            }
        }
#endif

        text_pos += len;
    }

#if FEAT_REACTIONS
    /* Reaction line */
    if (m->reaction_idx != 0xFF && m->reaction_count > 0 && line < max_rows) {
        char rbuf[16];
        int ri = 0;
        y++;
        line++;
        rbuf[ri++] = ' ';
        rbuf[ri++] = (char)reaction_glyphs[m->reaction_idx & 0x07];
        rbuf[ri++] = ' ';
        int_to_str(m->reaction_count, rbuf + ri);
        ri += (int)strlen(rbuf + ri);
        rbuf[ri] = '\0';
        scr_puts(DC_MSG_LEFT + prefix_len, y, rbuf, ATTR_REACTION);
    }
#endif

#if FEAT_THREADS
    /* Thread indicator */
    if (m->thread_count > 0 && line < max_rows) {
        char tbuf[24];
        int ti = 0;
        y++;
        line++;
        tbuf[ti++] = ' ';
        tbuf[ti++] = (char)0x10;  /* right arrow */
        tbuf[ti++] = ' ';
        int_to_str(m->thread_count, tbuf + ti);
        ti += (int)strlen(tbuf + ti);
        tbuf[ti++] = ' ';
        tbuf[ti++] = 'r'; tbuf[ti++] = 'e'; tbuf[ti++] = 'p';
        tbuf[ti++] = 'l';
        if (m->thread_count != 1) tbuf[ti++] = 'i';
        if (m->thread_count != 1) tbuf[ti++] = 'e';
        if (m->thread_count != 1) tbuf[ti++] = 's';
        else tbuf[ti++] = 'y';
        tbuf[ti] = '\0';
        scr_puts(DC_MSG_LEFT + prefix_len, y, tbuf, ATTR_THREAD);
    }
#endif

    return line;
}

void dc_render_messages(dc_state_t *s)
{
    int avail = DC_CONTENT_ROWS;
    int total_lines = 0;
    int i, y, start_msg, vis_start, lines_before;

    /* Clear message area */
    scr_fill(DC_MSG_LEFT, DC_CONTENT_TOP, DC_MSG_WIDTH,
             DC_CONTENT_ROWS, ' ', SCR_ATTR(SCR_BLACK, SCR_BLACK));

    if (s->msg_count == 0) {
        scr_puts(DC_MSG_LEFT + 2, DC_CONTENT_TOP + 1,
                 "No messages yet.", ATTR_DIM);
        return;
    }

    /* Calculate total display lines */
    for (i = 0; i < s->msg_count; i++)
        total_lines += msg_display_lines(&s->messages[i]);

    /* Clamp scroll */
    if (total_lines > avail) {
        if (s->msg_scroll > total_lines - avail)
            s->msg_scroll = total_lines - avail;
    } else {
        s->msg_scroll = 0;
    }
    if (s->msg_scroll < 0)
        s->msg_scroll = 0;

    /* Determine visible start line (bottom-aligned) */
    if (total_lines <= avail) {
        vis_start = 0;
    } else {
        vis_start = total_lines - avail - s->msg_scroll;
    }

    /* Find first visible message */
    lines_before = 0;
    start_msg = 0;
    for (i = 0; i < s->msg_count; i++) {
        int ml = msg_display_lines(&s->messages[i]);
        if (lines_before + ml > vis_start) {
            start_msg = i;
            break;
        }
        lines_before += ml;
    }

    /* Render visible messages */
    y = DC_CONTENT_TOP;
    for (i = start_msg; i < s->msg_count && y <= DC_CONTENT_BOT; i++) {
        int rows_left = DC_CONTENT_BOT - y + 1;
        y += render_one_msg(&s->messages[i], y, rows_left
#if FEAT_SEARCH
            , s, i
#endif
            );
    }
}

/* ================================================================
 * Compose bar (row 23)
 * ================================================================ */

void dc_render_compose(dc_state_t *s)
{
    int focused = (s->focus == DC_FOCUS_COMPOSE);

#if FEAT_MULTILINE
    {
        /* Count newlines in compose buffer */
        int nl = 0;
        int ci;
        for (ci = 0; ci < s->compose.len; ci++) {
            if (s->compose.buf[ci] == '\n') nl++;
        }

        if (nl > 0) {
            /* Multi-line: overwrite separator row 22 */
            int line = 0;
            int start = 0;
            int max_lines = DC_COMPOSE_MAX_LINES;
            int base_row = DC_SEP2_ROW;

            if (max_lines > nl + 1) max_lines = nl + 1;
            if (max_lines > 2) max_lines = 2;  /* rows 22-23 only */

            base_row = DC_COMPOSE_ROW - max_lines + 1;

            for (line = 0; line < max_lines; line++) {
                int end = start;
                int row = base_row + line;

                scr_fill(0, row, 80, 1, ' ', SCR_ATTR(SCR_BLACK, SCR_BLACK));

                if (line == 0) {
                    scr_puts(0, row, "> ", ATTR_COMPOSE_PROMPT);
                } else {
                    scr_puts(0, row, "  ", ATTR_COMPOSE_PROMPT);
                }

                /* Find end of this line */
                while (end < s->compose.len && s->compose.buf[end] != '\n')
                    end++;

                if (end > start) {
                    int len = end - start;
                    if (len > DC_COMPOSE_COLS) len = DC_COMPOSE_COLS;
                    scr_putsn(2, row, s->compose.buf + start, len,
                              ATTR_COMPOSE);
                }

                start = end + 1;  /* skip newline */
            }

            if (focused) {
                /* Position cursor on last line */
                int cpos = s->compose.cursor;
                int crow = DC_COMPOSE_ROW;
                int cline_start = 0;
                int cl;

                /* Walk to cursor position to find line/col */
                for (cl = 0, cline_start = 0; cl < s->compose.len; cl++) {
                    if (cl == cpos) break;
                    if (s->compose.buf[cl] == '\n') {
                        cline_start = cl + 1;
                    }
                }
                {
                    int ccol = cpos - cline_start;
                    if (ccol > DC_COMPOSE_COLS) ccol = DC_COMPOSE_COLS;
                    scr_cursor_show();
                    scr_cursor_pos(2 + ccol, crow);
                }
            } else {
                scr_cursor_hide();
            }
            return;
        }
    }
#endif

    /* Single-line compose */
    scr_fill(0, DC_COMPOSE_ROW, 80, 1, ' ', SCR_ATTR(SCR_BLACK, SCR_BLACK));
    scr_puts(0, DC_COMPOSE_ROW, "> ", ATTR_COMPOSE_PROMPT);

    if (s->compose.len > 0) {
        scr_putsn(2, DC_COMPOSE_ROW, s->compose.buf,
                  DC_COMPOSE_COLS, ATTR_COMPOSE);
    } else if (focused) {
        scr_puts(2, DC_COMPOSE_ROW, "Type a message...", ATTR_DIM);
    }

    if (focused) {
        int cpos = s->compose.cursor;
        if (cpos > DC_COMPOSE_COLS) cpos = DC_COMPOSE_COLS;
        scr_cursor_show();
        scr_cursor_pos(2 + cpos, DC_COMPOSE_ROW);
    } else {
        scr_cursor_hide();
    }
}

/* ================================================================
 * Status bar (row 24)
 * ================================================================ */

void dc_render_statusbar(dc_state_t *s)
{
    char buf[40];
    char num[8];
    int pos;

#if FEAT_SEARCH
    if (s->search.active) {
        dc_render_search_bar(s);
        return;
    }
#endif

    scr_fill(0, DC_STATUS_ROW, 80, 1, ' ', ATTR_STATUS);

    /* Left side: #channel | Msgs: N | Unread: N */
    scr_puts(1, DC_STATUS_ROW, "#", ATTR_STATUS);
    scr_putsn(2, DC_STATUS_ROW,
              s->channels[s->selected_channel].name, 16, ATTR_STATUS);

    /* Pipe separator */
    pos = 20;
    scr_puts(pos, DC_STATUS_ROW, "| Msgs:", ATTR_STATUS);
    int_to_str(s->msg_count, num);
    scr_puts(pos + 7, DC_STATUS_ROW, num, ATTR_STATUS);

    /* Unread count (sum across all channels) */
    {
        int total_unread = 0;
        int ci;
        for (ci = 0; ci < s->channel_count; ci++)
            total_unread += s->channels[ci].unread;

        pos = 35;
        scr_puts(pos, DC_STATUS_ROW, "| Unread:", ATTR_STATUS);
        int_to_str(total_unread, num);
        scr_puts(pos + 9, DC_STATUS_ROW, num, ATTR_STATUS);
    }

    /* Right side: keybind hints */
    scr_puts(50, DC_STATUS_ROW, "Tab:Focus ^F:Find F1:Help ^Q:Quit",
             ATTR_STATUS);
}

/* ================================================================
 * Search bar (replaces status bar when search active)
 * ================================================================ */

#if FEAT_SEARCH
void dc_render_search_bar(dc_state_t *s)
{
    char matchbuf[16];

    scr_fill(0, DC_STATUS_ROW, 80, 1, ' ',
             SCR_ATTR(SCR_BLACK, SCR_CYAN));

    scr_puts(1, DC_STATUS_ROW, "Find: ",
             SCR_ATTR(SCR_WHITE, SCR_CYAN));

    if (s->search.query_len > 0) {
        scr_putsn(7, DC_STATUS_ROW, s->search.query, 30,
                  SCR_ATTR(SCR_WHITE, SCR_CYAN));
    }

    /* Match count */
    if (s->search.match_count > 0) {
        int_to_str(s->search.current_match + 1, matchbuf);
        {
            int mlen = (int)strlen(matchbuf);
            matchbuf[mlen++] = '/';
            int_to_str(s->search.match_count, matchbuf + mlen);
        }
        scr_puts(50, DC_STATUS_ROW, matchbuf,
                 SCR_ATTR(SCR_YELLOW, SCR_CYAN));
    } else if (s->search.query_len > 0) {
        scr_puts(50, DC_STATUS_ROW, "No matches",
                 SCR_ATTR(SCR_LIGHTRED, SCR_CYAN));
    }

    scr_puts(65, DC_STATUS_ROW, "Esc:Close",
             SCR_ATTR(SCR_WHITE, SCR_CYAN));

    /* Position cursor in search field */
    scr_cursor_show();
    scr_cursor_pos(7 + s->search.query_len, DC_STATUS_ROW);
}
#endif

/* ================================================================
 * Scrollbar (col 79, rows 2-21)
 * ================================================================ */

void dc_render_scrollbar(dc_state_t *s)
{
    int row;
    int total_lines = 0;
    int max_scroll, thumb_row;
    int i;

    /* Draw track */
    for (row = DC_CONTENT_TOP; row <= DC_CONTENT_BOT; row++)
        scr_putc(79, row, (char)BOX_SHADE1, ATTR_SCROLLBAR);

    if (s->msg_count == 0) return;

    /* Calculate total display lines */
    for (i = 0; i < s->msg_count; i++)
        total_lines += msg_display_lines(&s->messages[i]);

    max_scroll = total_lines - DC_CONTENT_ROWS;
    if (max_scroll <= 0) {
        /* All messages fit: thumb at bottom */
        scr_putc(79, DC_CONTENT_BOT, (char)BOX_BLOCK, ATTR_SCROLLTHUMB);
        return;
    }

    /* Proportional thumb position */
    /* scroll_pos is lines from bottom; invert for top-down thumb */
    {
        int scroll_pos = max_scroll - s->msg_scroll;
        if (scroll_pos < 0) scroll_pos = 0;
        if (scroll_pos > max_scroll) scroll_pos = max_scroll;

        thumb_row = DC_CONTENT_TOP +
            (scroll_pos * (DC_CONTENT_ROWS - 1)) / max_scroll;
    }

    scr_putc(79, thumb_row, (char)BOX_BLOCK, ATTR_SCROLLTHUMB);
}

/* ================================================================
 * Help overlay (centered 50x18 box)
 * ================================================================ */

void dc_render_help(dc_state_t *s)
{
    int bx, by, bw, bh;
    int row;

    if (!s->show_help) return;

    bw = 50;
    bh = 18;
    bx = (80 - bw) / 2;   /* 15 */
    by = (25 - bh) / 2;   /* 3 */

    /* Shadow */
    scr_fill(bx + 1, by + 1, bw, bh, ' ', SCR_ATTR(SCR_DARKGRAY, SCR_BLACK));

    /* Box background */
    scr_fill(bx, by, bw, bh, ' ', ATTR_OVERLAY_BG);

    /* Border using box chars */
    scr_box(bx, by, bw, bh, ATTR_OVERLAY_BG);

    /* Title */
    scr_puts(bx + (bw - 14) / 2, by, " KEYBINDINGS ", ATTR_OVERLAY_TITLE);

    row = by + 2;

    /* Navigation */
    scr_puts(bx + 2, row,   "NAVIGATION", ATTR_OVERLAY_TITLE);
    row++;
    scr_puts(bx + 3, row,   "Tab", ATTR_OVERLAY_KEY);
    scr_puts(bx + 10, row,  "Cycle focus panes", ATTR_OVERLAY_BG);
    row++;
    scr_puts(bx + 3, row,   "Up/Down", ATTR_OVERLAY_KEY);
    scr_puts(bx + 14, row,  "Navigate list/scroll", ATTR_OVERLAY_BG);
    row++;
    scr_puts(bx + 3, row,   "PgUp/PgDn", ATTR_OVERLAY_KEY);
    scr_puts(bx + 14, row,  "Scroll messages", ATTR_OVERLAY_BG);
    row++;
    scr_puts(bx + 3, row,   "Alt+1..8", ATTR_OVERLAY_KEY);
    scr_puts(bx + 14, row,  "Quick switch channel", ATTR_OVERLAY_BG);
    row += 2;

    /* Compose */
    scr_puts(bx + 2, row,   "COMPOSE", ATTR_OVERLAY_TITLE);
    row++;
    scr_puts(bx + 3, row,   "Enter", ATTR_OVERLAY_KEY);
    scr_puts(bx + 14, row,  "Send message", ATTR_OVERLAY_BG);
    row++;
#if FEAT_MULTILINE
    scr_puts(bx + 3, row,   "Shift+Enter", ATTR_OVERLAY_KEY);
    scr_puts(bx + 16, row,  "New line", ATTR_OVERLAY_BG);
    row++;
#endif

    /* Commands */
    row++;
    scr_puts(bx + 2, row,   "COMMANDS", ATTR_OVERLAY_TITLE);
    row++;
    scr_puts(bx + 3, row,   "Ctrl+F", ATTR_OVERLAY_KEY);
    scr_puts(bx + 14, row,  "Search messages", ATTR_OVERLAY_BG);
    row++;
    scr_puts(bx + 3, row,   "Alt+U", ATTR_OVERLAY_KEY);
    scr_puts(bx + 14, row,  "Toggle user list", ATTR_OVERLAY_BG);
    row++;
    scr_puts(bx + 3, row,   "F9", ATTR_OVERLAY_KEY);
    scr_puts(bx + 14, row,  "Toggle sound", ATTR_OVERLAY_BG);
    row++;
    scr_puts(bx + 3, row,   "Ctrl+Q", ATTR_OVERLAY_KEY);
    scr_puts(bx + 14, row,  "Quit", ATTR_OVERLAY_BG);
}

/* ================================================================
 * User list overlay (right-aligned 20x14 box)
 * ================================================================ */

void dc_render_userlist(dc_state_t *s)
{
#if FEAT_USERS
    int bx, by, bw, bh;
    int i, row;

    if (!s->show_userlist) return;

    bw = 20;
    bh = 14;
    bx = 80 - bw - 1;  /* right-aligned with 1 col margin */
    by = DC_CONTENT_TOP;

    /* Shadow */
    scr_fill(bx + 1, by + 1, bw, bh, ' ', SCR_ATTR(SCR_DARKGRAY, SCR_BLACK));

    /* Box background */
    scr_fill(bx, by, bw, bh, ' ', ATTR_OVERLAY_BG);

    /* Border */
    scr_box(bx, by, bw, bh, ATTR_OVERLAY_BG);

    /* Title */
    scr_puts(bx + (bw - 7) / 2, by, " USERS ", ATTR_OVERLAY_TITLE);

    /* Online indicator */
    {
        char hdr[16];
        hdr[0] = 'O'; hdr[1] = 'n'; hdr[2] = 'l'; hdr[3] = 'i';
        hdr[4] = 'n'; hdr[5] = 'e'; hdr[6] = ' '; hdr[7] = '-';
        hdr[8] = ' '; hdr[9] = '8'; hdr[10] = '\0';
        scr_puts(bx + 2, by + 2, hdr,
                 SCR_ATTR(SCR_LIGHTGREEN, SCR_BLUE));
    }

    /* User list */
    for (i = 0; i < 8; i++) {
        unsigned char uattr;

        row = by + 4 + i;
        if (row >= by + bh - 1) break;

        /* Color each user with their author color */
        uattr = dc_author_color(overlay_users[i]);
        /* Make it on blue background for the overlay */
        uattr = SCR_ATTR(uattr & 0x0F, SCR_BLUE);

        scr_putc(bx + 2, row, (char)0xFE, SCR_ATTR(SCR_LIGHTGREEN, SCR_BLUE));
        scr_putc(bx + 3, row, ' ', ATTR_OVERLAY_BG);
        scr_putsn(bx + 4, row, overlay_users[i], bw - 6, uattr);
    }
#else
    (void)s;
#endif
}

/* ================================================================
 * Full screen render
 * ================================================================ */

void dc_render_all(dc_state_t *s)
{
    /* Decrement flash counter */
    if (s->flash_ticks > 0)
        s->flash_ticks--;

    /* Render all screen sections */
    dc_render_titlebar(s);
    dc_render_separator(DC_SEP1_ROW);
    dc_render_channels(s);
    dc_render_messages(s);
    dc_render_scrollbar(s);
    dc_render_separator(DC_SEP2_ROW);
    dc_render_compose(s);
    dc_render_statusbar(s);

    /* Overlays (drawn on top) */
    if (s->show_userlist)
        dc_render_userlist(s);
    if (s->show_help)
        dc_render_help(s);

    s->dirty = 0;
}
