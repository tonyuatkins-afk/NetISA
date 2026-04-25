/*
 * chat.c - Chat history rendering
 *
 * Renders the scrollable message history area (rows 2-19),
 * title bar, status bar, and thinking animation.
 * Messages are accessed via far pointers from the heap pool.
 */

#include "claude.h"
#include <string.h>
#include <stdio.h>
#include <dos.h>

/* Attribute definitions */
#define ATTR_TITLE      SCR_ATTR(SCR_BLACK, SCR_GREEN)
#define ATTR_USER_PFX   SCR_ATTR(SCR_LIGHTGREEN, SCR_BLACK)
#define ATTR_USER_TEXT  SCR_ATTR(SCR_WHITE, SCR_BLACK)
#define ATTR_CL_PFX     SCR_ATTR(SCR_LIGHTCYAN, SCR_BLACK)
#define ATTR_CL_TEXT    SCR_ATTR(SCR_LIGHTGRAY, SCR_BLACK)
#define ATTR_SYS_CMD    SCR_ATTR(SCR_YELLOW, SCR_BLACK)
#define ATTR_SYS_OUT    SCR_ATTR(SCR_LIGHTGREEN, SCR_BLACK)
#define ATTR_THINKING   SCR_ATTR(SCR_YELLOW, SCR_BLACK)
#define ATTR_PENDING    SCR_ATTR(SCR_YELLOW, SCR_BLACK)
#define ATTR_SKIP       SCR_ATTR(SCR_DARKGRAY, SCR_BLACK)
#define ATTR_SEP        SCR_ATTR(SCR_GREEN, SCR_BLACK)
#define ATTR_STAT       SCR_ATTR(SCR_BLACK, SCR_GREEN)

/* Near buffer for copying far message text before rendering */
static char render_buf[MAX_MSG_LEN];

/* Get BIOS tick counter (18.2 Hz) */
static unsigned long get_ticks(void)
{
    unsigned long t = 0;
    _asm {
        xor ax, ax
        int 1Ah
        mov word ptr t, dx
        mov word ptr t+2, cx
    }
    return t;
}

void cl_render_titlebar(cl_state_t *s)
{
    const char *mode_str;
    unsigned char mode_attr;

    scr_fill(0, CL_TITLE_ROW, 80, 1, ' ', ATTR_TITLE);
    scr_puts(2, CL_TITLE_ROW, "Claude for DOS", ATTR_TITLE);

    /* Exec mode indicator (centered) */
    switch (s->exec_mode) {
    case EXEC_OFF:
        mode_str = "[Chat]";
        mode_attr = SCR_ATTR(SCR_DARKGRAY, SCR_GREEN);
        break;
    case EXEC_ASK:
        mode_str = "[Ask]";
        mode_attr = SCR_ATTR(SCR_YELLOW, SCR_GREEN);
        break;
    case EXEC_AUTO:
        mode_str = "[Auto]";
        mode_attr = SCR_ATTR(SCR_LIGHTRED, SCR_GREEN);
        break;
    default:
        mode_str = "";
        mode_attr = ATTR_TITLE;
        break;
    }
    scr_puts(37, CL_TITLE_ROW, mode_str, mode_attr);

    scr_puts(66, CL_TITLE_ROW, "NetISA v1.0", ATTR_TITLE);

    /* Separator lines */
    scr_hline(0, CL_SEP1_ROW, 80, (char)0xC4, ATTR_SEP);
    scr_hline(0, CL_SEP2_ROW, 80, (char)0xC4, ATTR_SEP);
}

/* Find the length of the current line segment (up to next \n or end).
 * Does not include the \n itself. */
static int line_seg_len(const char *text, int pos, int text_len)
{
    int i = pos;
    while (i < text_len && text[i] != '\n') i++;
    return i - pos;
}

/* Render text with word wrap and newline support, return rows used.
 * Newlines force a line break; text between newlines wraps normally. */
static int render_wrapped(int x, int y, const char *text, int text_len,
                          int first_w, int cont_indent, int cont_w,
                          int max_rows, unsigned char attr)
{
    int pos = 0;
    int rows = 0;
    int col_w;       /* available width for current screen row */
    int first_seg;   /* is this the very first segment (gets prefix width)? */

    if (max_rows <= 0) return 0;

    first_seg = 1;

    while (pos <= text_len && rows < max_rows) {
        int seg_len = line_seg_len(text, pos, text_len);
        int seg_pos = 0;

        /* Render this segment (text between newlines) with wrapping */
        while (seg_pos <= seg_len && rows < max_rows) {
            int draw_x;
            int chunk;

            if (first_seg && seg_pos == 0) {
                col_w = first_w;
                draw_x = x;
                first_seg = 0;
            } else if (seg_pos == 0 && rows > 0) {
                /* New line from \n: use continuation indent */
                col_w = cont_w;
                draw_x = cont_indent;
            } else if (seg_pos > 0) {
                /* Wrap continuation within a segment */
                col_w = cont_w;
                draw_x = cont_indent;
            } else {
                col_w = cont_w;
                draw_x = cont_indent;
            }

            chunk = seg_len - seg_pos;
            if (chunk > col_w) chunk = col_w;
            if (chunk <= 0 && seg_pos == 0) {
                /* Empty line from consecutive \n */
                rows++;
                break;
            }
            if (chunk <= 0) break;

            scr_putsn(draw_x, y + rows, text + pos + seg_pos, chunk, attr);
            seg_pos += chunk;
            rows++;

            if (seg_pos >= seg_len) break;
        }

        /* If segment was empty (blank line), we already incremented rows */
        if (seg_len == 0 && rows == 0) rows = 1;

        /* Advance past the segment and the \n */
        pos += seg_len;
        if (pos < text_len && text[pos] == '\n') pos++;
        if (pos >= text_len) break;
    }

    if (rows == 0) rows = 1;
    return rows;
}

/* Render system message ("> cmd\noutput"), return rows used */
static int render_system_msg(const char *text, int y, int max_rows)
{
    int rows = 0;
    int i = 0;
    int line_start = 0;
    int first_line = 1;

    while (rows < max_rows && text[i] != '\0') {
        /* Find end of current line */
        while (text[i] != '\0' && text[i] != '\n') i++;

        {
            int line_len = i - line_start;
            unsigned char attr = first_line ? ATTR_SYS_CMD : ATTR_SYS_OUT;
            if (line_len > 79) line_len = 79;
            scr_putsn(0, y + rows, text + line_start, line_len, attr);
            rows++;
            first_line = 0;
        }

        if (text[i] == '\0') break;
        i++;  /* skip newline */
        line_start = i;
    }

    /* Handle case where text ends but we never entered the loop body */
    if (rows == 0) rows = 1;
    return rows;
}

void cl_render_chat(cl_state_t *s)
{
    int avail = CL_CHAT_ROWS;
    int total_lines = 0;
    int vis_start;
    int i, y, lines_before, start_msg;

    /* Clear chat area */
    scr_fill(0, CL_CHAT_TOP, 80, CL_CHAT_ROWS, ' ',
             SCR_ATTR(SCR_BLACK, SCR_BLACK));

    if (s->message_count == 0 || !s->messages) {
        scr_puts(2, CL_CHAT_TOP + 1, "No messages yet.", ATTR_SKIP);
        return;
    }

    /* Total display lines (including blank lines between messages) */
    for (i = 0; i < s->message_count; i++)
        total_lines += s->messages[i].display_lines + 1;

    /* Clamp scroll */
    if (total_lines > avail) {
        if (s->scroll_pos > total_lines - avail)
            s->scroll_pos = total_lines - avail;
    } else {
        s->scroll_pos = 0;
    }
    if (s->scroll_pos < 0)
        s->scroll_pos = 0;

    /* Bottom-aligned: find starting visible line */
    if (total_lines <= avail) {
        vis_start = 0;
    } else {
        vis_start = total_lines - avail - s->scroll_pos;
    }

    /* Find first visible message */
    lines_before = 0;
    start_msg = 0;
    for (i = 0; i < s->message_count; i++) {
        int ml = s->messages[i].display_lines + 1;
        if (lines_before + ml > vis_start) {
            start_msg = i;
            break;
        }
        lines_before += ml;
    }

    /* Render visible messages */
    y = CL_CHAT_TOP;
    for (i = start_msg; i < s->message_count && y <= CL_CHAT_BOT; i++) {
        cl_message_t far *m = &s->messages[i];
        int rows_left = CL_CHAT_BOT - y + 1;
        int rows_used = 0;
        int text_len;
        char role;

        /* Copy far text to near buffer for rendering */
        _fmemcpy((void far *)render_buf, (const void far *)m->text,
                 MAX_MSG_LEN);
        render_buf[MAX_MSG_LEN - 1] = '\0';
        text_len = (int)strlen(render_buf);
        role = m->role;

        switch (role) {
        case ROLE_USER:
            scr_puts(0, y, "You: ", ATTR_USER_PFX);
            rows_used = render_wrapped(5, y, render_buf, text_len,
                                       75, 2, 78, rows_left, ATTR_USER_TEXT);
            break;

        case ROLE_CLAUDE:
            scr_puts(0, y, "Claude: ", ATTR_CL_PFX);
            rows_used = render_wrapped(8, y, render_buf, text_len,
                                       72, 2, 78, rows_left, ATTR_CL_TEXT);
            break;

        case ROLE_SYSTEM:
            rows_used = render_system_msg(render_buf, y, rows_left);
            break;
        }

        y += rows_used;

        /* Blank separator line */
        if (y <= CL_CHAT_BOT) y++;
    }

    /* Thinking animation */
    if (s->waiting && y <= CL_CHAT_BOT) {
        unsigned long ticks = get_ticks();
        int dots = (int)((ticks / 6) % 4);
        char buf[24];
        strcpy(buf, "Claude is thinking");
        switch (dots) {
        case 0: strcat(buf, "."); break;
        case 1: strcat(buf, ".."); break;
        case 2: strcat(buf, "..."); break;
        case 3: strcat(buf, ".."); break;
        }
        scr_puts(0, y, buf, ATTR_THINKING);
    }

    /* Pending exec prompt */
    if (s->pending_exec) {
        int py = CL_CHAT_BOT;
        char prompt[80];
        snprintf(prompt, sizeof(prompt), "Run: %s  Y/N?", s->pending_cmd);
        scr_fill(0, py, 80, 1, ' ', SCR_ATTR(SCR_BLACK, SCR_BLACK));
        scr_putsn(0, py, prompt, 79, ATTR_PENDING);
    }
}

void cl_render_statusbar(cl_state_t *s)
{
    char buf[20];
    int mc, j, t;
    char tmp[6];

    scr_fill(0, CL_STATUS_ROW, 80, 1, ' ', ATTR_STAT);

    /* Left: message count */
    j = 0; t = 0;
    buf[j++] = 'M'; buf[j++] = 's'; buf[j++] = 'g'; buf[j++] = 's';
    buf[j++] = ':'; buf[j++] = ' ';
    mc = s->message_count;
    if (mc == 0) { tmp[t++] = '0'; }
    else { while (mc > 0) { tmp[t++] = (char)('0' + mc % 10); mc /= 10; } }
    while (t > 0) buf[j++] = tmp[--t];
    buf[j] = '\0';
    scr_puts(1, CL_STATUS_ROW, buf, ATTR_STAT);

    /* Center: model or thinking */
    if (s->waiting) {
        scr_puts(30, CL_STATUS_ROW, "Thinking...", ATTR_STAT);
    } else {
        scr_putsn(24, CL_STATUS_ROW, s->model, 30, ATTR_STAT);
    }

    /* Right: key hints */
    scr_puts(56, CL_STATUS_ROW, "F2:New F4:Mode ^Q:Quit", ATTR_STAT);
}
