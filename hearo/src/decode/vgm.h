/*
 * decode/vgm.h - VGM chip-music player (OPL2/OPL3 + SN76489).
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * VGM is a clock-accurate dump of chip register writes. Files run at a
 * notional 44100 Hz sample clock; "waits" are expressed in those samples.
 * We rescale to whatever rate the host mixer uses.
 *
 * Subset implemented: header v1.50+, opcodes 0x50 (SN76489), 0x5A (OPL2),
 * 0x5E/0x5F (OPL3 banks 0/1), 0x61 (wait), 0x62/0x63 (preset frame waits),
 * 0x66 (end), 0x67 (data block, skipped), 0x70-0x7F (short wait).
 * Other chips (YM2612, AY8910, etc.) get their opcodes consumed and
 * discarded so timing stays correct.
 */
#ifndef HEARO_DECODE_VGM_H
#define HEARO_DECODE_VGM_H

#include "../hearo.h"

typedef struct {
    u8  *file_data;
    u32  file_size;

    u32  vgm_data_start;        /* absolute offset of first command */
    u32  total_samples;         /* total VGM-clock samples in song */
    u32  loop_offset;           /* absolute offset of loop start, 0 if none */
    u32  loop_samples;
    u32  ym3812_clock;
    u32  ymf262_clock;
    u32  sn76489_clock;
    u8   loops_done;
    u8   loops_max;             /* 0 = infinite */

    /* Playback state */
    u32  cursor;                /* file offset of next command */
    u32  pending_wait_samples;  /* in VGM samples (44100 Hz), not mixer */
    u32  q16_acc;               /* fractional accumulator for rate conversion */
    u32  q16_step;              /* mixer samples consumed per VGM sample, Q16.16 */
    u8   ended;
    u8   loaded;
} vgm_song_t;

hbool vgm_load(const char *filename, vgm_song_t *song);
void  vgm_free(vgm_song_t *song);
void  vgm_play_init(vgm_song_t *song, u32 mixer_rate);
void  vgm_advance(vgm_song_t *song, u16 frames);

#endif
