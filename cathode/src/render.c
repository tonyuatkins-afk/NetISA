/*
 * render.c - Page renderer for Cathode v0.2
 *
 * Copies cells from page buffer to VGA text buffer via screen.h.
 * Adds scrollbar, search highlighting, and chrome.
 */

#include "render.h"
#include "search.h"
#include "screen.h"
#include <string.h>

void render_page(page_buffer_t *page, search_state_t *search)
{
    int vrow, prow, col;
    int sel = page->selected_link;
    int search_active = (search && search->active && search->query_len > 0);

    /* Pre-compute which viewport rows contain search matches.
     * Avoids O(viewport*matches) search_is_match calls per frame. */
    char match_row_flag[PAGE_VIEWPORT];
    if (search_active) {
        int i, cap;
        memset(match_row_flag, 0, PAGE_VIEWPORT);
        cap = search->total_matches;
        if (cap > SEARCH_MAX_MATCHES) cap = SEARCH_MAX_MATCHES;
        for (i = 0; i < cap; i++) {
            int vr = search->matches[i].row - page->scroll_pos;
            if (vr >= 0 && vr < PAGE_VIEWPORT)
                match_row_flag[vr] = 1;
        }
    } else {
        memset(match_row_flag, 0, PAGE_VIEWPORT);
    }

    for (vrow = 0; vrow < PAGE_VIEWPORT; vrow++) {
        prow = page->scroll_pos + vrow;
        if (prow < page->total_rows) {
            for (col = 0; col < 79; col++) {
                page_cell_t c = page_get_cell(page, prow, col);
                unsigned char type = page_get_meta(page, prow, col);
                unsigned char attr = c.attr;

                /* Link highlighting */
                if (type == CELL_LINK) {
                    unsigned short lid = page_get_linkid(page, prow, col);
                    if (sel >= 0 && lid == (unsigned short)sel)
                        attr = ATTR_LINK_SEL;
                    else
                        attr = ATTR_LINK;
                }

                /* Search highlighting (takes priority over links).
                 * Only check rows flagged in pre-computed bitmap. */
                if (search_active && match_row_flag[vrow]) {
                    if (search_is_match(search, page, prow, col)) {
                        if (prow == search->match_row &&
                            col >= search->match_col &&
                            col < search->match_col + search->query_len)
                            attr = ATTR_SEARCH_CUR;
                        else
                            attr = ATTR_SEARCH_HIT;
                    }
                }

                scr_putc(col, LAYOUT_CONTENT_TOP + vrow, c.ch, attr);
            }
        } else {
            /* Below content: show tilde like vi */
            scr_putc(0, LAYOUT_CONTENT_TOP + vrow, '~', ATTR_DIM);
            scr_hline(1, LAYOUT_CONTENT_TOP + vrow, 78, ' ', ATTR_NORMAL);
        }
    }
}

void render_scrollbar(page_buffer_t *page)
{
    int vp = PAGE_VIEWPORT;
    int total = page->total_rows;
    int pos = page->scroll_pos;
    int max_scroll = total - vp;
    int thumb_row, r;

    if (max_scroll <= 0) {
        for (r = 0; r < vp; r++)
            scr_putc(79, LAYOUT_CONTENT_TOP + r, ' ', ATTR_NORMAL);
        return;
    }

    thumb_row = (pos * (vp - 1)) / max_scroll;

    for (r = 0; r < vp; r++) {
        if (r == thumb_row)
            scr_putc(79, LAYOUT_CONTENT_TOP + r, (char)0xDB, ATTR_BORDER);
        else
            scr_putc(79, LAYOUT_CONTENT_TOP + r, (char)0xB0, ATTR_DIM);
    }
}

void render_titlebar(const char *title)
{
    scr_fill(0, LAYOUT_TITLE_ROW, PAGE_COLS, 1, ' ', ATTR_STATUS);
    scr_putc(1, LAYOUT_TITLE_ROW, ' ', ATTR_STATUS);
    scr_putsn(2, LAYOUT_TITLE_ROW, title, 58, ATTR_STATUS);
    scr_puts(66, LAYOUT_TITLE_ROW, "Cathode v0.2", ATTR_STATUS);
}

void render_urlbar(const char *url, int editing, int cursor_pos)
{
    scr_fill(0, LAYOUT_URL_ROW, PAGE_COLS, 1, ' ', ATTR_NORMAL);
    scr_putsn(1, LAYOUT_URL_ROW, url, PAGE_COLS - 2, ATTR_INPUT);

    if (editing) {
        int cpos = (cursor_pos < PAGE_COLS - 2) ? cursor_pos : PAGE_COLS - 2;
        scr_cursor_show();
        scr_cursor_pos(1 + cpos, LAYOUT_URL_ROW);
    } else {
        scr_cursor_hide();
    }
}

void render_searchbar(search_state_t *search)
{
    scr_fill(0, LAYOUT_URL_ROW, PAGE_COLS, 1, ' ', ATTR_NORMAL);
    scr_puts(1, LAYOUT_URL_ROW, "Find: ", ATTR_HIGHLIGHT);
    scr_putsn(7, LAYOUT_URL_ROW, search->query, PAGE_COLS - 9, ATTR_INPUT);

    if (search->editing) {
        int cpos = search->cursor;
        if (cpos > PAGE_COLS - 9) cpos = PAGE_COLS - 9;
        scr_cursor_show();
        scr_cursor_pos(7 + cpos, LAYOUT_URL_ROW);
    }
}

void render_statusbar(page_buffer_t *page, const char *status_msg)
{
    char buf[81];
    int pos, total;

    scr_fill(0, LAYOUT_STATUS_ROW, PAGE_COLS, 1, ' ', ATTR_STATUS);

    /* Left: scroll position */
    pos = page->scroll_pos + 1;
    total = page->total_rows;
    {
        char tmp[6];
        int i = 0, t, v;
        buf[i++] = ' ';
        buf[i++] = 'L';
        buf[i++] = 'n';
        buf[i++] = ' ';
        v = pos; t = 0;
        if (v == 0) { tmp[t++] = '0'; }
        else { while (v > 0) { tmp[t++] = (char)('0' + v % 10); v /= 10; } }
        while (t > 0) buf[i++] = tmp[--t];
        buf[i++] = '/';
        v = total; t = 0;
        if (v == 0) { tmp[t++] = '0'; }
        else { while (v > 0) { tmp[t++] = (char)('0' + v % 10); v /= 10; } }
        while (t > 0) buf[i++] = tmp[--t];
        buf[i] = '\0';
    }
    scr_puts(0, LAYOUT_STATUS_ROW, buf, ATTR_STATUS);

    /* Center: link count */
    {
        char lbuf[20];
        char tmp[6];
        int i = 0, t = 0, v = page->link_count;
        lbuf[i++] = 'L';
        lbuf[i++] = 'i';
        lbuf[i++] = 'n';
        lbuf[i++] = 'k';
        lbuf[i++] = 's';
        lbuf[i++] = ':';
        if (v == 0) { tmp[t++] = '0'; }
        else { while (v > 0) { tmp[t++] = (char)('0' + v % 10); v /= 10; } }
        while (t > 0) lbuf[i++] = tmp[--t];
        lbuf[i] = '\0';
        scr_puts(30, LAYOUT_STATUS_ROW, lbuf, ATTR_STATUS);
    }

    /* Right: key hints or status message */
    if (status_msg && status_msg[0]) {
        scr_putsn(50, LAYOUT_STATUS_ROW, status_msg, 29, ATTR_STATUS);
    } else {
        scr_puts(42, LAYOUT_STATUS_ROW, "F5:Rel Tab:Link ^F:Find Esc:Quit", ATTR_STATUS);
    }
}

void render_chrome(void)
{
    scr_hline(0, LAYOUT_SEP1_ROW, PAGE_COLS, (char)BOX_H, ATTR_BORDER);
    scr_hline(0, LAYOUT_SEP2_ROW, PAGE_COLS, (char)BOX_H, ATTR_BORDER);
}

void render_all(page_buffer_t *page, const char *url,
                int url_editing, int url_cursor,
                const char *status_msg, search_state_t *search)
{
    render_titlebar(page->title);

    if (search && search->active)
        render_searchbar(search);
    else
        render_urlbar(url, url_editing, url_cursor);

    render_chrome();
    render_page(page, search);
    render_scrollbar(page);
    render_statusbar(page, status_msg);
}
