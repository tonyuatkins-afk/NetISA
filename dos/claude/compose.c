/*
 * compose.c - Multi-line input composition
 *
 * Manages the 3-line compose area (rows 21-23) with cursor,
 * basic editing (left, right, home, end, backspace, delete),
 * Enter to send, Escape to clear/quit.
 */

#include "claude.h"
#include <string.h>

/* Extended key codes */
#define KEY_HOME    0x4700
#define KEY_END     0x4F00
#define KEY_DELETE  0x5300

/* Compose area attributes */
#define ATTR_PROMPT     SCR_ATTR(SCR_GREEN, SCR_BLACK)
#define ATTR_COMPOSE    SCR_ATTR(SCR_YELLOW, SCR_BLACK)
#define ATTR_WAIT       SCR_ATTR(SCR_DARKGRAY, SCR_BLACK)

void cl_render_compose(cl_state_t *s)
{
    int row, col;
    int chars_per_line = 78;  /* 80 - 2 for "> " prompt */
    int i, cx, cy;

    /* Clear compose area */
    scr_fill(0, CL_COMPOSE_TOP, 80, CL_COMPOSE_ROWS, ' ',
             SCR_ATTR(SCR_BLACK, SCR_BLACK));

    if (s->waiting) {
        scr_puts(0, CL_COMPOSE_TOP, "> ", ATTR_PROMPT);
        scr_puts(2, CL_COMPOSE_TOP, "Waiting...", ATTR_WAIT);
        scr_cursor_hide();
        return;
    }

    /* Draw prompt */
    scr_puts(0, CL_COMPOSE_TOP, "> ", ATTR_PROMPT);

    /* Render compose text with wrapping across 3 lines */
    col = 2;
    row = CL_COMPOSE_TOP;
    for (i = 0; i < s->compose_len; i++) {
        if (col >= 80) {
            col = 2;
            row++;
            if (row > CL_COMPOSE_BOT) break;
        }
        scr_putc(col, row, s->compose_buf[i], ATTR_COMPOSE);
        col++;
    }

    /* Position cursor */
    cx = 2 + (s->compose_cursor % chars_per_line);
    cy = CL_COMPOSE_TOP + (s->compose_cursor / chars_per_line);
    if (cy <= CL_COMPOSE_BOT) {
        scr_cursor_show();
        scr_cursor_pos(cx, cy);
    } else {
        scr_cursor_hide();
    }
}

void cl_compose_key(cl_state_t *s, int key)
{
    int ch = key & 0xFF;
    int scan = key & 0xFF00;
    int max_chars = CL_COMPOSE_ROWS * 78;  /* 3 lines x 78 chars */

    if (max_chars > MAX_COMPOSE_LEN) max_chars = MAX_COMPOSE_LEN;

    /* Enter: send message */
    if (ch == 0x0D) {
        if (s->compose_len > 0) {
            s->compose_buf[s->compose_len] = '\0';
            cl_send_message(s, s->compose_buf);
            s->compose_buf[0] = '\0';
            s->compose_len = 0;
            s->compose_cursor = 0;
        }
        return;
    }

    /* Escape: clear buffer, or quit if empty */
    if (ch == 0x1B) {
        if (s->compose_len > 0) {
            s->compose_buf[0] = '\0';
            s->compose_len = 0;
            s->compose_cursor = 0;
        } else {
            s->running = 0;
        }
        return;
    }

    /* Backspace */
    if (ch == 0x08) {
        if (s->compose_cursor > 0) {
            int i;
            s->compose_cursor--;
            for (i = s->compose_cursor; i < s->compose_len - 1; i++)
                s->compose_buf[i] = s->compose_buf[i + 1];
            s->compose_len--;
            s->compose_buf[s->compose_len] = '\0';
        }
        return;
    }

    /* Extended keys */
    if (ch == 0) {
        switch (scan) {
        case KEY_LEFT:
            if (s->compose_cursor > 0) s->compose_cursor--;
            break;
        case KEY_RIGHT:
            if (s->compose_cursor < s->compose_len) s->compose_cursor++;
            break;
        case KEY_HOME:
            s->compose_cursor = 0;
            break;
        case KEY_END:
            s->compose_cursor = s->compose_len;
            break;
        case KEY_DELETE:
            if (s->compose_cursor < s->compose_len) {
                int i;
                for (i = s->compose_cursor; i < s->compose_len - 1; i++)
                    s->compose_buf[i] = s->compose_buf[i + 1];
                s->compose_len--;
                s->compose_buf[s->compose_len] = '\0';
            }
            break;
        }
        return;
    }

    /* Printable character */
    if (ch >= 0x20 && ch < 0x7F && s->compose_len < max_chars) {
        int i;
        for (i = s->compose_len; i > s->compose_cursor; i--)
            s->compose_buf[i] = s->compose_buf[i - 1];
        s->compose_buf[s->compose_cursor] = (char)ch;
        s->compose_cursor++;
        s->compose_len++;
        s->compose_buf[s->compose_len] = '\0';
    }
}
