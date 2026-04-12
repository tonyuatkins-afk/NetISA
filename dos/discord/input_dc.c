/*
 * input_dc.c - Keyboard handler for Discord client
 *
 * Handles focus rotation, channel selection, message scrolling,
 * compose bar editing, and global shortcuts.
 */

#include "discord.h"
#include <string.h>

/* Extended key codes */
#define KEY_PGUP    0x4900
#define KEY_PGDN    0x5100
#define KEY_F1      0x3B00
#define KEY_F5      0x3F00
#define KEY_DELETE  0x5300

/* Ctrl+Q = 0x11 (ASCII DC1) */
#define KEY_CTRL_Q  0x11

/* Compose bar key handling */
static void compose_key(dc_state_t *s, int key)
{
    int ch = key & 0xFF;
    int scan = key & 0xFF00;

    /* Enter: send message */
    if (ch == 0x0D) {
        if (s->compose.len > 0)
            dc_send_message(s, s->compose.buf);
        return;
    }

    /* Escape: clear compose */
    if (ch == 0x1B) {
        s->compose.buf[0] = '\0';
        s->compose.len = 0;
        s->compose.cursor = 0;
        return;
    }

    /* Tab: rotate focus */
    if (ch == 0x09) {
        s->focus = DC_FOCUS_CHANNELS;
        return;
    }

    /* Backspace */
    if (ch == 0x08) {
        if (s->compose.cursor > 0) {
            int i;
            s->compose.cursor--;
            for (i = s->compose.cursor; i < s->compose.len - 1; i++)
                s->compose.buf[i] = s->compose.buf[i + 1];
            s->compose.len--;
            s->compose.buf[s->compose.len] = '\0';
        }
        return;
    }

    /* Extended keys */
    if (ch == 0) {
        switch (scan) {
        case KEY_LEFT:
            if (s->compose.cursor > 0) s->compose.cursor--;
            break;
        case KEY_RIGHT:
            if (s->compose.cursor < s->compose.len) s->compose.cursor++;
            break;
        case 0x4700: /* Home */
            s->compose.cursor = 0;
            break;
        case 0x4F00: /* End */
            s->compose.cursor = s->compose.len;
            break;
        case KEY_DELETE:
            if (s->compose.cursor < s->compose.len) {
                int i;
                for (i = s->compose.cursor; i < s->compose.len - 1; i++)
                    s->compose.buf[i] = s->compose.buf[i + 1];
                s->compose.len--;
                s->compose.buf[s->compose.len] = '\0';
            }
            break;
        }
        return;
    }

    /* Printable character */
    if (ch >= 0x20 && ch < 0x7F && s->compose.len < DC_MAX_COMPOSE) {
        int i;
        for (i = s->compose.len; i > s->compose.cursor; i--)
            s->compose.buf[i] = s->compose.buf[i - 1];
        s->compose.buf[s->compose.cursor] = (char)ch;
        s->compose.cursor++;
        s->compose.len++;
        s->compose.buf[s->compose.len] = '\0';
    }
}

void dc_handle_key(dc_state_t *s, int key)
{
    int ch = key & 0xFF;
    int scan = key & 0xFF00;

    /* Global: Ctrl+Q = quit */
    if (ch == KEY_CTRL_Q) {
        s->running = 0;
        return;
    }

    /* Global: Alt+1-9 quick channel switch */
    if (ch == 0 && scan >= 0x7800 && scan <= 0x8000) {
        /* Alt+1 = 0x7800, Alt+2 = 0x7900, ... Alt+9 = 0x8000 */
        int idx = (scan - 0x7800) >> 8;
        if (idx < s->channel_count) {
            dc_switch_channel(s, idx);
            s->focus = DC_FOCUS_COMPOSE;
        }
        return;
    }

    /* Dispatch by focus */
    switch (s->focus) {
    case DC_FOCUS_CHANNELS:
        if (ch == 0x09) {  /* Tab */
            s->focus = DC_FOCUS_MESSAGES;
            return;
        }
        if (ch == 0x0D) {  /* Enter */
            dc_switch_channel(s, s->selected_channel);
            s->focus = DC_FOCUS_COMPOSE;
            return;
        }
        if (ch == 0) {
            switch (scan) {
            case KEY_UP:
                if (s->selected_channel > 0)
                    s->selected_channel--;
                break;
            case KEY_DOWN:
                if (s->selected_channel < s->channel_count - 1)
                    s->selected_channel++;
                break;
            }
        }
        break;

    case DC_FOCUS_MESSAGES:
        if (ch == 0x09) {  /* Tab */
            s->focus = DC_FOCUS_COMPOSE;
            return;
        }
        if (ch == 0) {
            switch (scan) {
            case KEY_PGUP:
                s->msg_scroll += DC_CONTENT_ROWS;
                break;
            case KEY_PGDN:
                s->msg_scroll -= DC_CONTENT_ROWS;
                if (s->msg_scroll < 0) s->msg_scroll = 0;
                break;
            case KEY_UP:
                s->msg_scroll++;
                break;
            case KEY_DOWN:
                if (s->msg_scroll > 0) s->msg_scroll--;
                break;
            }
        }
        break;

    case DC_FOCUS_COMPOSE:
        compose_key(s, key);
        break;
    }
}
