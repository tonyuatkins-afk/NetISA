/*
 * decode/s3m.h - ScreamTracker 3 loader and player.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * S3M is the demoscene staple of 1994-1996. Up to 32 channels, 8-bit or
 * 16-bit samples, fine pitch/volume control, OPL2 instruments alongside
 * sampled instruments. We implement the sampled-instrument path; OPL
 * instruments are loaded but routed to the FM driver in Phase 3.5.
 */
#ifndef HEARO_DECODE_S3M_H
#define HEARO_DECODE_S3M_H

#include "../hearo.h"

#define S3M_MAX_SAMPLES   100
#define S3M_MAX_PATTERNS  100
#define S3M_MAX_CHANNELS  32
#define S3M_MAX_ORDERS    256

typedef struct {
    u8   type;            /* 0 = empty, 1 = sampled, 2-7 = OPL melody/perc */
    char filename[13];
    u32  length;          /* in samples */
    u32  loop_start;
    u32  loop_end;
    u8   volume;          /* 0..64 */
    u8   pack;            /* 0 = unpacked PCM */
    u8   flags;           /* bit 0 = loop, bit 1 = stereo, bit 2 = 16-bit */
    u32  c2spd;           /* sample rate at C-5 (middle C in S3M) */
    char name[28];
    void *data;           /* _fmalloc'd; signed if file_format=2, unsigned if 1 */
} s3m_sample_t;

typedef struct {
    /* Header */
    char title[28];
    u8   ord_count;
    u8   smp_count;
    u8   pat_count;
    u16  flags;
    u16  cwt_v;            /* tracker version */
    u16  ffi;              /* 1 = unsigned samples, 2 = signed */
    u8   global_vol;       /* 0..64 */
    u8   init_speed;
    u8   init_tempo;
    u8   master_vol;
    u8   default_pan;      /* 0xFC = use channel pan table */
    u8   chan_settings[32];/* per-channel: enabled? L/R? OPL melody/perc? */
    u8   chan_pan[32];     /* default pan, only used when default_pan = 0xFC */
    u8   order[S3M_MAX_ORDERS];
    u16  para_inst[S3M_MAX_SAMPLES];
    u16  para_pat [S3M_MAX_PATTERNS];

    /* Loaded resources */
    s3m_sample_t samples[S3M_MAX_SAMPLES];
    u8          *patterns[S3M_MAX_PATTERNS];   /* unpacked rows[64] of channel events */
    u16          pattern_bytes[S3M_MAX_PATTERNS];

    /* Playback state */
    u8   current_order;
    u8   current_row;
    u8   speed;
    u8   tempo;
    u8   tick;
    u32  tick_samples;
    u32  tick_acc;
    u8   num_channels;     /* count of enabled sampled channels */

    struct {
        u16  period;
        u16  target_period;
        u8   sample_num;
        u8   volume;
        u8   pan;
        u8   effect;
        u8   effect_param;
        u8   port_speed;
        u8   vib_pos, vib_speed, vib_depth;
        u8   last_eff[16]; /* memory for effects */
    } chans[S3M_MAX_CHANNELS];
} s3m_song_t;

hbool s3m_load(const char *filename, s3m_song_t *song);
void  s3m_free(s3m_song_t *song);
void  s3m_play_init(s3m_song_t *song, u32 mixer_rate);
void  s3m_advance(s3m_song_t *song, u16 frames);

#endif
