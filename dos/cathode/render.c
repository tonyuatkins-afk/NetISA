/*
 * render.c - Page renderer for Cathode browser
 *
 * Copies cells from page buffer to VGA text buffer via screen.h.
 */

#include "render.h"
#include "screen.h"
#include <string.h>

void render_page(page_buffer_t *page)
{
    int vrow, prow, col;
    int sel = page->selected_link;

    for (vrow = 0; vrow < PAGE_VIEWPORT; vrow++) {
        prow = page->scroll_pos + vrow;
        if (prow < page->total_rows) {
            for (col = 0; col < PAGE_COLS; col++) {
                page_cell_t c = page_get_cell(page, prow, col);
                unsigned char type = page_get_meta(page, prow, col);
                unsigned char attr = c.attr;

                /* Highlight links */
                if (type == CELL_LINK) {
                    unsigned short lid = page_get_linkid(page, prow, col);
                    if ((int)lid == sel)
                        attr = ATTR_LINK_SEL;
                    else
                        attr = ATTR_LINK;
                }

                scr_putc(col, LAYOUT_CONTENT_TOP + vrow, c.ch, attr);
            }
        } else {
            /* Below content: show tilde like vi */
            scr_putc(0, LAYOUT_CONTENT_TOP + vrow, '~', ATTR_DIM);
            scr_hline(1, LAYOUT_CONTENT_TOP + vrow, PAGE_COLS - 1, ' ',
                       ATTR_NORMAL);
        }
    }
}

void render_titlebar(const char *title)
{
    scr_fill(0, LAYOUT_TITLE_ROW, PAGE_COLS, 1, ' ', ATTR_STATUS);
    scr_putc(1, LAYOUT_TITLE_ROW, ' ', ATTR_STATUS);
    scr_putsn(2, LAYOUT_TITLE_ROW, title, 58, ATTR_STATUS);
    scr_puts(66, LAYOUT_TITLE_ROW, "Cathode v0.1", ATTR_STATUS);
}

void render_urlbar(const char *url, int editing, int cursor_pos)
{
    scr_fill(0, LAYOUT_URL_ROW, PAGE_COLS, 1, ' ', ATTR_NORMAL);
    scr_putsn(1, LAYOUT_URL_ROW, url, PAGE_COLS - 2, ATTR_INPUT);

    if (editing) {
        scr_cursor_show();
        scr_cursor_pos(1 + cursor_pos, LAYOUT_URL_ROW);
    } else {
        scr_cursor_hide();
    }
}

void render_statusbar(page_buffer_t *page, const char *status_msg)
{
    char buf[81];
    int pos, total, lnk;

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
    lnk = page->link_count;
    {
        char lbuf[20];
        char tmp[6];
        int i = 0, t = 0, v = lnk;
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
        scr_puts(46, LAYOUT_STATUS_ROW, "F5:Reload Tab:Link Esc:Quit", ATTR_STATUS);
    }
}

void render_chrome(void)
{
    scr_hline(0, LAYOUT_SEP1_ROW, PAGE_COLS, (char)BOX_H, ATTR_BORDER);
    scr_hline(0, LAYOUT_SEP2_ROW, PAGE_COLS, (char)BOX_H, ATTR_BORDER);
}

void render_all(page_buffer_t *page, const char *url,
                int url_editing, int url_cursor, const char *status_msg)
{
    render_titlebar(page->title);
    render_urlbar(url, url_editing, url_cursor);
    render_chrome();
    render_page(page);
    render_statusbar(page, status_msg);
}
