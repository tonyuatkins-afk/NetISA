/*
 * decode/wav.h - RIFF/WAVE PCM loader.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_DECODE_WAV_H
#define HEARO_DECODE_WAV_H

#include "../hearo.h"

typedef struct {
    u16 format;
    u16 channels;
    u32 sample_rate;
    u32 byte_rate;
    u16 block_align;
    u16 bits_per_sample;
    void *data;            /* malloc'd raw sample data; free with wav_free */
    u32  data_length;      /* in bytes */
    u32  num_frames;
    /* Stereo deinterleave path stashes per-channel buffers here so wav_free
     * can release them.  NULL when the file is mono. */
    void *deinterleaved_left;
    void *deinterleaved_right;
} wav_file_t;

hbool wav_load(const char *filename, wav_file_t *wav);
void  wav_free(wav_file_t *wav);

#endif
