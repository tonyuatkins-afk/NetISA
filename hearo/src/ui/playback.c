/*
 * ui/playback.c - Playback controller wiring browser to audio engine.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Phase 3.3 wiring: ui_run is the foreground UI loop; this module owns the
 * song-handle, paused flag, and the audio callback. Browser ENTER on a music
 * file calls playback_start_file; SPACE calls playback_toggle_pause; the
 * F12/ESC exit path drops back through ui_run and main() shuts the driver
 * down via audiodrv_active()->shutdown().
 *
 * Designed to be safe against a stale audio ISR firing during teardown:
 * playback_stop calls drv->close before freeing the song handle, which is
 * what the chunk-A sb_close hardening guarantees is safe (running=0 + cb=0
 * before any teardown I/O, isr_in_progress drain, ordered DMA / DSP / PIC
 * teardown). The callback's `if (paused || !song)` guard catches the brief
 * window between `cb=0` and `song=0` even though the SB driver guarantees
 * the callback pointer is cleared first.
 */

/* Disable Watcom's stack-overflow probe. play_callback is reached from the
 * audio ISR via S.cb; the probe is a call to __STK on every C function
 * entry that subtracts the local frame size from SP and traps if it would
 * underflow. Inside an ISR the probe can route to a runtime helper that
 * calls INT 21h (DOS not reentrant), and even on the path where it does
 * not, an abort from the probe mid-DMA would tear the program down with
 * the SB ISR vector still hooked. Same convention as sb.c and pcspeaker.c. */
#pragma off (check_stack)

#include "playback.h"
#include "nowplay.h"
#include "../audio/audiodrv.h"
#include "../audio/mixer.h"
#include "../decode/decode.h"
#include <stdlib.h>
#include <string.h>

/* The song pointer is volatile because the audio ISR reads it (via
 * play_callback) and the foreground writes it (in playback_start_file
 * and playback_stop). Watcom 16-bit large model stores far pointers as
 * two 16-bit cells; without volatile the compiler is free to load them
 * once and reuse the value across barriers, which would race with a
 * pointer rewrite from the foreground. Today the ordering (drv->close
 * before song=0) closes the actual race window, but volatile preserves
 * correctness if a future refactor rewrites song while S.cb is still
 * wired. */
static decode_handle_t * volatile song;
static u32                        cfg_rate;
static u8                         cfg_format;
static volatile hbool             paused;
static np_state_t                 cur_state = NP_STOPPED;

static void play_callback(void *buffer, u16 samples, u8 format)
{
    /* Race-safe early-out: if song is NULL (between stop and start, or
     * before the first start), write silence instead of dereferencing.
     * Same paused-branch shape as testplay.c. */
    if (paused || !song) {
        memset(buffer, format >= AFMT_S16_MONO ? 0 : 0x80,
               (u32)samples * AFMT_FRAME_BYTES(format));
        return;
    }
    decode_advance(song, samples);
    mixer_render(buffer, samples, format);
}

void playback_init(u32 rate, u8 format)
{
    cfg_rate   = rate;
    cfg_format = format;
    cur_state  = NP_STOPPED;
    paused     = HFALSE;
    song       = 0;
}

void playback_stop(void)
{
    const audio_driver_t *drv = audiodrv_active();
    if (cur_state == NP_STOPPED) return;
    /* Close the driver FIRST. The chunk-A sb_close guarantees the ISR's
     * running flag and cb pointer are cleared before any teardown I/O,
     * so once close returns the callback can no longer fire and we can
     * safely free the song handle. */
    if (drv && drv->close) drv->close();
    if (song) {
        decode_free(song);
        free(song);
        song = 0;
    }
    paused    = HFALSE;
    cur_state = NP_STOPPED;
    nowplay_set_state(NP_STOPPED);
}

hbool playback_start_file(const char *path)
{
    const audio_driver_t *drv;

    if (!path || !path[0]) return HFALSE;

    /* Stop any current track cleanly before starting a new one. The
     * sequence (close driver, free song, set state) is deliberate:
     * the next decode_load below allocates a fresh handle and the
     * previous one must be released first to avoid double-allocating
     * the format-specific sample tables. */
    playback_stop();

    song = (decode_handle_t *)malloc(sizeof(decode_handle_t));
    if (!song) return HFALSE;
    memset(song, 0, sizeof(*song));

    if (!decode_load(path, song)) {
        /* decode_load contract: on failure the format-specific loader
         * (mod_load, s3m_load, etc.) frees any state it allocated before
         * returning HFALSE. We only release our outer malloc here.
         * Matches the convention in test/testplay.c. */
        free(song);
        song = 0;
        return HFALSE;
    }

    drv = audiodrv_active();
    if (!drv || !drv->open) {
        decode_free(song);
        free(song);
        song = 0;
        return HFALSE;
    }

    /* Prime mixer state. MOD's decode_start only resets sequencer state;
     * the first row of channel data isn't pushed until decode_advance runs.
     * Doing it here means the very first ISR sees an already-active mixer.
     * Same idiom as testplay.c. */
    decode_start(song, cfg_rate);
    decode_advance(song, 1);

    if (!drv->open(cfg_rate, cfg_format, play_callback)) {
        decode_free(song);
        free(song);
        song = 0;
        return HFALSE;
    }

    paused    = HFALSE;
    cur_state = NP_PLAYING;
    /* Surface track metadata to the now-playing pane. Decoders fill
     * song->title and song->duration_seconds during decode_load. */
    nowplay_set_track(song->title[0] ? song->title : path,
                      "", "", song->duration_seconds);
    nowplay_set_state(NP_PLAYING);
    return HTRUE;
}

void playback_toggle_pause(void)
{
    if (cur_state == NP_STOPPED) return;
    paused    = paused ? HFALSE : HTRUE;
    cur_state = paused ? NP_PAUSED : NP_PLAYING;
    nowplay_set_state(cur_state);
}

np_state_t playback_state(void) { return cur_state; }

u8 playback_progress_pct(void)
{
    return song ? decode_progress(song) : 0;
}
