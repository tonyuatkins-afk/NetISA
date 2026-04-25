/*
 * page.c - Page buffer management for Cathode browser
 *
 * Cells, meta, and linkmap are allocated separately on the far heap
 * to stay within 64KB per allocation in small model.
 */

#include "page.h"
#include "screen.h"
#include <malloc.h>
#include <string.h>
#include <stdlib.h>

#define TOTAL_CELLS ((unsigned int)PAGE_MAX_ROWS * PAGE_COLS)

page_buffer_t *page_alloc(void)
{
    page_buffer_t *p;

    p = (page_buffer_t *)malloc(sizeof(page_buffer_t));
    if (!p) return (page_buffer_t *)0;
    memset(p, 0, sizeof(page_buffer_t));

    /* Allocate far arrays separately */
    p->cells = (page_cell_t far *)_fmalloc(TOTAL_CELLS * sizeof(page_cell_t));
    p->meta = (unsigned char far *)_fmalloc(TOTAL_CELLS);
    p->linkmap = (unsigned short far *)_fmalloc(TOTAL_CELLS * sizeof(unsigned short));

    if (!p->cells || !p->meta || !p->linkmap) {
        page_free(p);
        return (page_buffer_t *)0;
    }

    /* Non-critical: form fields. If alloc fails, forms are disabled. */
    p->fields = (form_field_t far *)_fmalloc(
        (unsigned long)MAX_FORM_FIELDS * sizeof(form_field_t));
    if (!p->fields)
        p->field_count = -1;   /* sentinel: forms disabled */
    else
        p->field_count = 0;

    page_clear(p);
    return p;
}

void page_free(page_buffer_t *page)
{
    if (!page) return;
    if (page->cells)   _ffree(page->cells);
    if (page->meta)    _ffree(page->meta);
    if (page->linkmap) _ffree(page->linkmap);
    if (page->fields)  _ffree(page->fields);
    free(page);
}

void page_clear(page_buffer_t *page)
{
    page->total_rows = 0;
    page->scroll_pos = 0;
    page->title[0] = '\0';
    page->url[0] = '\0';
    page->link_count = 0;
    page->selected_link = -1;
    if (page->field_count != -1)
        page->field_count = 0;
    page->focused_field = -1;

    /* Zero meta and linkmap with _fmemset (much faster on 8088) */
    _fmemset(page->meta, CELL_TEXT, TOTAL_CELLS);
    _fmemset(page->linkmap, 0, TOTAL_CELLS * sizeof(unsigned short));

    /* Zero all cells with _fmemset (much faster than 16K far writes on 8088).
     * ch=0 renders as NUL/black — invisible, same as empty. The renderer
     * overwrites visible cells anyway, so zeroed unused cells are fine. */
    _fmemset(page->cells, 0, TOTAL_CELLS * sizeof(page_cell_t));
}

void page_set_cell(page_buffer_t *page, int row, int col,
                   char ch, unsigned char attr, unsigned char type,
                   unsigned short link_id)
{
    unsigned int idx;
    if (row < 0 || row >= PAGE_MAX_ROWS || col < 0 || col >= PAGE_COLS)
        return;

    idx = PAGE_IDX(row, col);
    page->cells[idx].ch = ch;
    page->cells[idx].attr = attr;
    page->meta[idx] = type;
    page->linkmap[idx] = link_id;

    if (row >= page->total_rows)
        page->total_rows = row + 1;
}

page_cell_t page_get_cell(page_buffer_t *page, int row, int col)
{
    page_cell_t c;
    unsigned int idx;
    if (row < 0 || row >= PAGE_MAX_ROWS || col < 0 || col >= PAGE_COLS) {
        c.ch = ' ';
        c.attr = ATTR_NORMAL;
        return c;
    }
    idx = PAGE_IDX(row, col);
    c.ch = page->cells[idx].ch;
    c.attr = page->cells[idx].attr;
    return c;
}

unsigned char page_get_meta(page_buffer_t *page, int row, int col)
{
    if (row < 0 || row >= PAGE_MAX_ROWS || col < 0 || col >= PAGE_COLS)
        return CELL_TEXT;
    return page->meta[PAGE_IDX(row, col)];
}

unsigned short page_get_linkid(page_buffer_t *page, int row, int col)
{
    if (row < 0 || row >= PAGE_MAX_ROWS || col < 0 || col >= PAGE_COLS)
        return 0;
    return page->linkmap[PAGE_IDX(row, col)];
}

void page_add_link(page_buffer_t *page, const char *url,
                   int sr, int sc, int er, int ec)
{
    int idx;
    if (page->link_count >= MAX_LINKS)
        return;
    idx = page->link_count;
    strncpy(page->links[idx].url, url, LINK_URL_MAX - 1);
    page->links[idx].url[LINK_URL_MAX - 1] = '\0';
    page->links[idx].start_row = sr;
    page->links[idx].start_col = sc;
    page->links[idx].end_row = er;
    page->links[idx].end_col = ec;
    page->link_count++;
}
