/*
 * test.c - Minimal test: parse home_html and print total_rows
 */

#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include "page.h"
#include "htmltok.h"
#include "stub_pages.h"
#include "screen.h"

static html_parser_t parser;

static void test_callback(const char far *chunk, int len, void *ud)
{
    html_parser_t *p = (html_parser_t *)ud;
    html_parse_chunk(p, chunk, len);
}

int main(void)
{
    page_buffer_t *page;
    const char far *html;
    unsigned int len;
    const char far *p;

    printf("Cathode parser test\n");

    page = page_alloc();
    if (!page) {
        printf("ERROR: page_alloc failed\n");
        return 1;
    }
    printf("page_alloc OK\n");

    page_clear(page);
    printf("page_clear OK, total_rows=%d\n", page->total_rows);

    html = stub_get_html("about:home");
    if (!html) {
        printf("ERROR: stub_get_html returned NULL\n");
        return 1;
    }

    /* Measure length */
    len = 0;
    p = html;
    while (p[len]) len++;
    printf("HTML length: %u bytes\n", len);

    /* Parse */
    html_init(&parser, page);
    printf("html_init OK, parser.row=%d col=%d\n", parser.row, parser.col);

    html_parse_chunk(&parser, html, (int)len);
    printf("html_parse_chunk OK\n");

    html_finish(&parser);
    printf("html_finish OK\n");

    printf("total_rows=%d\n", page->total_rows);

    /* Print first few rows of content */
    {
        int r, c;
        for (r = 0; r < 5 && r < page->total_rows; r++) {
            printf("Row %2d: ", r);
            for (c = 0; c < 60; c++) {
                page_cell_t cell = page_get_cell(page, r, c);
                if (cell.ch >= 0x20 && cell.ch < 0x7F)
                    putchar(cell.ch);
                else if (cell.ch == 0)
                    putchar('.');
                else
                    putchar('?');
            }
            printf("\n");
        }
    }

    page_free(page);
    printf("DONE\n");
    return 0;
}
