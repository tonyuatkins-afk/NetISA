/*
 * browser.c - Core browser state machine for Cathode
 *
 * Manages navigation, history, scrolling, and link selection.
 */

#include "browser.h"
#include "stub_pages.h"
#include "screen.h"
#include <string.h>

void browser_init(browser_state_t *b)
{
    b->current_page = page_alloc();
    b->history_pos = -1;
    b->history_count = 0;
    b->running = 1;
    b->menu_open = 0;
    b->status_msg[0] = '\0';
    urlbar_init(&b->urlbar);
}

void browser_shutdown(browser_state_t *b)
{
    if (b->current_page) {
        page_free(b->current_page);
        b->current_page = (page_buffer_t far *)0;
    }
}

void browser_navigate(browser_state_t *b, const char *url)
{
    if (!b->current_page || !url || !url[0])
        return;

    strcpy(b->status_msg, "Loading...");

    /* Fetch page (stub for now) */
    stub_fetch_page(url, b->current_page);

    /* Update URL bar */
    urlbar_set(&b->urlbar, b->current_page->url);

    /* Add to history */
    if (b->history_pos < HISTORY_MAX - 1) {
        b->history_pos++;
        strncpy(b->history[b->history_pos], url, 255);
        b->history[b->history_pos][255] = '\0';
        b->history_count = b->history_pos + 1;
    }

    b->status_msg[0] = '\0';
}

void browser_back(browser_state_t *b)
{
    if (b->history_pos > 0) {
        b->history_pos--;
        stub_fetch_page(b->history[b->history_pos], b->current_page);
        urlbar_set(&b->urlbar, b->current_page->url);
    }
}

void browser_forward(browser_state_t *b)
{
    if (b->history_pos < b->history_count - 1) {
        b->history_pos++;
        stub_fetch_page(b->history[b->history_pos], b->current_page);
        urlbar_set(&b->urlbar, b->current_page->url);
    }
}

void browser_reload(browser_state_t *b)
{
    if (b->history_pos >= 0) {
        strcpy(b->status_msg, "Reloading...");
        stub_fetch_page(b->history[b->history_pos], b->current_page);
        urlbar_set(&b->urlbar, b->current_page->url);
        b->status_msg[0] = '\0';
    }
}

void browser_scroll(browser_state_t *b, int delta)
{
    int max_scroll;
    if (!b->current_page)
        return;

    max_scroll = b->current_page->total_rows - PAGE_VIEWPORT;
    if (max_scroll < 0) max_scroll = 0;

    b->current_page->scroll_pos += delta;

    if (b->current_page->scroll_pos < 0)
        b->current_page->scroll_pos = 0;
    if (b->current_page->scroll_pos > max_scroll)
        b->current_page->scroll_pos = max_scroll;
}

void browser_scroll_to(browser_state_t *b, int pos)
{
    int max_scroll;
    if (!b->current_page)
        return;

    max_scroll = b->current_page->total_rows - PAGE_VIEWPORT;
    if (max_scroll < 0) max_scroll = 0;

    if (pos < 0) pos = 0;
    if (pos > max_scroll) pos = max_scroll;
    b->current_page->scroll_pos = pos;
}

void browser_select_link(browser_state_t *b, int delta)
{
    int sel, cnt;
    if (!b->current_page || b->current_page->link_count == 0)
        return;

    cnt = b->current_page->link_count;
    sel = b->current_page->selected_link;

    if (sel < 0) {
        /* No link selected yet: select first or last */
        sel = (delta > 0) ? 0 : cnt - 1;
    } else {
        sel += delta;
        if (sel < 0) sel = cnt - 1;
        if (sel >= cnt) sel = 0;
    }

    b->current_page->selected_link = sel;

    /* Scroll to make selected link visible */
    {
        int link_row = b->current_page->links[sel].start_row;
        int scroll = b->current_page->scroll_pos;
        if (link_row < scroll)
            b->current_page->scroll_pos = link_row;
        else if (link_row >= scroll + PAGE_VIEWPORT)
            b->current_page->scroll_pos = link_row - PAGE_VIEWPORT + 1;
    }
}

void browser_follow_link(browser_state_t *b)
{
    int sel;
    if (!b->current_page)
        return;
    sel = b->current_page->selected_link;
    if (sel < 0 || sel >= b->current_page->link_count)
        return;

    browser_navigate(b, b->current_page->links[sel].url);
}
