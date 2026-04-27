/*
 * decode/xm.h - FastTracker II XM loader and player.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * MVP: parses the full XM header, unpacks patterns, loads the first sample
 * of each instrument (multi-sample instruments collapse to sample 0),
 * delta-decodes sample data, and runs a tick-based sequencer.  Envelopes,
 * multi-sample mapping, vibrato sweep, and the long tail of the XM effect
 * set are deferred.  Plays simple modules cleanly.
 */
#ifndef HEARO_DECODE_XM_H
#define HEARO_DECODE_XM_H

#include "../hearo.h"

#define XM_MAX_CHANNELS    32
#define XM_MAX_PATTERNS    256
#define XM_MAX_INSTRUMENTS 128
#define XM_MAX_ORDERS      256
#define XM_ROW_BYTES       (5 * XM_MAX_CHANNELS)

typedef struct {
    char name[23];
    u32  length;          /* in samples */
    u32  loop_start;
    u32  loop_length;
    u8   volume;          /* 0..64 */
    s8   finetune;        /* -128..+127 */
    u8   type;            /* bit 0/1 = loop type, bit 4 = 16-bit */
    u8   panning;
    s8   relative_note;
    void *data;           /* signed PCM after delta decode */
} xm_sample_t;

/* XM instruments can have up to 16 samples mapped across MIDI notes via
 * sample_for_note[]. MVP shipped with one sample per instrument; this
 * struct holds the full 16-sample table. Loader fills as many as the
 * file declares (num_samples). Playback selects via sample_for_note[]. */
#define XM_MAX_INSTR_SAMPLES 16
typedef struct {
    char name[23];
    u8   num_samples;
    u8   sample_for_note[96];
    u8   volume;          /* fallback when sample's volume is 0 */
    xm_sample_t samples[XM_MAX_INSTR_SAMPLES];
} xm_instrument_t;

typedef struct {
    char title[21];
    u16  song_length;
    u16  restart;
    u16  num_channels;
    u16  num_patterns;
    u16  num_instruments;
    u16  flags;           /* bit 0 = linear periods (else Amiga) */
    u16  init_tempo;
    u16  init_bpm;
    u8   order[XM_MAX_ORDERS];

    u8           *patterns[XM_MAX_PATTERNS];
    u16           pattern_rows[XM_MAX_PATTERNS];
    xm_instrument_t *instruments[XM_MAX_INSTRUMENTS];

    /* Playback state */
    u8   current_order;
    u8   current_row;
    u8   tempo;           /* ticks per row */
    u8   bpm;
    u8   tick;
    u32  tick_samples;
    u32  tick_acc;

    struct {
        u16  period;
        u8   sample_num;
        u8   volume;
        u8   pan;
        u8   effect;
        u8   effect_param;
    } chans[XM_MAX_CHANNELS];

    u8   loaded;
} xm_song_t;

hbool xm_load(const char *filename, xm_song_t *song);
void  xm_free(xm_song_t *song);
void  xm_play_init(xm_song_t *song, u32 r);
void  xm_advance(xm_song_t *song, u16 f);

#endif
