/*
 * main.c - Discord client for DOS entry point
 *
 * Usage: DISCORD.EXE
 */

#include "discord.h"
#include <stdio.h>
#include <dos.h>

int main(void)
{
    static dc_state_t state;

    scr_init();
    dc_init(&state);

    while (state.running) {
        dc_poll_messages(&state);

        if (state.dirty) {
            dc_render_titlebar(&state);
            dc_render_channels(&state);
            dc_render_messages(&state);
            dc_render_compose(&state);
            dc_render_statusbar(&state);
            state.dirty = 0;
        }

        if (scr_kbhit()) {
            int key = scr_getkey();
            dc_handle_key(&state, key);
            state.dirty = 1;
        } else {
            /* DOS idle interrupt: yields CPU, lets TSRs run */
            _asm { int 28h }
        }
    }

    scr_shutdown();
    return 0;
}
