/*
 * urlbar.c - URL bar input and display for Cathode browser
 */

#include "urlbar.h"
#include "screen.h"
#include "input.h"
#include <string.h>

void urlbar_init(urlbar_t *u)
{
    u->buf[0] = '\0';
    u->len = 0;
    u->cursor = 0;
    u->editing = 0;
}

void urlbar_set(urlbar_t *u, const char *url)
{
    strncpy(u->buf, url, URL_MAX_LEN);
    u->buf[URL_MAX_LEN] = '\0';
    u->len = strlen(u->buf);
    u->cursor = u->len;
}

void urlbar_start_edit(urlbar_t *u)
{
    u->editing = 1;
    u->cursor = u->len;
}

void urlbar_cancel_edit(urlbar_t *u, const char *restore_url)
{
    u->editing = 0;
    urlbar_set(u, restore_url);
}

int urlbar_handle_key(urlbar_t *u, int key)
{
    int ch = key & 0xFF;
    int scan = key & 0xFF00;

    /* Enter: submit URL */
    if (ch == 0x0D) {
        u->editing = 0;
        return 1;
    }

    /* Escape: cancel */
    if (ch == 0x1B) {
        u->editing = 0;
        return -1;
    }

    /* Backspace */
    if (ch == 0x08) {
        if (u->cursor > 0) {
            int i;
            u->cursor--;
            for (i = u->cursor; i < u->len - 1; i++)
                u->buf[i] = u->buf[i + 1];
            u->len--;
            u->buf[u->len] = '\0';
        }
        return 0;
    }

    /* Extended keys */
    if (ch == 0) {
        switch (scan) {
        case KEY_LEFT:
            if (u->cursor > 0) u->cursor--;
            break;
        case KEY_RIGHT:
            if (u->cursor < u->len) u->cursor++;
            break;
        case KEY_HOME:
            u->cursor = 0;
            break;
        case KEY_END:
            u->cursor = u->len;
            break;
        case KEY_DELETE:
            if (u->cursor < u->len) {
                int i;
                for (i = u->cursor; i < u->len - 1; i++)
                    u->buf[i] = u->buf[i + 1];
                u->len--;
                u->buf[u->len] = '\0';
            }
            break;
        }
        return 0;
    }

    /* Printable character */
    if (ch >= 0x20 && ch < 0x7F && u->len < URL_MAX_LEN) {
        int i;
        for (i = u->len; i > u->cursor; i--)
            u->buf[i] = u->buf[i - 1];
        u->buf[u->cursor] = (char)ch;
        u->cursor++;
        u->len++;
        u->buf[u->len] = '\0';
    }

    return 0;
}
