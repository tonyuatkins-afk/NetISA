/*
 * decode/mod.h - ProTracker MOD loader and player.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_DECODE_MOD_H
#define HEARO_DECODE_MOD_H

#include "../hearo.h"

#define MOD_MAX_SAMPLES   31
#define MOD_MAX_CHANNELS  8

typedef struct {
    char name[23];
    u16  length_words;        /* sample length in 16-bit words */
    s8   finetune;            /* -8..+7 */
    u8   volume;              /* 0..64 */
    u16  loop_start_words;
    u16  loop_length_words;
    s8  *data;                /* signed 8-bit samples, allocated */
} mod_sample_t;

typedef struct {
    /* Static song data. */
    char title[21];
    u8   num_channels;        /* 4, 6, 8 */
    u8   num_samples;
    u8   song_length;
    u8   restart;
    u8   order[128];
    u8   num_patterns;
    u8  *patterns;            /* num_patterns * 64 * channels * 4 bytes */
    mod_sample_t samples[MOD_MAX_SAMPLES];

    /* Playback state. */
    u8   current_order;
    u8   current_row;
    u8   speed;               /* ticks per row */
    u8   tempo;               /* BPM */
    u8   tick;                /* current tick within row */
    u32  tick_samples;        /* samples between ticks at output rate */
    u32  tick_acc;            /* sample counter toward next tick */

    struct {
        u16  period;
        u16  target_period;
        u8   sample_num;       /* 1-based, 0 = none */
        u8   volume;
        u8   pan;              /* 0..255 */
        u8   effect;
        u8   effect_param;
        u8   port_speed;
        u8   vib_pos, vib_speed, vib_depth;
        u8   trem_pos, trem_speed, trem_depth;
        u8   arp_off1, arp_off2;
    } chans[MOD_MAX_CHANNELS];
} mod_song_t;

hbool mod_load(const char *filename, mod_song_t *song);
void  mod_free(mod_song_t *song);

/* Initialize playback against the active mixer (must be open already). */
void  mod_play_init(mod_song_t *song, u32 mixer_rate);
/* Advance one tick worth of frames; called from the audio callback before
 * mixer_render(). The sequencer adjusts mixer channel state in lock step
 * with the tick boundary defined by song tempo. */
void  mod_advance(mod_song_t *song, u16 frames);

/* Convenience query for the UI. */
u8    mod_get_position(const mod_song_t *song);

#endif
