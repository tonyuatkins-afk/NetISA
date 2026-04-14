/*
 * stub_pages.c - Stub HTML pages for Cathode browser
 *
 * Provides HTML strings for about: URLs. The procedural CP437 block-art
 * CATHODE logo is preserved for about:home (cannot be expressed in HTML).
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

/* ===== Procedural CATHODE logo (CP437 block art) ===== */

void build_home_logo(page_buffer_t *page)
{
    int r = 1;

    /* BBS-style block art: CATHODE
     * CP437: \xDB=full, \xDC=lower half, \xDF=upper half
     * 5 rows tall, centered at col 13.
     * Color gradient: green -> bright green -> white -> bright green -> green
     */
    put_str(page, r++, 13,
        "\xDC\xDB\xDB\xDB\xDB\xDC" "  " "\xDC\xDB\xDB\xDB\xDB\xDC" "  "
        "\xDB\xDB\xDB\xDB\xDB\xDB" "  " "\xDB\xDB  \xDB\xDB" "  "
        "\xDC\xDB\xDB\xDB\xDB\xDC" "  " "\xDB\xDB\xDB\xDB\xDB\xDC" "  "
        "\xDB\xDB\xDB\xDB\xDB\xDB",
        ATTR_BORDER, CELL_TEXT);
    put_str(page, r++, 13,
        "\xDB\xDB    " "  " "\xDB\xDB  \xDB\xDB" "  "
        "  \xDB\xDB  " "  " "\xDB\xDB  \xDB\xDB" "  "
        "\xDB\xDB  \xDB\xDB" "  " "\xDB\xDB  \xDB\xDB" "  "
        "\xDB\xDB    ",
        ATTR_SELECTED, CELL_TEXT);
    put_str(page, r++, 13,
        "\xDB\xDB    " "  " "\xDB\xDB\xDB\xDB\xDB\xDB" "  "
        "  \xDB\xDB  " "  " "\xDB\xDB\xDB\xDB\xDB\xDB" "  "
        "\xDB\xDB  \xDB\xDB" "  " "\xDB\xDB  \xDB\xDB" "  "
        "\xDB\xDB\xDB\xDB  ",
        ATTR_HIGHLIGHT, CELL_TEXT);
    put_str(page, r++, 13,
        "\xDB\xDB    " "  " "\xDB\xDB  \xDB\xDB" "  "
        "  \xDB\xDB  " "  " "\xDB\xDB  \xDB\xDB" "  "
        "\xDB\xDB  \xDB\xDB" "  " "\xDB\xDB  \xDB\xDB" "  "
        "\xDB\xDB    ",
        ATTR_SELECTED, CELL_TEXT);
    put_str(page, r++, 13,
        "\xDF\xDB\xDB\xDB\xDB\xDF" "  " "\xDB\xDB  \xDB\xDB" "  "
        "  \xDB\xDB  " "  " "\xDB\xDB  \xDB\xDB" "  "
        "\xDF\xDB\xDB\xDB\xDB\xDF" "  " "\xDB\xDB\xDB\xDB\xDB\xDF" "  "
        "\xDB\xDB\xDB\xDB\xDB\xDB",
        ATTR_BORDER, CELL_TEXT);

    page->total_rows = 7;  /* logo occupies rows 1-6 */
}

/* ===== HTML string pages ===== */

static const char far home_html[] =
    "<html><head><title>Cathode - Start Page</title></head>"
    "<body>"
    "<p>Text-mode document browser for DOS</p>"
    "<p>Powered by NetISA</p>"
    "<hr>"
    "<h2>Quick Links</h2>"
    "<ul>"
    "<li><a href=\"about:help\">Keyboard Shortcuts</a></li>"
    "<li><a href=\"about:test\">Feature Test Page</a></li>"
    "<li><a href=\"about:bb\">barelybooting.com</a></li>"
    "</ul>"
    "<h2>Demo Sites (cached)</h2>"
    "<ul>"
    "<li><a href=\"about:npr\">NPR - National Public Radio</a></li>"
    "<li><a href=\"about:bsd\">OpenBSD man page: ls</a></li>"
    "<li><a href=\"about:example\">Example Domain</a></li>"
    "</ul>"
    "<hr>"
    "<p>Enter a URL in the address bar (F6) or select a link (Tab).</p>"
    "</body></html>";

static const char far test_html[] =
    "<html><head><title>Feature Test Page</title></head>"
    "<body>"
    "<h1>Test Page</h1>"
    "<h2>Text Styles</h2>"
    "<p>Normal text in the default style. "
    "<b>Bold text stands out.</b> "
    "<i>Italic text is dimmed.</i> "
    "<code>Code text is green.</code></p>"
    "<h2>Links</h2>"
    "<p><a href=\"about:home\">Back to Start Page</a> | "
    "<a href=\"about:help\">Help &amp; Shortcuts</a> | "
    "<a href=\"https://github.com/tonyuatkins-afk/NetISA\">NetISA on GitHub</a></p>"
    "<hr>"
    "<h2>Bulleted List</h2>"
    "<ul>"
    "<li>First item in the list</li>"
    "<li>Second item with more detail</li>"
    "<li>Third item</li>"
    "<li>Fourth item for scrolling</li>"
    "</ul>"
    "<h2>Ordered List</h2>"
    "<ol>"
    "<li>Alpha</li>"
    "<li>Bravo</li>"
    "<li>Charlie</li>"
    "</ol>"
    "<h2>Preformatted</h2>"
    "<pre>"
    "CATHODE v0.2 - Text-mode document browser\n"
    "Copyright (c) 2026 Tony Atkins\n"
    "MIT License\n"
    "</pre>"
    "<h2>Blockquote</h2>"
    "<blockquote>"
    "<p>The best way to predict the future is to invent it.</p>"
    "<p>&mdash; Alan Kay</p>"
    "</blockquote>"
    "<h2>Table (Phase 3)</h2>"
    "<table>"
    "<tr><th>Feature</th><th>Status</th></tr>"
    "<tr><td>HTML Parsing</td><td>Working</td></tr>"
    "<tr><td>Link Navigation</td><td>Working</td></tr>"
    "<tr><td>URL Bar</td><td>Working</td></tr>"
    "<tr><td>Scroll Bar</td><td>Working</td></tr>"
    "<tr><td>Find on Page</td><td>Working</td></tr>"
    "<tr><td>Bookmarks</td><td>Working</td></tr>"
    "<tr><td>Mouse Support</td><td>Phase 2</td></tr>"
    "<tr><td>Tabs</td><td>Phase 2b</td></tr>"
    "<tr><td>Tables</td><td>Phase 3</td></tr>"
    "</table>"
    "<h2>Entities</h2>"
    "<p>&amp; &lt; &gt; &quot; &copy; &mdash; &nbsp; &#169;</p>"
    "<h2>Scrolling Test</h2>"
    "<p>The following content extends below the visible viewport. "
    "Use PgDn, Down Arrow, or End to scroll.</p>"
    "<p>Line 01: Scroll test</p>"
    "<p>Line 02: Scroll test</p>"
    "<p>Line 03: Scroll test</p>"
    "<p>Line 04: Scroll test</p>"
    "<p>Line 05: Scroll test</p>"
    "<p>Line 06: Scroll test</p>"
    "<p>Line 07: Scroll test</p>"
    "<p>Line 08: Scroll test</p>"
    "<p>Line 09: Scroll test</p>"
    "<p>Line 10: Scroll test</p>"
    "<hr>"
    "<p>End of test page.</p>"
    "</body></html>";

static const char far help_html[] =
    "<html><head><title>Keyboard Shortcuts</title></head>"
    "<body>"
    "<h1>Cathode Keyboard Shortcuts</h1>"
    "<h2>Navigation</h2>"
    "<p><b>Up / Down</b> - Scroll one line</p>"
    "<p><b>PgUp / PgDn</b> - Scroll one page (20 lines)</p>"
    "<p><b>Home / End</b> - Jump to top / bottom</p>"
    "<h2>Links</h2>"
    "<p><b>Tab</b> - Select next link</p>"
    "<p><b>Shift+Tab</b> - Select previous link</p>"
    "<p><b>Enter</b> - Follow selected link</p>"
    "<p><b>Backspace</b> - Go back to previous page</p>"
    "<h2>Address Bar</h2>"
    "<p><b>F6 / Ctrl+L</b> - Focus address bar</p>"
    "<p><b>Enter</b> - Navigate to typed URL</p>"
    "<p><b>Escape</b> - Cancel URL editing</p>"
    "<h2>Search</h2>"
    "<p><b>Ctrl+F</b> - Find on page</p>"
    "<p><b>N</b> - Find next match</p>"
    "<p><b>Shift+N</b> - Find previous match</p>"
    "<h2>Bookmarks</h2>"
    "<p><b>Ctrl+D</b> - Bookmark current page</p>"
    "<p><b>Ctrl+B</b> - View bookmarks</p>"
    "<h2>Other</h2>"
    "<p><b>F5</b> - Reload current page</p>"
    "<p><b>Escape</b> - Quit Cathode</p>"
    "<hr>"
    "<p><a href=\"about:home\">Back to Start Page</a></p>"
    "</body></html>";

static const char far error_html_prefix[] =
    "<html><head><title>Page Not Found</title></head>"
    "<body>"
    "<h1>Page Not Found</h1>"
    "<p>Cathode could not load this page.</p>"
    "<p>In this stub build, only <b>about:</b> pages are available. "
    "HTTPS page fetching requires the NetISA card.</p>"
    "<hr>"
    "<p><a href=\"about:home\">Go to Start Page</a></p>"
    "</body></html>";

const char far *stub_get_html(const char *url)
{
    if (strcmp(url, "about:home") == 0 || strcmp(url, "") == 0)
        return home_html;
    if (strcmp(url, "about:test") == 0)
        return test_html;
    if (strcmp(url, "about:help") == 0)
        return help_html;

    /* Unknown about: URL */
    return error_html_prefix;
}

/* Legacy API for backwards compatibility during migration */
int stub_fetch_page(const char *url, page_buffer_t *page)
{
    (void)url;
    (void)page;
    return -1;  /* No longer used — see stub_get_html */
}
