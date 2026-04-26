/*
 * audio/mixer.h - 32-channel software mixer with optional quire path.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_AUDIO_MIXER_H
#define HEARO_AUDIO_MIXER_H

#include "../hearo.h"
#include "../math/quire.h"
#include "audiodrv.h"

#define MIXER_MAX_CHANNELS    32
#define MIXER_BUFFER_SAMPLES  2048

typedef struct {
    hbool   active;
    const s8 *sample_data;
    u32     sample_length;
    u32     loop_start;
    u32     loop_end;
    hbool   looping;
    u32     position;        /* Q16.16 */
    u32     increment;       /* Q16.16 */
    s16     volume_left;     /* 0-256 (8.0) */
    s16     volume_right;
    u8      pan;
    u8      base_volume;
    hbool   bits16;          /* sample is 16-bit signed when set */
} mix_channel_t;

typedef struct {
    mix_channel_t channels[MIXER_MAX_CHANNELS];
    u8     active_channels;
    u32    output_rate;
    u8     output_format;
    /* master_volume MUST stay u8: mixer_set_master writes it with a single
     * byte store (atomic on x86) so the audio ISR's mixer_render can read it
     * without a cli/sti bracket. Refactoring to u16 (or wider) requires
     * wrapping mixer_set_master with _disable()/_enable(). */
    u8     master_volume;
    hbool  use_quire;
    quire_t quire_left;
    quire_t quire_right;
} mixer_state_t;

void mixer_init(u32 rate, u8 format, hbool use_quire);

void mixer_set_channel(u8 ch, const s8 *data, u32 len,
                       u32 loop_start, u32 loop_end, hbool loop);
void mixer_set_channel16(u8 ch, const s16 *data, u32 len,
                         u32 loop_start, u32 loop_end, hbool loop);
void mixer_set_frequency(u8 ch, u32 freq_hz);
void mixer_set_volume(u8 ch, u8 volume, u8 pan);
void mixer_stop_channel(u8 ch);
void mixer_stop_all(void);

void mixer_set_master(u8 volume);
u8   mixer_get_master(void);

/* Render `samples` frames into out_buffer in `format` (must match init). */
void mixer_render(void *out_buffer, u16 samples, u8 format);

const mixer_state_t *mixer_get_state(void);

/* Spectrum hint: rolling RMS magnitude of the last rendered buffer halves,
 * 16 bins of 8-bit values.  Cheap stand-in for FFT until UL_FFT_256 lands. */
void mixer_get_spectrum(u8 out[16]);

#endif
