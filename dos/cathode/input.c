/*
 * input.c - Keyboard handler and command dispatch for Cathode v0.2
 */

#include "input.h"
#include "screen.h"
#include "bookmark.h"
#include <string.h>

void input_handle_key(browser_state_t *b, int key)
{
    int ch = key & 0xFF;
    int scan = key & 0xFF00;

    /* === Search bar editing mode === */
    if (b->search.editing) {
        int result = search_handle_key(&b->search, key);
        if (result == 1) {
            /* Enter: execute first search */
            search_find_next(&b->search, b->current_page, 1);
            /* Scroll to match if found */
            if (b->search.match_row >= 0) {
                int target = b->search.match_row - PAGE_VIEWPORT / 2;
                browser_scroll_to(b, target);
            }
        }
        /* result == -1: cancel handled in search_cancel */
        return;
    }

    /* === URL bar editing mode === */
    if (b->urlbar.editing) {
        int result = urlbar_handle_key(&b->urlbar, key);
        if (result == 1) {
            browser_navigate(b, b->urlbar.buf);
        } else if (result == -1) {
            if (b->current_page)
                urlbar_cancel_edit(&b->urlbar, b->current_page->url);
        }
        return;
    }

    /* === Normal browsing mode === */

    /* N/Shift+N for search cycling (only when search is active, not editing) */
    if (b->search.active && !b->search.editing) {
        if (ch == 'n') {
            search_find_next(&b->search, b->current_page, 1);
            if (b->search.match_row >= 0)
                browser_scroll_to(b, b->search.match_row - PAGE_VIEWPORT / 2);
            return;
        }
        if (ch == 'N') {
            search_find_next(&b->search, b->current_page, -1);
            if (b->search.match_row >= 0)
                browser_scroll_to(b, b->search.match_row - PAGE_VIEWPORT / 2);
            return;
        }
    }

    switch (ch) {
    case 0x1B:  /* Escape */
        if (b->search.active) {
            search_cancel(&b->search);
            return;
        }
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
    case 0x06:  /* Ctrl+F = find */
        search_start(&b->search);
        return;
    case 0x04:  /* Ctrl+D = bookmark */
        if (b->current_page && b->current_page->url[0]) {
            if (bookmark_add(b->current_page->url) == 0)
                strcpy(b->status_msg, "Bookmarked!");
            else
                strcpy(b->status_msg, "Bookmark failed");
        }
        return;
    case 0x02:  /* Ctrl+B = view bookmarks */
        browser_navigate(b, "about:bookmarks");
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
