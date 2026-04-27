/*
 * decode/midi.h - Standard MIDI File player (formats 0 and 1).
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_DECODE_MIDI_H
#define HEARO_DECODE_MIDI_H

#include "../hearo.h"

#define MIDI_MAX_TRACKS 32

typedef struct {
    /* File data, kept resident; tracks point into this. */
    u8  *file_data;
    u32  file_size;

    /* Header */
    u8  format;           /* 0 = single track, 1 = parallel multi, 2 = sequential */
    u16 num_tracks;
    u16 ticks_per_qn;
    u32 tempo_us_per_qn;  /* current tempo, default 500000 */

    /* Per-track state */
    struct {
        u8  *data;        /* points into file_data, after MTrk header */
        u32  length;
        u32  pos;         /* byte offset into data */
        u32  next_tick;   /* absolute tick of next event */
        u8   running_status;
        u8   ended;
    } tracks[MIDI_MAX_TRACKS];

    /* Scheduler */
    u32 current_tick;
    u32 samples_per_tick_q16;   /* Q16.16 fixed-point */
    u32 sample_acc_q16;         /* fractional sample carry */
    u32 cached_mixer_rate;      /* captured at play_init for tempo-change recomputes */
    u8  loaded;
} midi_song_t;

hbool midi_load(const char *filename, midi_song_t *song);
void  midi_free(midi_song_t *song);
void  midi_play_init(midi_song_t *song, u32 mixer_rate);
void  midi_advance(midi_song_t *song, u16 frames);

/* Diagnostic counters for the dispatch path. NULL slots are skipped. Used
 * by testplay's smoke harness to confirm midifm wiring when MIDI playback
 * is silent. Counters reset to zero on midi_play_init via static-zero
 * initialization (BSS); they accumulate across multiple advance calls. */
void  midi_get_diag(u32 *dispatch_count, u32 *note_on_count, u32 *note_off_count);
void  midi_get_diag_ext(u32 *advance_calls, u32 *total_frames,
                        u32 *ticks_processed, u32 *step_q16, u32 *current_tick,
                        u8 *num_tracks);

#endif
