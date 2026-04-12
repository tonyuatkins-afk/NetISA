/*
 * input.c - Keyboard handler and command dispatch for Cathode browser
 */

#include "input.h"
#include "screen.h"

void input_handle_key(browser_state_t *b, int key)
{
    int ch = key & 0xFF;
    int scan = key & 0xFF00;

    /* URL bar editing mode: delegate to urlbar handler */
    if (b->urlbar.editing) {
        int result = urlbar_handle_key(&b->urlbar, key);
        if (result == 1) {
            /* Enter: navigate to typed URL */
            browser_navigate(b, b->urlbar.buf);
        } else if (result == -1) {
            /* Escape: cancel, restore URL */
            if (b->current_page)
                urlbar_cancel_edit(&b->urlbar, b->current_page->url);
        }
        return;
    }

    /* Normal browsing mode */
    switch (ch) {
    case 0x1B:  /* Escape = quit */
        b->running = 0;
        return;
    case 0x0D:  /* Enter = follow link */
        browser_follow_link(b);
        return;
    case 0x08:  /* Backspace = go back */
        browser_back(b);
        return;
    case 0x0C:  /* Ctrl+L = focus URL bar */
        urlbar_start_edit(&b->urlbar);
        return;
    case 0x09:  /* Tab = next link */
        browser_select_link(b, 1);
        return;
    }

    /* Extended keys (scan code, ch == 0) */
    if (ch == 0) {
        switch (scan) {
        case KEY_UP:
            browser_scroll(b, -1);
            break;
        case KEY_DOWN:
            browser_scroll(b, 1);
            break;
        case KEY_PGUP:
            browser_scroll(b, -PAGE_VIEWPORT);
            break;
        case KEY_PGDN:
            browser_scroll(b, PAGE_VIEWPORT);
            break;
        case KEY_HOME:
            browser_scroll_to(b, 0);
            break;
        case KEY_END:
            if (b->current_page)
                browser_scroll_to(b, b->current_page->total_rows - PAGE_VIEWPORT);
            break;
        case KEY_F5:
            browser_reload(b);
            break;
        case KEY_F6:
            urlbar_start_edit(&b->urlbar);
            break;
        case KEY_SHIFT_TAB:
            browser_select_link(b, -1);
            break;
        }
    }
}
