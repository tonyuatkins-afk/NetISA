/*
 * main.c - Cathode v0.2 text-mode document browser entry point
 *
 * Usage: CATHODE [url]
 *   If url provided, navigate to it.
 *   Otherwise, show about:home start page.
 */

#include "screen.h"
#include "browser.h"
#include "render.h"
#include "input.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
    static browser_state_t b;
    const char *start_url;

    /* Determine start URL */
    if (argc > 1)
        start_url = argv[1];
    else
        start_url = "about:home";

    /* Initialize browser state */
    browser_init(&b);
    if (!b.current_page) {
        printf("Error: not enough memory for page buffer.\n");
        return 1;
    }

    /* Initialize screen */
    scr_init();

    /* Initialize screen */
    scr_init();

    /* Navigate to start page */
    browser_navigate(&b, start_url);

    /* Initial render */
    render_all(b.current_page, b.urlbar.buf,
               b.urlbar.editing, b.urlbar.cursor,
               b.status_msg, &b.search);

    /* Fade in from black */
    scr_fade_in(12, 40);

    /* Main event loop — non-blocking with DOS idle yield */
    while (b.running) {
        int dirty = 0;

        /* Poll keyboard */
        if (scr_kbhit()) {
            int key = scr_getkey();
            input_handle_key(&b, key);
            dirty = 1;
        }

        /* Render when state changed */
        if (dirty && b.running) {
            render_all(b.current_page, b.urlbar.buf,
                       b.urlbar.editing, b.urlbar.cursor,
                       b.status_msg, &b.search);
        }

        /* Yield to DOS when idle (lets TSRs run) */
        if (!dirty) {
            _asm { int 28h }
        }
    }

    /* Fade out to black before exit */
    scr_fade_out(12, 30);

    /* Cleanup */
    browser_shutdown(&b);
    scr_shutdown();

    return 0;
}
