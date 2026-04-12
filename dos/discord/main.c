/*
 * main.c - Discord client for DOS entry point
 *
 * Usage: DISCORD.EXE
 */

#include "discord.h"
#include <stdio.h>

int main(void)
{
    static dc_state_t state;

    scr_init();
    dc_init(&state);
    dc_switch_channel(&state, 0);

    while (state.running) {
        dc_poll_messages(&state);

        dc_render_titlebar(&state);
        dc_render_channels(&state);
        dc_render_messages(&state);
        dc_render_compose(&state);
        dc_render_statusbar(&state);

        if (scr_kbhit()) {
            int key = scr_getkey();
            dc_handle_key(&state, key);
        }
    }

    scr_shutdown();
    return 0;
}
