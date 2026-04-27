/*
 * audio/mixer.c - 32-channel software mixer.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#pragma off (check_stack)  /* see Makefile CF16_ISR -- belt-and-braces */
#include "mixer.h"
#include <i86.h>
#include <stdlib.h>
#include <string.h>

static mixer_state_t M;
/* spectrum_bins is written from compute_spectrum (called from mixer_render,
 * which the audio ISR reaches via its callback) and read from the foreground
 * UI without a lock. volatile prevents the compiler from caching torn reads
 * across the bytewise memcpy in mixer_get_spectrum. */
static volatile u8   spectrum_bins[16];
/* Heap-allocated accumulators keep 16K of working buffers out of DGROUP.
 * Allocated ONCE in mixer_init at startup (see hearo.c) and never freed;
 * mixer_render runs from ISR context and can not be allowed to touch the
 * heap. The ISR-context guard `if (!acc_l || !acc_r) return;` in
 * mixer_render covers a startup race where audio fires before init. */
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
    /* rate==0 would trap mixer_set_frequency on its (freq << 16) / output_rate
     * divide. Caller is supposed to pass the configured output rate; a zero
     * here is almost always misconfiguration, but rather than silently divide
     * by zero later we clamp to a safe 8 kHz default and continue. */
    if (rate == 0) rate = 8000;
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
    /* cli/sti bracket: mixer_render runs from audio ISR context and reads
     * c->position (u32), c->sample_data (far pointer), and c->active inside
     * its inner loop. A torn write here (a mid-update IRQ) could leave the
     * channel pointing at half-old / half-new sample state for one render
     * pass, which manifests as a one-block click. Same rule applies to every
     * mixer_set_* below. */
    _disable();
    c->sample_data   = data;
    c->sample_length = len;
    c->loop_start    = loop_start;
    c->loop_end      = loop_end ? loop_end : len;
    c->looping       = loop;
    c->position      = 0;
    c->bits16        = HFALSE;
    c->active        = (data && len) ? HTRUE : HFALSE;
    _enable();
}

void mixer_set_channel16(u8 ch, const s16 *data, u32 len,
                         u32 loop_start, u32 loop_end, hbool loop)
{
    mixer_set_channel(ch, (const s8 *)data, len, loop_start, loop_end, loop);
    if (ch < MIXER_MAX_CHANNELS) {
        _disable();
        M.channels[ch].bits16 = HTRUE;
        _enable();
    }
}

void mixer_set_frequency(u8 ch, u32 freq_hz)
{
    u32 inc;
    if (ch >= MIXER_MAX_CHANNELS || M.output_rate == 0) return;
    /* increment = freq / out_rate in Q16.16 */
    inc = (u32)(((u32)freq_hz << 16) / M.output_rate);
    _disable();
    M.channels[ch].increment = inc;
    _enable();
}

void mixer_set_volume(u8 ch, u8 volume, u8 pan)
{
    mix_channel_t *c;
    u16 v;
    s16 vr, vl;
    if (ch >= MIXER_MAX_CHANNELS) return;
    c = &M.channels[ch];
    v = volume;
    /* Constant-power-ish pan: linear is fine for tracker work. */
    vr = (s16)((v * (u16)pan) / 255);
    vl = (s16)v - vr;
    _disable();
    c->base_volume  = volume;
    c->pan          = pan;
    c->volume_right = vr;
    c->volume_left  = vl;
    _enable();
}

void mixer_stop_channel(u8 ch)
{
    if (ch >= MIXER_MAX_CHANNELS) return;
    /* Single-byte hbool write is atomic on x86, but bracket anyway for
     * symmetry with the rest of the mixer_set_* family and so that any
     * future widening of `active` does not silently regress. */
    _disable();
    M.channels[ch].active = HFALSE;
    _enable();
}

void mixer_stop_all(void)
{
    u8 i;
    _disable();
    for (i = 0; i < MIXER_MAX_CHANNELS; i++) M.channels[i].active = HFALSE;
    _enable();
}

/* Compile-time assertion that master_volume stays a single byte. Bracket
 * symmetry below assumes a one-byte store; any widening would silently
 * break the ISR-side read in mixer_render. The typedef will fail to
 * compile if the array size goes negative. C89 has no _Static_assert. */
typedef char mixer_master_size_check[
    (sizeof(((mixer_state_t *)0)->master_volume) == 1) ? 1 : -1];

void mixer_set_master(u8 v)
{
    /* Single-byte store is atomic on x86, but bracket for symmetry with the
     * rest of the mixer_set_* family so a future widening of master_volume
     * does not silently regress to a torn write. The static-assert above
     * also guards the invariant. */
    _disable();
    M.master_volume = v;
    _enable();
}
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
/* Position math runs in the FRAME domain, not the Q16.16 domain. The old
 * code computed `end = sample_length << 16`, which silently truncates for
 * any sample of 65536 frames or more (loop_len << 16 == 0 wraps modulo
 * 2^32). At sample_length == 65536 the channel deactivates on the first
 * iteration. Track samples are routinely >= 64K frames in XM / IT modules,
 * so this was not a hypothetical. Compare (pos >> 16) against the frame
 * counts directly. The loop-len shift is bounded by a clamp so the wrap
 * decrement stays correct even for long loops. */
static void mix_naive(s32 far *acc_l, s32 far *acc_r, u16 samples)
{
    u8 ci;
    memset(acc_l, 0, samples * sizeof(s32));
    memset(acc_r, 0, samples * sizeof(s32));
    for (ci = 0; ci < MIXER_MAX_CHANNELS; ci++) {
        mix_channel_t *c = &M.channels[ci];
        u16 i;
        u32 pos, inc;
        u32 end_frame;
        u32 loop_start_frame, loop_end_frame, loop_len_frames;
        u32 loop_len_q16;
        s16 vl, vr;
        if (!c->active || !c->sample_data) continue;
        pos = c->position;
        inc = c->increment;
        end_frame        = c->looping ? c->loop_end : c->sample_length;
        loop_start_frame = c->loop_start;
        loop_end_frame   = c->loop_end;
        loop_len_frames  = (loop_end_frame > loop_start_frame)
                           ? (loop_end_frame - loop_start_frame) : 0;
        /* Clamp before shifting into Q16.16 so the wrap decrement is always
         * representable. A loop length >= 65536 frames falls back to a
         * one-shot stop instead of an infinite loop with a broken wrap. */
        loop_len_q16 = (loop_len_frames && loop_len_frames < 0x10000UL)
                       ? (loop_len_frames << 16) : 0;
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
                if ((pos >> 16) >= end_frame) {
                    if (c->looping && loop_len_q16) {
                        while ((pos >> 16) >= end_frame) pos -= loop_len_q16;
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
                if ((pos >> 16) >= end_frame) {
                    if (c->looping && loop_len_q16) {
                        while ((pos >> 16) >= end_frame) pos -= loop_len_q16;
                    } else { c->active = HFALSE; break; }
                }
            }
        }
        c->position = pos;
    }
}

/* Quire path: same shape, but every multiply-accumulate goes through the
 * 256-bit accumulator before rounding back to s32.  The audible payoff is
 * preserving very quiet channels in dense mixes.
 *
 * The quire implementation is pure integer (see math/quire.c); no FPU state
 * is touched, so this is safe to call from ISR context without FSAVE/FRSTOR. */
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
        /* Advance channels (separate loop keeps the inner branch tight).
         * Frame-domain comparison: see mix_naive for why we do NOT
         * shift sample_length / loop_end into Q16.16 (truncation at
         * lengths >= 65536 frames). */
        for (ci = 0; ci < MIXER_MAX_CHANNELS; ci++) {
            mix_channel_t *c = &M.channels[ci];
            u32 end_frame;
            u32 loop_len_frames;
            u32 loop_len_q16;
            if (!c->active) continue;
            c->position += c->increment;
            end_frame = c->looping ? c->loop_end : c->sample_length;
            if ((c->position >> 16) >= end_frame) {
                if (c->looping) {
                    loop_len_frames = (c->loop_end > c->loop_start)
                                      ? (c->loop_end - c->loop_start) : 0;
                    loop_len_q16 = (loop_len_frames && loop_len_frames < 0x10000UL)
                                   ? (loop_len_frames << 16) : 0;
                    if (loop_len_q16) {
                        while ((c->position >> 16) >= end_frame)
                            c->position -= loop_len_q16;
                    } else c->active = HFALSE;
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
        /* acc_l/acc_r can grow to ~9.4M with 32 channels mixing peak signals.
         * Multiplying by master (up to 255) before reduction overflows s32
         * (acc * 255 wraps above 8.4M). Pre-clamp the accumulator to a
         * 24-bit window so the multiply by master always fits in s32, then
         * shift down by 8 to land in the s16 output range as before. */
        s32 al = sclamp(acc_l[i], -8388608L, 8388607L);
        s32 ar = sclamp(acc_r[i], -8388608L, 8388607L);
        s32 l  = (al * (s32)master) >> 8;
        s32 r  = (ar * (s32)master) >> 8;
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
    /* Manual byte copy: spectrum_bins is volatile (written from ISR), and
     * memcpy is not required to honor volatile semantics. */
    u8 i;
    for (i = 0; i < 16; i++) out[i] = spectrum_bins[i];
}
