/*
 * main.c - Claude for DOS entry point
 *
 * Usage: CLAUDE.EXE
 */

#include "claude.h"
#include <dos.h>

int main(void)
{
    static cl_state_t state;

    scr_init();
    cl_init(&state);

    if (!state.messages) {
        /* Far heap allocation failed */
        scr_clear(SCR_ATTR(SCR_LIGHTGRAY, SCR_BLACK));
        scr_puts(0, 0, "ERROR: Not enough memory for Claude.", SCR_ATTR(SCR_LIGHTRED, SCR_BLACK));
        scr_puts(0, 1, "Need ~15KB free far heap. Free TSRs or drivers and retry.", SCR_ATTR(SCR_LIGHTGRAY, SCR_BLACK));
        scr_puts(0, 3, "Press any key to exit.", SCR_ATTR(SCR_DARKGRAY, SCR_BLACK));
        scr_getkey();
        scr_shutdown();
        return 1;
    }

    while (state.running) {
        cl_poll(&state);

        cl_render_titlebar(&state);
        cl_render_chat(&state);
        cl_render_compose(&state);
        cl_render_statusbar(&state);

        if (scr_kbhit()) {
            int key = scr_getkey();
            cl_handle_key(&state, key);
        } else {
            /* DOS idle interrupt: yields CPU, lets TSRs run */
            _asm { int 28h }
        }
    }

    cl_shutdown(&state);
    scr_shutdown();
    return 0;
}
