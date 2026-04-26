/*
 * ui/playback.h - Playback controller wiring browser to audio engine.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Owns the active decode_handle and the audio callback so that the UI
 * (browser ENTER on a music file, SPACE to pause, etc.) can trigger real
 * playback. Models testplay.c's play_callback shape.
 */
#ifndef HEARO_UI_PLAYBACK_H
#define HEARO_UI_PLAYBACK_H

#include "../hearo.h"
#include "nowplay.h"

/* Configure the rate and format the active driver will be opened with on
 * each playback_start_file call. Caller must have already run
 * audiodrv_register_all + audiodrv_auto_select + mixer_init at the same
 * rate/format before invoking this. Idempotent. */
void playback_init(u32 mixer_rate, u8 mixer_format);

/* Load and start playback from a file in the current working directory.
 * Stops any currently playing track first (calls playback_stop). Returns
 * HFALSE if decode_load or drv->open failed; on failure the engine is
 * left in NP_STOPPED so subsequent calls are clean. */
hbool playback_start_file(const char *path);

/* Close the active driver, free the decode handle, return to NP_STOPPED.
 * Safe to call when nothing is playing (no-op). */
void playback_stop(void);

/* Toggle pause state. Has no effect when NP_STOPPED. The audio ISR keeps
 * firing during pause; the callback writes silence and skips decode_advance,
 * so song state is preserved across pause/resume. */
void playback_toggle_pause(void);

np_state_t playback_state(void);

/* 0..100 percent, derived from the active decoder's progress hook. Returns
 * 0 when nothing is loaded. */
u8 playback_progress_pct(void);

#endif
