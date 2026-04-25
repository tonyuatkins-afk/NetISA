/*
 * ui/nowplay.h - Now playing pane.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_UI_NOWPLAY_H
#define HEARO_UI_NOWPLAY_H

#include "../hearo.h"

typedef enum { NP_STOPPED=0, NP_PLAYING, NP_PAUSED } np_state_t;

void nowplay_set_track(const char *title, const char *artist, const char *album, u32 duration_s);
void nowplay_set_output(const char *device_label);
void nowplay_set_state(np_state_t s);
void nowplay_advance(u32 seconds);
void nowplay_render(u8 x, u8 y, u8 w, u8 h);
np_state_t nowplay_state(void);
u32 nowplay_position(void);
u32 nowplay_duration(void);

#endif
