/*
 * stub_pages.c - Hardcoded test pages for DOSBox-X testing
 *
 * Three pages: about:home, about:test, about:help
 * Any other URL returns a "not found" error page.
 */

#include "stub_pages.h"
#include "render.h"
#include "screen.h"
#include <string.h>

/* Helper: write a string into page cells at (row, col) */
static void put_str(page_buffer_t *p, int row, int col,
                    const char *s, unsigned char attr, unsigned char type)
{
    while (*s && col < PAGE_COLS) {
        page_set_cell(p, row, col, *s, attr, type, 0);
        s++;
        col++;
    }
}

/* Helper: write a link string and register it */
static void put_link(page_buffer_t *p, int row, int col,
                     const char *text, const char *url)
{
    int sc = col;
    unsigned short lid;
    const char *t = text;

    /* If link table is full, render as plain text instead of ghost link */
    if (p->link_count >= MAX_LINKS) {
        put_str(p, row, col, text, ATTR_NORMAL, CELL_TEXT);
        return;
    }

    lid = (unsigned short)p->link_count;

    page_set_cell(p, row, col, '[', ATTR_LINK, CELL_LINK, lid);
    col++;

    while (*t && col < PAGE_COLS - 1) {
        page_set_cell(p, row, col, *t, ATTR_LINK, CELL_LINK, lid);
        t++;
        col++;
    }

    page_set_cell(p, row, col, ']', ATTR_LINK, CELL_LINK, lid);
    col++;

    page_add_link(p, url, row, sc, row, col - 1);
}

/* Helper: horizontal rule */
static void put_hrule(page_buffer_t *p, int row)
{
    int c;
    for (c = 0; c < PAGE_COLS; c++)
        page_set_cell(p, row, c, (char)0xC4, ATTR_HRULE, CELL_TEXT, 0);
}

/* Helper: heading with bullet marker */
static void put_heading(page_buffer_t *p, int row, const char *text)
{
    page_set_cell(p, row, 0, (char)0xFE, ATTR_BORDER, CELL_HEADING, 0);
    page_set_cell(p, row, 1, ' ', ATTR_HEADING, CELL_HEADING, 0);
    put_str(p, row, 2, text, ATTR_HEADING, CELL_HEADING);
}

/* ===== Page 1: about:home ===== */
static void build_home(page_buffer_t *p)
{
    int r = 1;

    strcpy(p->title, "Cathode - Start Page");
    strcpy(p->url, "about:home");

    /* Block-character title */
    put_str(p, r, 4,
        "\xDB\xDB\xDB \xDB\xDB\xDB \xDB\xDB\xDB\xDB \xDB  \xDB \xDB\xDB\xDB  \xDB\xDB\xDB  \xDB\xDB\xDB",
        ATTR_BORDER, CELL_TEXT);
    r++;
    put_str(p, r, 4,
        "\xDB    \xDB  \xDB  \xDB\xDB\xDB\xDB \xDB  \xDB \xDB  \xDB \xDB  \xDB \xDB",
        ATTR_BORDER, CELL_TEXT);
    r++;
    put_str(p, r, 4,
        "\xDB\xDB\xDB \xDB  \xDB  \xDB  \xDB  \xDB\xDB\xDB  \xDB\xDB\xDB  \xDB\xDB\xDB",
        ATTR_BORDER, CELL_TEXT);
    r += 2;

    put_str(p, r, 4, "Text-mode web browser for DOS", ATTR_HIGHLIGHT, CELL_TEXT);
    r++;
    put_str(p, r, 4, "Powered by NetISA", ATTR_DIM, CELL_TEXT);
    r += 2;

    put_hrule(p, r);
    r += 2;

    put_str(p, r, 4, "Quick Links:", ATTR_HEADER, CELL_TEXT);
    r += 2;

    page_set_cell(p, r, 4, (char)0xFE, ATTR_BULLET, CELL_TEXT, 0);
    put_link(p, r, 6, "Keyboard Shortcuts", "about:help");
    r++;

    page_set_cell(p, r, 4, (char)0xFE, ATTR_BULLET, CELL_TEXT, 0);
    put_link(p, r, 6, "Feature Test Page", "about:test");
    r++;

    page_set_cell(p, r, 4, (char)0xFE, ATTR_BULLET, CELL_TEXT, 0);
    put_link(p, r, 6, "barelybooting.com", "https://barelybooting.com");
    r += 2;

    put_hrule(p, r);
    r += 2;

    put_str(p, r, 4,
        "Enter a URL in the address bar (F6) or select a link (Tab).",
        ATTR_DIM, CELL_TEXT);
    r++;

    p->total_rows = r + 1;
}

/* ===== Page 2: about:test ===== */
static void build_test(page_buffer_t *p)
{
    int r = 0;

    strcpy(p->title, "Feature Test Page");
    strcpy(p->url, "about:test");

    put_heading(p, r, "Test Page");
    r += 2;

    put_heading(p, r, "Text Styles");
    r++;
    put_str(p, r, 2, "Normal text in the default style.", ATTR_NORMAL, CELL_TEXT);
    r++;
    put_str(p, r, 2, "Bold text stands out for emphasis.", ATTR_BOLD, CELL_BOLD);
    r++;
    put_str(p, r, 2, "Error messages appear in red.", ATTR_ERROR, CELL_TEXT);
    r++;
    put_str(p, r, 2, "Dim text is used for secondary info.", ATTR_DIM, CELL_TEXT);
    r += 2;

    put_heading(p, r, "Links");
    r++;
    put_str(p, r, 2, "Click or Tab to these links:", ATTR_NORMAL, CELL_TEXT);
    r++;
    put_str(p, r, 4, "1. ", ATTR_NORMAL, CELL_TEXT);
    put_link(p, r, 7, "Back to Start Page", "about:home");
    r++;
    put_str(p, r, 4, "2. ", ATTR_NORMAL, CELL_TEXT);
    put_link(p, r, 7, "Help & Shortcuts", "about:help");
    r++;
    put_str(p, r, 4, "3. ", ATTR_NORMAL, CELL_TEXT);
    put_link(p, r, 7, "NetISA on GitHub",
             "https://github.com/tonyuatkins-afk/NetISA");
    r += 2;

    put_hrule(p, r);
    r += 2;

    put_heading(p, r, "Table Example");
    r++;
    {
        int c;
        /* Top border */
        page_set_cell(p, r, 2, (char)0xDA, ATTR_TABLE, CELL_TEXT, 0);
        for (c = 3; c < 22; c++)
            page_set_cell(p, r, c, (char)0xC4, ATTR_TABLE, CELL_TEXT, 0);
        page_set_cell(p, r, 22, (char)0xC2, ATTR_TABLE, CELL_TEXT, 0);
        for (c = 23; c < 42; c++)
            page_set_cell(p, r, c, (char)0xC4, ATTR_TABLE, CELL_TEXT, 0);
        page_set_cell(p, r, 42, (char)0xBF, ATTR_TABLE, CELL_TEXT, 0);
        r++;

        /* Header row */
        page_set_cell(p, r, 2, (char)0xB3, ATTR_TABLE, CELL_TEXT, 0);
        put_str(p, r, 3, " Feature           ", ATTR_HIGHLIGHT, CELL_TEXT);
        page_set_cell(p, r, 22, (char)0xB3, ATTR_TABLE, CELL_TEXT, 0);
        put_str(p, r, 23, " Status             ", ATTR_HIGHLIGHT, CELL_TEXT);
        page_set_cell(p, r, 42, (char)0xB3, ATTR_TABLE, CELL_TEXT, 0);
        r++;

        /* Separator */
        page_set_cell(p, r, 2, (char)0xC3, ATTR_TABLE, CELL_TEXT, 0);
        for (c = 3; c < 22; c++)
            page_set_cell(p, r, c, (char)0xC4, ATTR_TABLE, CELL_TEXT, 0);
        page_set_cell(p, r, 22, (char)0xC5, ATTR_TABLE, CELL_TEXT, 0);
        for (c = 23; c < 42; c++)
            page_set_cell(p, r, c, (char)0xC4, ATTR_TABLE, CELL_TEXT, 0);
        page_set_cell(p, r, 42, (char)0xB4, ATTR_TABLE, CELL_TEXT, 0);
        r++;

        /* Data rows */
        page_set_cell(p, r, 2, (char)0xB3, ATTR_TABLE, CELL_TEXT, 0);
        put_str(p, r, 3, " Scrolling         ", ATTR_NORMAL, CELL_TEXT);
        page_set_cell(p, r, 22, (char)0xB3, ATTR_TABLE, CELL_TEXT, 0);
        put_str(p, r, 23, " Working            ", ATTR_SELECTED, CELL_TEXT);
        page_set_cell(p, r, 42, (char)0xB3, ATTR_TABLE, CELL_TEXT, 0);
        r++;

        page_set_cell(p, r, 2, (char)0xB3, ATTR_TABLE, CELL_TEXT, 0);
        put_str(p, r, 3, " Link Navigation   ", ATTR_NORMAL, CELL_TEXT);
        page_set_cell(p, r, 22, (char)0xB3, ATTR_TABLE, CELL_TEXT, 0);
        put_str(p, r, 23, " Working            ", ATTR_SELECTED, CELL_TEXT);
        page_set_cell(p, r, 42, (char)0xB3, ATTR_TABLE, CELL_TEXT, 0);
        r++;

        page_set_cell(p, r, 2, (char)0xB3, ATTR_TABLE, CELL_TEXT, 0);
        put_str(p, r, 3, " URL Bar           ", ATTR_NORMAL, CELL_TEXT);
        page_set_cell(p, r, 22, (char)0xB3, ATTR_TABLE, CELL_TEXT, 0);
        put_str(p, r, 23, " Working            ", ATTR_SELECTED, CELL_TEXT);
        page_set_cell(p, r, 42, (char)0xB3, ATTR_TABLE, CELL_TEXT, 0);
        r++;

        page_set_cell(p, r, 2, (char)0xB3, ATTR_TABLE, CELL_TEXT, 0);
        put_str(p, r, 3, " HTTPS Fetch       ", ATTR_NORMAL, CELL_TEXT);
        page_set_cell(p, r, 22, (char)0xB3, ATTR_TABLE, CELL_TEXT, 0);
        put_str(p, r, 23, " Stub only          ", ATTR_INPUT, CELL_TEXT);
        page_set_cell(p, r, 42, (char)0xB3, ATTR_TABLE, CELL_TEXT, 0);
        r++;

        /* Bottom border */
        page_set_cell(p, r, 2, (char)0xC0, ATTR_TABLE, CELL_TEXT, 0);
        for (c = 3; c < 22; c++)
            page_set_cell(p, r, c, (char)0xC4, ATTR_TABLE, CELL_TEXT, 0);
        page_set_cell(p, r, 22, (char)0xC1, ATTR_TABLE, CELL_TEXT, 0);
        for (c = 23; c < 42; c++)
            page_set_cell(p, r, c, (char)0xC4, ATTR_TABLE, CELL_TEXT, 0);
        page_set_cell(p, r, 42, (char)0xD9, ATTR_TABLE, CELL_TEXT, 0);
        r++;
    }
    r++;

    put_heading(p, r, "Bulleted List");
    r++;
    page_set_cell(p, r, 4, (char)0x07, ATTR_BULLET, CELL_TEXT, 0);
    put_str(p, r, 6, "First item in the list", ATTR_NORMAL, CELL_TEXT);
    r++;
    page_set_cell(p, r, 4, (char)0x07, ATTR_BULLET, CELL_TEXT, 0);
    put_str(p, r, 6, "Second item with more detail", ATTR_NORMAL, CELL_TEXT);
    r++;
    page_set_cell(p, r, 4, (char)0x07, ATTR_BULLET, CELL_TEXT, 0);
    put_str(p, r, 6, "Third item", ATTR_NORMAL, CELL_TEXT);
    r++;
    page_set_cell(p, r, 4, (char)0x07, ATTR_BULLET, CELL_TEXT, 0);
    put_str(p, r, 6, "Fourth item for scrolling", ATTR_NORMAL, CELL_TEXT);
    r += 2;

    put_heading(p, r, "Scrolling Test");
    r++;
    put_str(p, r, 2,
        "The following content extends below the visible viewport.",
        ATTR_NORMAL, CELL_TEXT);
    r++;
    put_str(p, r, 2, "Use PgDn, Down Arrow, or End to scroll.",
            ATTR_NORMAL, CELL_TEXT);
    r += 2;

    {
        int i;
        for (i = 1; i <= 10; i++) {
            char buf[40];
            buf[0] = 'L'; buf[1] = 'i'; buf[2] = 'n'; buf[3] = 'e';
            buf[4] = ' ';
            buf[5] = (char)('0' + (i / 10));
            buf[6] = (char)('0' + (i % 10));
            buf[7] = ':'; buf[8] = ' ';
            buf[9] = 'S'; buf[10] = 'c'; buf[11] = 'r'; buf[12] = 'o';
            buf[13] = 'l'; buf[14] = 'l'; buf[15] = ' ';
            buf[16] = 't'; buf[17] = 'e'; buf[18] = 's'; buf[19] = 't';
            buf[20] = '\0';
            put_str(p, r, 4, buf, ATTR_DIM, CELL_TEXT);
            r++;
        }
    }

    r++;
    put_str(p, r, 2, "End of test page.", ATTR_DIM, CELL_TEXT);
    r++;

    p->total_rows = r + 1;
}

/* ===== Page 3: about:help ===== */
static void build_help(page_buffer_t *p)
{
    int r = 0;

    strcpy(p->title, "Keyboard Shortcuts");
    strcpy(p->url, "about:help");

    put_heading(p, r, "Cathode Keyboard Shortcuts");
    r += 2;

    put_heading(p, r, "Navigation");
    r++;
    put_str(p, r, 4, "Up / Down     ", ATTR_HIGHLIGHT, CELL_TEXT);
    put_str(p, r, 20, "Scroll one line", ATTR_NORMAL, CELL_TEXT);
    r++;
    put_str(p, r, 4, "PgUp / PgDn   ", ATTR_HIGHLIGHT, CELL_TEXT);
    put_str(p, r, 20, "Scroll one page (20 lines)", ATTR_NORMAL, CELL_TEXT);
    r++;
    put_str(p, r, 4, "Home / End    ", ATTR_HIGHLIGHT, CELL_TEXT);
    put_str(p, r, 20, "Jump to top / bottom", ATTR_NORMAL, CELL_TEXT);
    r += 2;

    put_heading(p, r, "Links");
    r++;
    put_str(p, r, 4, "Tab           ", ATTR_HIGHLIGHT, CELL_TEXT);
    put_str(p, r, 20, "Select next link", ATTR_NORMAL, CELL_TEXT);
    r++;
    put_str(p, r, 4, "Shift+Tab     ", ATTR_HIGHLIGHT, CELL_TEXT);
    put_str(p, r, 20, "Select previous link", ATTR_NORMAL, CELL_TEXT);
    r++;
    put_str(p, r, 4, "Enter         ", ATTR_HIGHLIGHT, CELL_TEXT);
    put_str(p, r, 20, "Follow selected link", ATTR_NORMAL, CELL_TEXT);
    r++;
    put_str(p, r, 4, "Backspace     ", ATTR_HIGHLIGHT, CELL_TEXT);
    put_str(p, r, 20, "Go back to previous page", ATTR_NORMAL, CELL_TEXT);
    r += 2;

    put_heading(p, r, "Address Bar");
    r++;
    put_str(p, r, 4, "F6 / Ctrl+L   ", ATTR_HIGHLIGHT, CELL_TEXT);
    put_str(p, r, 20, "Focus address bar", ATTR_NORMAL, CELL_TEXT);
    r++;
    put_str(p, r, 4, "Enter         ", ATTR_HIGHLIGHT, CELL_TEXT);
    put_str(p, r, 20, "Navigate to typed URL", ATTR_NORMAL, CELL_TEXT);
    r++;
    put_str(p, r, 4, "Escape        ", ATTR_HIGHLIGHT, CELL_TEXT);
    put_str(p, r, 20, "Cancel URL editing", ATTR_NORMAL, CELL_TEXT);
    r += 2;

    put_heading(p, r, "Other");
    r++;
    put_str(p, r, 4, "F5            ", ATTR_HIGHLIGHT, CELL_TEXT);
    put_str(p, r, 20, "Reload current page", ATTR_NORMAL, CELL_TEXT);
    r++;
    put_str(p, r, 4, "Escape        ", ATTR_HIGHLIGHT, CELL_TEXT);
    put_str(p, r, 20, "Quit Cathode", ATTR_NORMAL, CELL_TEXT);
    r += 2;

    put_hrule(p, r);
    r += 2;

    put_link(p, r, 4, "Back to Start Page", "about:home");
    r++;

    p->total_rows = r + 1;
}

/* ===== Error page ===== */
static void build_error(page_buffer_t *p, const char *url)
{
    int r = 2;

    strcpy(p->title, "Page Not Found");
    strncpy(p->url, url, 255);
    p->url[255] = '\0';

    put_heading(p, r, "Page Not Found");
    r += 2;

    put_str(p, r, 4, "Cathode could not load:", ATTR_NORMAL, CELL_TEXT);
    r++;
    put_str(p, r, 4, url, ATTR_INPUT, CELL_TEXT);
    r += 2;

    put_str(p, r, 4, "In this stub build, only about: pages are available.",
            ATTR_DIM, CELL_TEXT);
    r++;
    put_str(p, r, 4, "HTTPS page fetching requires the NetISA card.",
            ATTR_DIM, CELL_TEXT);
    r += 2;

    put_link(p, r, 4, "Go to Start Page", "about:home");
    r++;

    p->total_rows = r + 1;
}

int stub_fetch_page(const char *url, page_buffer_t *page)
{
    page_clear(page);

    if (strcmp(url, "about:home") == 0 || strcmp(url, "") == 0) {
        build_home(page);
        return 0;
    }
    if (strcmp(url, "about:test") == 0) {
        build_test(page);
        return 0;
    }
    if (strcmp(url, "about:help") == 0) {
        build_help(page);
        return 0;
    }

    build_error(page, url);
    return -1;
}
