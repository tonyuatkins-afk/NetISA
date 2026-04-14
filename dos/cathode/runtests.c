/*
 * runtests.c - Automated HTML fixture test harness for Cathode browser
 *
 * Loads HTML fixtures from the HTML\ directory, parses each through
 * the Cathode HTML engine, and reports rendered output and stats.
 * Designed to run via DOSBox-X relay for automated testing.
 *
 * Build: wmake -f Makefile runtests
 * Run:   RUNTESTS.EXE
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "page.h"
#include "htmltok.h"
#include "stub_pages.h"

/* Test fixture definition */
typedef struct {
    const char *filename;   /* Path relative to exe (HTML\NAME.HTM) */
    const char *label;      /* Human-readable name */
} fixture_t;

static const fixture_t fixtures[] = {
    { "HTML\\EXAMPLE.HTM",  "example.com (minimal)"      },
    { "HTML\\UTF8.HTM",     "UTF-8 entities & chars"     },
    { "HTML\\LISTS.HTM",    "Lists (UL/OL/nested)"       },
    { "HTML\\MALFORM.HTM",  "Malformed HTML robustness"  },
    { "HTML\\NPR.HTM",      "text.npr.org (news)"        },
    { "HTML\\MOTH.HTM",     "motherfuckingwebsite.com"   },
    { "HTML\\BIGPAGE.HTM",  "Large page (truncation)"    },
    { "HTML\\CNN.HTM",      "lite.cnn.com (headlines)"   },
    { "HTML\\BSD.HTM",      "OpenBSD ls man page"        },
    { "HTML\\HN.HTM",       "Hacker News (tables)"       },
    { "HTML\\WIKI.HTM",     "Wikipedia Computer (large)" },
    { NULL, NULL }
};

static html_parser_t parser;

/*
 * Load a file into a far buffer. Returns bytes read, or -1 on error.
 * Caller must _ffree() the returned buffer.
 */
static long load_file(const char *path, char far **out_buf)
{
    FILE *fp;
    long fsize;
    char far *buf;
    long nread;

    fp = fopen(path, "rb");
    if (!fp) {
        *out_buf = NULL;
        return -1;
    }

    /* Get file size */
    fseek(fp, 0, SEEK_END);
    fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 64000L) {
        /* Cap at 64KB for safety in small model */
        if (fsize > 64000L)
            fsize = 64000L;
        else {
            fclose(fp);
            *out_buf = NULL;
            return -1;
        }
    }

    buf = (char far *)_fmalloc((unsigned int)fsize);
    if (!buf) {
        fclose(fp);
        *out_buf = NULL;
        return -1;
    }

    /* Read in chunks (fread goes through near buffer) */
    {
        char tmp[512];
        long total = 0;
        int n;
        while (total < fsize) {
            int want = 512;
            if (fsize - total < 512)
                want = (int)(fsize - total);
            n = (int)fread(tmp, 1, want, fp);
            if (n <= 0) break;
            _fmemcpy(buf + total, (char far *)tmp, n);
            total += n;
        }
        nread = total;
    }

    fclose(fp);
    *out_buf = buf;
    return nread;
}

/*
 * Print rendered page content (first N rows, 80 cols).
 * Shows the text as Cathode would display it.
 */
static void dump_page(page_buffer_t *page, int max_rows)
{
    int r, c;
    int rows = page->total_rows;
    if (rows > max_rows)
        rows = max_rows;

    for (r = 0; r < rows; r++) {
        /* Find last non-space, non-null char for trim */
        int last = -1;
        for (c = 0; c < PAGE_COLS; c++) {
            page_cell_t cell = page_get_cell(page, r, c);
            if (cell.ch != 0 && cell.ch != ' ')
                last = c;
        }

        /* Print the row up to last visible char */
        for (c = 0; c <= last; c++) {
            page_cell_t cell = page_get_cell(page, r, c);
            if (cell.ch >= 0x20 && cell.ch <= 0x7E)
                putchar(cell.ch);
            else if (cell.ch == 0)
                putchar(' ');
            else
                putchar('?');  /* Non-ASCII CP437 */
        }
        putchar('\n');
    }
}

/*
 * Run one test fixture.
 * Returns: 0 = pass, 1 = file not found, 2 = alloc fail, 3 = parse problem
 */
static int run_fixture(const fixture_t *fix)
{
    page_buffer_t *page;
    char far *html;
    long html_len;
    int i;

    printf("--- %s ---\n", fix->label);
    printf("File: %s\n", fix->filename);

    /* Load HTML */
    html_len = load_file(fix->filename, &html);
    if (html_len < 0) {
        printf("SKIP: cannot open file\n\n");
        return 1;
    }
    printf("Size: %ld bytes\n", html_len);

    /* Allocate page */
    page = page_alloc();
    if (!page) {
        printf("FAIL: page_alloc failed\n\n");
        _ffree(html);
        return 2;
    }
    page_clear(page);

    /* Parse in chunks (html_parse_chunk takes int len, max 32767 on DOS) */
    html_init(&parser, page);
    {
        long offset = 0;
        while (offset < html_len) {
            int chunk = (html_len - offset > 16384L)
                        ? 16384 : (int)(html_len - offset);
            html_parse_chunk(&parser, html + offset, chunk);
            offset += chunk;
        }
    }
    html_finish(&parser);

    /* Stats */
    printf("Rows: %d", page->total_rows);
    if (parser.truncated)
        printf(" (TRUNCATED at %d)", PAGE_MAX_ROWS);
    printf("\n");
    printf("Links: %d\n", page->link_count);

    /* Dump title if present */
    if (page->title[0])
        printf("Title: %s\n", page->title);

    /* Print first and last few rows */
    printf("\n=== First 10 rows ===\n");
    dump_page(page, 10);

    if (page->total_rows > 15) {
        int start = page->total_rows - 5;
        int r, c;
        if (start < 10) start = 10;
        printf("\n=== Last 5 rows (from row %d) ===\n", start);
        for (r = start; r < page->total_rows; r++) {
            int last = -1;
            for (c = 0; c < PAGE_COLS; c++) {
                page_cell_t cell = page_get_cell(page, r, c);
                if (cell.ch != 0 && cell.ch != ' ')
                    last = c;
            }
            for (c = 0; c <= last; c++) {
                page_cell_t cell = page_get_cell(page, r, c);
                if (cell.ch >= 0x20 && cell.ch <= 0x7E)
                    putchar(cell.ch);
                else if (cell.ch == 0)
                    putchar(' ');
                else
                    putchar('?');
            }
            putchar('\n');
        }
    }

    /* Print link table */
    if (page->link_count > 0) {
        int show = page->link_count;
        if (show > 5) show = 5;
        printf("\n=== Links (first %d of %d) ===\n", show, page->link_count);
        for (i = 0; i < show; i++) {
            printf("  [%d] %s (r%d,c%d)-(r%d,c%d)\n", i,
                   page->links[i].url,
                   page->links[i].start_row, page->links[i].start_col,
                   page->links[i].end_row, page->links[i].end_col);
        }
    }

    printf("\nRESULT: PASS\n\n");

    /* Cleanup */
    page_free(page);
    _ffree(html);
    return 0;
}

int main(void)
{
    int i;
    int pass = 0, fail = 0, skip = 0;

    printf("========================================\n");
    printf(" Cathode HTML Parser Test Suite\n");
    printf("========================================\n\n");

    /* First test: parse about:home (stub page, no file I/O) */
    {
        page_buffer_t *page;
        const char far *html;
        unsigned int len;

        printf("--- about:home (stub page) ---\n");
        page = page_alloc();
        if (!page) {
            printf("FAIL: page_alloc\n");
            fail++;
        } else {
            html = stub_get_html("about:home");
            if (!html) {
                printf("FAIL: stub_get_html returned NULL\n");
                fail++;
            } else {
                len = 0;
                { const char far *p = html; while (p[len]) len++; }
                page_clear(page);
                html_init(&parser, page);
                html_parse_chunk(&parser, html, (int)len);
                html_finish(&parser);
                printf("Rows: %d, Links: %d\n", page->total_rows,
                       page->link_count);
                printf("RESULT: PASS\n\n");
                pass++;
            }
            page_free(page);
        }
    }

    /* Run all file-based fixtures */
    for (i = 0; fixtures[i].filename != NULL; i++) {
        int rc = run_fixture(&fixtures[i]);
        if (rc == 0)
            pass++;
        else if (rc == 1)
            skip++;
        else
            fail++;
    }

    /* Summary */
    printf("========================================\n");
    printf(" SUMMARY: %d passed, %d failed, %d skipped\n",
           pass, fail, skip);
    printf("========================================\n");

    return fail;
}
