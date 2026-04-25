/*
 * main.c - Discord v2 for DOS entry point
 *
 * Usage: DISCORD.EXE
 */

#include "discord.h"

int main(void)
{
    static dc_state_t state;

    scr_init();
    scr_cursor_hide();
    dc_config_load(&state.config);
    dc_init(&state);
    scr_fade_in(8, 40);
    dc_render_all(&state);

    while (state.running) {
        if (scr_kbhit()) {
            int key = scr_getkey();
            dc_handle_key(&state, key);
        }
        dc_poll_messages(&state);
        if (state.dirty) {
            dc_render_all(&state);
            state.dirty = 0;
        }
        /* DOS idle: yield to TSR and background tasks */
        _asm { int 28h }
    }

    state.config.last_channel = state.selected_channel;
    dc_config_save(&state.config);
    scr_fade_out(8, 40);
    scr_shutdown();
    return 0;
}
