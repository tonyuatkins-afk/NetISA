/*
 * audio/mixer.c - 32-channel software mixer.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#include "mixer.h"
#include <stdlib.h>
#include <string.h>

static mixer_state_t M;
static u8            spectrum_bins[16];
/* Heap-allocated accumulators keep 16K of working buffers out of DGROUP. */
static s32 far      *acc_l;
static s32 far      *acc_r;

static s32 sclamp(s32 v, s32 lo, s32 hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void mixer_init(u32 rate, u8 format, hbool use_quire)
{
    memset(&M, 0, sizeof(M));
    M.output_rate   = rate;
    M.output_format = format;
    M.use_quire     = use_quire;
    M.master_volume = 200;
    if (!acc_l) acc_l = (s32 far *)malloc(MIXER_BUFFER_SAMPLES * sizeof(s32));
    if (!acc_r) acc_r = (s32 far *)malloc(MIXER_BUFFER_SAMPLES * sizeof(s32));
}

void mixer_set_channel(u8 ch, const s8 *data, u32 len,
                       u32 loop_start, u32 loop_end, hbool loop)
{
    mix_channel_t *c;
    if (ch >= MIXER_MAX_CHANNELS) return;
    c = &M.channels[ch];
    c->sample_data   = data;
    c->sample_length = len;
    c->loop_start    = loop_start;
    c->loop_end      = loop_end ? loop_end : len;
    c->looping       = loop;
    c->position      = 0;
    c->bits16        = HFALSE;
    c->active        = (data && len) ? HTRUE : HFALSE;
}

void mixer_set_channel16(u8 ch, const s16 *data, u32 len,
                         u32 loop_start, u32 loop_end, hbool loop)
{
    mixer_set_channel(ch, (const s8 *)data, len, loop_start, loop_end, loop);
    if (ch < MIXER_MAX_CHANNELS) M.channels[ch].bits16 = HTRUE;
}

void mixer_set_frequency(u8 ch, u32 freq_hz)
{
    if (ch >= MIXER_MAX_CHANNELS || M.output_rate == 0) return;
    /* increment = freq / out_rate in Q16.16 */
    M.channels[ch].increment = (u32)(((u32)freq_hz << 16) / M.output_rate);
}

void mixer_set_volume(u8 ch, u8 volume, u8 pan)
{
    mix_channel_t *c;
    u16 v;
    if (ch >= MIXER_MAX_CHANNELS) return;
    c = &M.channels[ch];
    c->base_volume = volume;
    c->pan         = pan;
    v = volume;
    /* Constant-power-ish pan: linear is fine for tracker work. */
    c->volume_right = (s16)((v * (u16)pan) / 255);
    c->volume_left  = (s16)v - c->volume_right;
}

void mixer_stop_channel(u8 ch)
{
    if (ch >= MIXER_MAX_CHANNELS) return;
    M.channels[ch].active = HFALSE;
}

void mixer_stop_all(void)
{
    u8 i;
    for (i = 0; i < MIXER_MAX_CHANNELS; i++) M.channels[i].active = HFALSE;
}

void mixer_set_master(u8 v) { M.master_volume = v; }
u8   mixer_get_master(void) { return M.master_volume; }

const mixer_state_t *mixer_get_state(void) { return &M; }

/* Inner mixing loop (naive integer path).  Renders into 32-bit accumulators
 * then converts down to the requested format.
 *
 * Sample data pointers are declared `__huge` so far-pointer arithmetic
 * auto-normalizes the segment when the offset would wrap.  Watcom emits
 * a slightly heavier address calculation per access, but for an XM with
 * 256K+ samples it's the only way to read past offset 0xFFFF without
 * silently aliasing into the start of the same segment. */
static void mix_naive(s32 far *acc_l, s32 far *acc_r, u16 samples)
{
    u8 ci;
    memset(acc_l, 0, samples * sizeof(s32));
    memset(acc_r, 0, samples * sizeof(s32));
    for (ci = 0; ci < MIXER_MAX_CHANNELS; ci++) {
        mix_channel_t *c = &M.channels[ci];
        u16 i;
        u32 pos, inc, end;
        s16 vl, vr;
        if (!c->active || !c->sample_data) continue;
        pos = c->position;
        inc = c->increment;
        end = (c->looping ? c->loop_end : c->sample_length) << 16;
        vl  = c->volume_left;
        vr  = c->volume_right;
        if (c->bits16) {
            const s16 huge *src = (const s16 huge *)c->sample_data;
            for (i = 0; i < samples; i++) {
                u32 idx = pos >> 16;
                s32 s   = (s32)src[idx];
                acc_l[i] += (s * vl) >> 8;
                acc_r[i] += (s * vr) >> 8;
                pos += inc;
                if (pos >= end) {
                    if (c->looping) {
                        u32 loop_len = (c->loop_end - c->loop_start) << 16;
                        if (loop_len) {
                            while (pos >= end) pos -= loop_len;
                        } else { c->active = HFALSE; break; }
                    } else { c->active = HFALSE; break; }
                }
            }
        } else {
            const s8 huge *src = (const s8 huge *)c->sample_data;
            for (i = 0; i < samples; i++) {
                u32 idx = pos >> 16;
                s32 s   = (s32)src[idx] << 8;
                acc_l[i] += (s * vl) >> 8;
                acc_r[i] += (s * vr) >> 8;
                pos += inc;
                if (pos >= end) {
                    if (c->looping) {
                        u32 loop_len = (c->loop_end - c->loop_start) << 16;
                        if (loop_len) {
                            while (pos >= end) pos -= loop_len;
                        } else { c->active = HFALSE; break; }
                    } else { c->active = HFALSE; break; }
                }
            }
        }
        c->position = pos;
    }
}

/* Quire path: same shape, but every multiply-accumulate goes through the
 * 256-bit accumulator before rounding back to s32.  The audible payoff is
 * preserving very quiet channels in dense mixes. */
static void mix_quire(s32 far *acc_l, s32 far *acc_r, u16 samples)
{
    u16 i;
    u8 ci;
    for (i = 0; i < samples; i++) {
        quire_clear(&M.quire_left);
        quire_clear(&M.quire_right);
        for (ci = 0; ci < MIXER_MAX_CHANNELS; ci++) {
            mix_channel_t *c = &M.channels[ci];
            u32 idx;
            s32 s;
            if (!c->active || !c->sample_data) continue;
            idx = c->position >> 16;
            if (c->bits16) {
                const s16 huge *src = (const s16 huge *)c->sample_data;
                s = (s32)src[idx];
            } else {
                const s8 huge *src = (const s8 huge *)c->sample_data;
                s = (s32)src[idx] << 8;
            }
            quire_mac(&M.quire_left,  s, (s32)c->volume_left);
            quire_mac(&M.quire_right, s, (s32)c->volume_right);
        }
        acc_l[i] = quire_round(&M.quire_left, 8);
        acc_r[i] = quire_round(&M.quire_right, 8);
        /* Advance channels (separate loop keeps the inner branch tight). */
        for (ci = 0; ci < MIXER_MAX_CHANNELS; ci++) {
            mix_channel_t *c = &M.channels[ci];
            u32 end;
            if (!c->active) continue;
            c->position += c->increment;
            end = (c->looping ? c->loop_end : c->sample_length) << 16;
            if (c->position >= end) {
                if (c->looping) {
                    u32 loop_len = (c->loop_end - c->loop_start) << 16;
                    if (loop_len) c->position -= loop_len;
                    else c->active = HFALSE;
                } else c->active = HFALSE;
            }
        }
    }
}

static void compute_spectrum(const s32 far *acc_l, const s32 far *acc_r, u16 samples)
{
    u8 b;
    u16 step = samples / 16;
    if (step == 0) return;
    for (b = 0; b < 16; b++) {
        u16 i;
        u32 sum = 0;
        for (i = 0; i < step; i++) {
            s32 v = acc_l[b * step + i] + acc_r[b * step + i];
            if (v < 0) v = -v;
            sum += (u32)v;
        }
        sum /= step;
        if (sum > 65535) sum = 65535;
        spectrum_bins[b] = (u8)(sum >> 8);
    }
}

void mixer_render(void *out_buffer, u16 samples, u8 format)
{
    u16 i;
    u32 master = M.master_volume;
    u8 active = 0;
    u8 j;

    if (!acc_l || !acc_r) return;
    if (samples > MIXER_BUFFER_SAMPLES) samples = MIXER_BUFFER_SAMPLES;

    for (j = 0; j < MIXER_MAX_CHANNELS; j++) if (M.channels[j].active) active++;
    M.active_channels = active;

    if (M.use_quire) mix_quire(acc_l, acc_r, samples);
    else             mix_naive(acc_l, acc_r, samples);

    compute_spectrum(acc_l, acc_r, samples);

    for (i = 0; i < samples; i++) {
        s32 l = (acc_l[i] * (s32)master) >> 8;
        s32 r = (acc_r[i] * (s32)master) >> 8;
        l = sclamp(l, -32768, 32767);
        r = sclamp(r, -32768, 32767);
        switch (format) {
        case AFMT_U8_MONO: {
            s32 m = (l + r) >> 1;
            ((u8 *)out_buffer)[i] = (u8)((m >> 8) + 128);
            break;
        }
        case AFMT_U8_STEREO:
            ((u8 *)out_buffer)[i*2]   = (u8)((l >> 8) + 128);
            ((u8 *)out_buffer)[i*2+1] = (u8)((r >> 8) + 128);
            break;
        case AFMT_S16_MONO:
            ((s16 *)out_buffer)[i] = (s16)((l + r) >> 1);
            break;
        case AFMT_S16_STEREO:
        default:
            ((s16 *)out_buffer)[i*2]   = (s16)l;
            ((s16 *)out_buffer)[i*2+1] = (s16)r;
            break;
        }
    }
}

void mixer_get_spectrum(u8 out[16])
{
    memcpy(out, spectrum_bins, 16);
}
