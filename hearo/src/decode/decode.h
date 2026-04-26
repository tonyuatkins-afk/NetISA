/*
 * decode/decode.h - Format-aware loader and playback dispatcher.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_DECODE_DECODE_H
#define HEARO_DECODE_DECODE_H

#include "../hearo.h"
#include "wav.h"
#include "mod.h"
#include "s3m.h"
#include "xm.h"
#include "it.h"
#include "midi.h"
#include "vgm.h"

typedef enum {
    DECODE_NONE = 0,
    DECODE_WAV, DECODE_MOD, DECODE_S3M, DECODE_XM, DECODE_IT, DECODE_MIDI, DECODE_VGM
} decode_format_t;

typedef struct {
    decode_format_t format;
    union {
        wav_file_t   wav;
        mod_song_t   mod;
        s3m_song_t   s3m;
        xm_song_t    xm;
        it_song_t    it;
        midi_song_t  midi;
        vgm_song_t   vgm;
    } u;
    char filename[80];
    char title[80];
    u32  duration_seconds;
} decode_handle_t;

decode_format_t decode_guess(const char *path);

hbool decode_load (const char *path, decode_handle_t *h);
void  decode_free (decode_handle_t *h);
void  decode_start(decode_handle_t *h, u32 mixer_rate);
/* Called from the audio callback to advance the song before mixing. */
void  decode_advance(decode_handle_t *h, u16 frames);
/* Returns 0..100 for UI display. */
u8    decode_progress(const decode_handle_t *h);

#endif
