/*
 * browser.c - Core browser state machine for Cathode v0.2
 *
 * Manages navigation via the HTML parser pipeline, history,
 * scrolling, and link/field selection.
 */

#include "browser.h"
#include "stub_pages.h"
#include "htmltok.h"
#include "fetch.h"
#include "bookmark.h"
#include "screen.h"
#include <string.h>
#include <malloc.h>

/* Static parser and fetch state — too large for stack (~1150 + ~280 bytes) */
static html_parser_t g_parser;
static fetch_state_t g_fetch_state;

/* Fetch callback: feeds HTML chunks to the parser */
static void html_callback(const char far *chunk, int len, void *userdata)
{
    html_parser_t *p = (html_parser_t *)userdata;
    html_parse_chunk(p, chunk, len);
}

void browser_init(browser_state_t *b)
{
    b->current_page = page_alloc();
    b->history_pos = -1;
    b->history_count = 0;
    b->running = 1;
    b->status_msg[0] = '\0';
    urlbar_init(&b->urlbar);
    search_init(&b->search);
}

void browser_shutdown(browser_state_t *b)
{
    if (b->current_page) {
        page_free(b->current_page);
        b->current_page = (page_buffer_t *)0;
    }
}

/* Internal navigate — loads the URL without touching history */
static void browser_load_url(browser_state_t *b, const char *url)
{
    int is_home;
    char far *bm_html = (char far *)0;

    if (!b->current_page || !url || !url[0])
        return;

    strcpy(b->status_msg, "Loading...");

    /* Clear page for new content */
    page_clear(b->current_page);

    is_home = (strcmp(url, "about:home") == 0 || strcmp(url, "") == 0);

    /* For about:home, write the procedural block-art logo first */
    if (is_home)
        build_home_logo(b->current_page);

    /* Set up URL in page buffer for relative URL resolution */
    strncpy(b->current_page->url, url, 255);
    b->current_page->url[255] = '\0';

    /* Initialize parser */
    html_init(&g_parser, b->current_page);

    /* If logo was drawn, start parser below it */
    if (is_home) {
        g_parser.row = 8;
        g_parser.col = 0;
    }

    /* Handle about:bookmarks specially — dynamic HTML buffer */
    if (strcmp(url, "about:bookmarks") == 0) {
        bm_html = bookmark_build_html();
        if (bm_html) {
            /* Measure length */
            unsigned int len = 0;
            const char far *p = bm_html;
            while (p[len]) len++;
            html_parse_chunk(&g_parser, bm_html, (int)len);
        } else {
            /* No bookmarks — use inline fallback */
            static const char far fb[] =
                "<h1>Bookmarks</h1><p>No bookmarks saved.</p>";
            unsigned int len = 0;
            while (fb[len]) len++;
            html_parse_chunk(&g_parser, fb, (int)len);
        }
    } else {
        /* Fetch via the standard pipeline (routes about: and http/https) */
        fetch_page(url, html_callback, &g_parser, &g_fetch_state);
    }

    /* Finalize parsing */
    html_finish(&g_parser);

    /* Free bookmark buffer if allocated */
    if (bm_html) _ffree(bm_html);

    /* Update URL bar */
    urlbar_set(&b->urlbar, b->current_page->url);

    /* Cancel any active search */
    search_cancel(&b->search);

    b->status_msg[0] = '\0';
}

void browser_navigate(browser_state_t *b, const char *url)
{
    if (!b->current_page || !url || !url[0])
        return;

    browser_load_url(b, url);

    /* Add to history */
    if (b->history_pos < HISTORY_MAX - 1) {
        b->history_pos++;
    } else {
        memmove(b->history[0], b->history[1],
                (unsigned)(HISTORY_MAX - 1) * 256u);
    }
    strncpy(b->history[b->history_pos], url, 255);
    b->history[b->history_pos][255] = '\0';
    b->history_count = b->history_pos + 1;
}

void browser_back(browser_state_t *b)
{
    if (!b->current_page) return;
    if (b->history_pos > 0) {
        b->history_pos--;
        browser_load_url(b, b->history[b->history_pos]);
    }
}

void browser_forward(browser_state_t *b)
{
    if (!b->current_page) return;
    if (b->history_pos < b->history_count - 1) {
        b->history_pos++;
        browser_load_url(b, b->history[b->history_pos]);
    }
}

void browser_reload(browser_state_t *b)
{
    if (!b->current_page) return;
    if (b->history_pos >= 0) {
        strcpy(b->status_msg, "Reloading...");
        browser_load_url(b, b->history[b->history_pos]);
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
    char url_copy[LINK_URL_MAX];

    if (!b->current_page)
        return;
    sel = b->current_page->selected_link;
    if (sel < 0 || sel >= b->current_page->link_count)
        return;

    strncpy(url_copy, b->current_page->links[sel].url, LINK_URL_MAX - 1);
    url_copy[LINK_URL_MAX - 1] = '\0';
    browser_navigate(b, url_copy);
}
