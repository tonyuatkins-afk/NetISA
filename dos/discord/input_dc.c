/*
 * input_dc.c - Keyboard handler for Discord v2 DOS client
 *
 * Single entry point: dc_handle_key() routes all keyboard input
 * based on current focus state. Global shortcuts are checked first,
 * then search mode, then overlay dismissal, then focus-specific.
 *
 * Target: 8088, OpenWatcom C, small memory model.
 */

#include "discord.h"
#include <string.h>

/* Count newline characters in compose buffer to derive line count */
static int compose_lines(const char *buf, int len)
{
    int n = 1;
    int i;
    for (i = 0; i < len; i++) {
        if (buf[i] == '\n') n++;
    }
    return n;
}

/* Maximum scroll offset for message view */
static int msg_scroll_max(dc_state_t *s)
{
    int max = s->msg_count - DC_CONTENT_ROWS;
    return (max > 0) ? max : 0;
}

void dc_handle_key(dc_state_t *s, int key)
{
    /* ============================================================
     * Global shortcuts (checked first, regardless of focus)
     * ============================================================ */

    /* Ctrl+Q: quit */
    if (key == KEY_CTRL_Q) {
        s->running = 0;
        return;
    }

    /* F1: toggle help overlay */
    if (key == KEY_F1) {
        s->show_help = !s->show_help;
        s->dirty = 1;
        return;
    }

    /* F9: toggle sound */
    if (key == KEY_F9) {
        s->config.sound = !s->config.sound;
        s->dirty = 1;
        return;
    }

    /* Alt+U: toggle user list overlay */
    if (key == KEY_ALT_U) {
        s->show_userlist = !s->show_userlist;
        s->dirty = 1;
        return;
    }

    /* Alt+1 through Alt+8: quick channel switch */
    if (key >= KEY_ALT_1 && key <= KEY_ALT_8) {
        int ch = (key >> 8) - 0x78;
        if (ch < s->channel_count) {
            dc_switch_channel(s, ch);
        }
        s->dirty = 1;
        return;
    }

    /* Tab: cycle focus CHANNELS -> MESSAGES -> COMPOSE -> CHANNELS */
    if (key == KEY_TAB) {
        s->focus++;
        if (s->focus > DC_FOCUS_COMPOSE)
            s->focus = DC_FOCUS_CHANNELS;
        s->dirty = 1;
        return;
    }

    /* Ctrl+F: open search */
#if FEAT_SEARCH
    if (key == KEY_CTRL_F) {
        dc_search_open(s);
        s->dirty = 1;
        return;
    }
#endif

    /* ============================================================
     * Search mode: route keys to search handler
     * ============================================================ */
#if FEAT_SEARCH
    if (s->search.active) {
        dc_search_handle_key(s, key);
        s->dirty = 1;
        return;
    }
#endif

    /* ============================================================
     * Overlay dismissal: any key closes help or user list
     * ============================================================ */
    if (s->show_help) {
        s->show_help = 0;
        s->dirty = 1;
        return;
    }

    if (s->show_userlist) {
        s->show_userlist = 0;
        s->dirty = 1;
        return;
    }

    /* ============================================================
     * Focus-specific handlers
     * ============================================================ */
    switch (s->focus) {

    /* --------------------------------------------------------
     * Channel list navigation
     * -------------------------------------------------------- */
    case DC_FOCUS_CHANNELS:
        if (key == KEY_UP) {
            s->selected_channel--;
            if (s->selected_channel < 0)
                s->selected_channel = s->channel_count - 1;
            s->dirty = 1;
        } else if (key == KEY_DOWN) {
            s->selected_channel++;
            if (s->selected_channel >= s->channel_count)
                s->selected_channel = 0;
            s->dirty = 1;
        } else if (key == KEY_ENTER) {
            dc_switch_channel(s, s->selected_channel);
            s->dirty = 1;
        }
        break;

    /* --------------------------------------------------------
     * Message view scrolling
     * -------------------------------------------------------- */
    case DC_FOCUS_MESSAGES: {
        int max = msg_scroll_max(s);

        if (key == KEY_UP) {
            s->msg_scroll++;
            if (s->msg_scroll > max)
                s->msg_scroll = max;
            s->dirty = 1;
        } else if (key == KEY_DOWN) {
            s->msg_scroll--;
            if (s->msg_scroll < 0)
                s->msg_scroll = 0;
            s->dirty = 1;
        } else if (key == KEY_PGUP) {
            s->msg_scroll += 20;
            if (s->msg_scroll > max)
                s->msg_scroll = max;
            s->dirty = 1;
        } else if (key == KEY_PGDN) {
            s->msg_scroll -= 20;
            if (s->msg_scroll < 0)
                s->msg_scroll = 0;
            s->dirty = 1;
        } else if (key == KEY_HOME) {
            s->msg_scroll = max;
            s->dirty = 1;
        } else if (key == KEY_END) {
            s->msg_scroll = 0;
            s->dirty = 1;
        }
#if FEAT_SEARCH
        else if (key == 'n') {
            dc_search_next(s);
            s->dirty = 1;
        } else if (key == 'N') {
            dc_search_prev(s);
            s->dirty = 1;
        }
#endif
        break;
    }

    /* --------------------------------------------------------
     * Compose buffer editing
     * -------------------------------------------------------- */
    case DC_FOCUS_COMPOSE: {
        dc_compose_t *c = &s->compose;

        if (key == KEY_ENTER) {
            /* Send message if buffer is non-empty */
            if (c->len > 0) {
                dc_send_message(s, c->buf);
                c->len = 0;
                c->cursor = 0;
                c->buf[0] = '\0';
                c->scroll_line = 0;
                s->dirty = 1;
            }
        }
#if FEAT_MULTILINE
        else if (key == KEY_SHIFT_ENTER) {
            /* Insert newline if under line limit and buffer limit */
            int lines = compose_lines(c->buf, c->len);
            if (lines < DC_COMPOSE_MAX_LINES && c->len < DC_MAX_COMPOSE) {
                memmove(&c->buf[c->cursor + 1], &c->buf[c->cursor],
                        c->len - c->cursor + 1);
                c->buf[c->cursor] = '\n';
                c->len++;
                c->cursor++;
                s->dirty = 1;
            }
        }
#endif
        else if (key == KEY_BACKSPACE) {
            if (c->cursor > 0) {
                c->cursor--;
                memmove(&c->buf[c->cursor], &c->buf[c->cursor + 1],
                        c->len - c->cursor);
                c->len--;
                s->dirty = 1;
            }
        } else if (key == KEY_DEL) {
            if (c->cursor < c->len) {
                memmove(&c->buf[c->cursor], &c->buf[c->cursor + 1],
                        c->len - c->cursor);
                c->len--;
                s->dirty = 1;
            }
        } else if (key == KEY_LEFT) {
            if (c->cursor > 0) {
                c->cursor--;
                s->dirty = 1;
            }
        } else if (key == KEY_RIGHT) {
            if (c->cursor < c->len) {
                c->cursor++;
                s->dirty = 1;
            }
        } else if (key == KEY_HOME) {
            c->cursor = 0;
            s->dirty = 1;
        } else if (key == KEY_END) {
            c->cursor = c->len;
            s->dirty = 1;
        } else if (key == KEY_ESC) {
            c->len = 0;
            c->cursor = 0;
            c->buf[0] = '\0';
            c->scroll_line = 0;
            s->dirty = 1;
        } else if (key >= 0x20 && key <= 0x7E) {
            /* Printable ASCII: insert at cursor */
            if (c->len < DC_MAX_COMPOSE) {
                memmove(&c->buf[c->cursor + 1], &c->buf[c->cursor],
                        c->len - c->cursor + 1);
                c->buf[c->cursor] = (char)key;
                c->len++;
                c->cursor++;
                s->dirty = 1;
            }
        }
        break;
    }

    } /* switch(focus) */
}
