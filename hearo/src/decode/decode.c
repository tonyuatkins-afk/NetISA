/*
 * decode/decode.c - Format dispatcher.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#pragma off (check_stack)  /* see Makefile CF16_ISR -- belt-and-braces */
#include "decode.h"
#include "../audio/mixer.h"
#include "../audio/audiodrv.h"
#include "midifm.h"
#include <ctype.h>
#include <dos.h>
#include <malloc.h>
#include <string.h>

static decode_format_t guess_from_ext(const char *p)
{
    const char *dot;
    char ext[6];
    int i;
    if (!p) return DECODE_NONE;
    dot = strrchr(p, '.');
    if (!dot) return DECODE_NONE;
    dot++;
    for (i = 0; i < 5 && dot[i]; i++) ext[i] = (char)toupper((unsigned char)dot[i]);
    ext[i] = 0;
    if (strcmp(ext, "WAV") == 0)                 return DECODE_WAV;
    if (strcmp(ext, "MOD") == 0)                 return DECODE_MOD;
    if (strcmp(ext, "S3M") == 0)                 return DECODE_S3M;
    if (strcmp(ext, "XM") == 0)                  return DECODE_XM;
    if (strcmp(ext, "IT") == 0)                  return DECODE_IT;
    if (strcmp(ext, "MID") == 0 || strcmp(ext, "MIDI") == 0) return DECODE_MIDI;
    if (strcmp(ext, "VGM") == 0) return DECODE_VGM;
    return DECODE_NONE;
}

decode_format_t decode_guess(const char *path) { return guess_from_ext(path); }

hbool decode_load(const char *path, decode_handle_t *h)
{
    if (!path || !h) return HFALSE;
    memset(h, 0, sizeof(*h));
    strncpy(h->filename, path, sizeof(h->filename) - 1);
    h->format = guess_from_ext(path);
    switch (h->format) {
    case DECODE_WAV:
        if (!wav_load(path, &h->u.wav)) return HFALSE;
        if (h->u.wav.sample_rate)
            h->duration_seconds = h->u.wav.num_frames / h->u.wav.sample_rate;
        strncpy(h->title, path, sizeof(h->title) - 1);
        return HTRUE;
    case DECODE_MOD:
        if (!mod_load(path, &h->u.mod)) return HFALSE;
        strncpy(h->title, h->u.mod.title, sizeof(h->title) - 1);
        return HTRUE;
    case DECODE_S3M:
        if (!s3m_load(path, &h->u.s3m)) return HFALSE;
        if (h->u.s3m.title[0])
            strncpy(h->title, h->u.s3m.title, sizeof(h->title) - 1);
        else
            strncpy(h->title, path, sizeof(h->title) - 1);
        return HTRUE;
    case DECODE_XM:
        if (!xm_load(path, &h->u.xm)) return HFALSE;
        if (h->u.xm.title[0])
            strncpy(h->title, h->u.xm.title, sizeof(h->title) - 1);
        else
            strncpy(h->title, path, sizeof(h->title) - 1);
        return HTRUE;
    case DECODE_IT:
        if (!it_load(path, &h->u.it)) return HFALSE;
        strncpy(h->title, h->u.it.title, sizeof(h->title) - 1);
        return HTRUE;
    case DECODE_MIDI:
        if (!midi_load(path, &h->u.midi)) return HFALSE;
        strncpy(h->title, path, sizeof(h->title) - 1);
        /* Initialise the FM bank.  We always assume OPL3 capability when
         * the AdLib driver was registered; if the box only has OPL2 the
         * second-bank writes are no-ops via adlib_write_b. */
        midifm_init(HTRUE);
        /* Try to load a GENMIDI.OP2 (DMX OPL2 bank). Search a few common
         * locations: CWD first, then a banks/ subdir, then C:\HEARO\. The
         * first hit wins. If none are present, midifm falls back to its
         * built-in 16-patch hand-tuned bank. */
        if (!midifm_load_bank("GENMIDI.OP2")) {
            if (!midifm_load_bank("banks\\GENMIDI.OP2")) {
                (void)midifm_load_bank("C:\\HEARO\\GENMIDI.OP2");
            }
        }
        return HTRUE;
    case DECODE_VGM:
        if (!vgm_load(path, &h->u.vgm)) return HFALSE;
        strncpy(h->title, path, sizeof(h->title) - 1);
        return HTRUE;
    default:
        return HFALSE;
    }
}

void decode_free(decode_handle_t *h)
{
    if (!h) return;
    switch (h->format) {
    case DECODE_WAV:  wav_free(&h->u.wav);  break;
    case DECODE_MOD:  mod_free(&h->u.mod);  break;
    case DECODE_S3M:  s3m_free(&h->u.s3m);  break;
    case DECODE_XM:   xm_free(&h->u.xm);    break;
    case DECODE_IT:   it_free(&h->u.it);    break;
    case DECODE_MIDI: midi_free(&h->u.midi); break;
    case DECODE_VGM:  vgm_free(&h->u.vgm);  break;
    default: break;
    }
    memset(h, 0, sizeof(*h));
}

void decode_start(decode_handle_t *h, u32 rate)
{
    if (!h) return;
    switch (h->format) {
    case DECODE_WAV: {
        wav_file_t *w = &h->u.wav;
        if (w->channels == 2) {
            /* The mixer indexes by frame (pos>>16), not by interleaved sample.
             * Setting two mixer channels both pointing at the interleaved
             * buffer would walk [L0][R0][L1][R1]... as serial mono and play
             * at 2x pitch with channel bleed.  Deinterleave here so each
             * mixer channel sees its own contiguous mono stream.
             *
             * Watcom large model far-pointer arithmetic does not normalize
             * segment:offset, so a single-pointer indexing loop wraps at
             * 32K samples (16-bit) or 64K bytes (8-bit).  Process in 16K-
             * frame slices with a normalized far pointer recomputed each
             * slice so we never index past the wrap boundary. */
            u32 nf = w->num_frames;
            u32 i;
            if (w->bits_per_sample == 16) {
                s16 *left  = (s16 *)_fmalloc(nf * 2);
                s16 *right = left ? (s16 *)_fmalloc(nf * 2) : 0;
                u32 src_phys  = ((u32)FP_SEG(w->data) << 4) + FP_OFF(w->data);
                u32 left_phys = left  ? ((u32)FP_SEG(left)  << 4) + FP_OFF(left)  : 0;
                u32 right_phys= right ? ((u32)FP_SEG(right) << 4) + FP_OFF(right) : 0;
                if (!left || !right) {
                    if (left)  _ffree(left);
                    if (right) _ffree(right);
                    return;
                }
                for (i = 0; i < nf; ) {
                    u16 batch = (nf - i > 0x4000) ? 0x4000 : (u16)(nf - i);
                    s16 far *s = (s16 far *)MK_FP((u16)((src_phys  >> 4) & 0xFFFF), (u16)(src_phys  & 0xF));
                    s16 far *l = (s16 far *)MK_FP((u16)((left_phys >> 4) & 0xFFFF), (u16)(left_phys & 0xF));
                    s16 far *r = (s16 far *)MK_FP((u16)((right_phys>> 4) & 0xFFFF), (u16)(right_phys& 0xF));
                    u16 j;
                    for (j = 0; j < batch; j++) { l[j] = s[j*2]; r[j] = s[j*2 + 1]; }
                    src_phys   += (u32)batch * 4;     /* 2 bytes * 2 channels per frame */
                    left_phys  += (u32)batch * 2;
                    right_phys += (u32)batch * 2;
                    i += batch;
                }
                w->deinterleaved_left  = left;
                w->deinterleaved_right = right;
                mixer_set_channel16(0, left,  nf, 0, 0, HFALSE);
                mixer_set_channel16(1, right, nf, 0, 0, HFALSE);
            } else {
                s8 *left  = (s8 *)_fmalloc(nf);
                s8 *right = left ? (s8 *)_fmalloc(nf) : 0;
                u32 src_phys  = ((u32)FP_SEG(w->data) << 4) + FP_OFF(w->data);
                u32 left_phys = left  ? ((u32)FP_SEG(left)  << 4) + FP_OFF(left)  : 0;
                u32 right_phys= right ? ((u32)FP_SEG(right) << 4) + FP_OFF(right) : 0;
                if (!left || !right) {
                    if (left)  _ffree(left);
                    if (right) _ffree(right);
                    return;
                }
                for (i = 0; i < nf; ) {
                    u16 batch = (nf - i > 0x4000) ? 0x4000 : (u16)(nf - i);
                    s8 far *s = (s8 far *)MK_FP((u16)((src_phys  >> 4) & 0xFFFF), (u16)(src_phys  & 0xF));
                    s8 far *l = (s8 far *)MK_FP((u16)((left_phys >> 4) & 0xFFFF), (u16)(left_phys & 0xF));
                    s8 far *r = (s8 far *)MK_FP((u16)((right_phys>> 4) & 0xFFFF), (u16)(right_phys& 0xF));
                    u16 j;
                    for (j = 0; j < batch; j++) { l[j] = s[j*2]; r[j] = s[j*2 + 1]; }
                    src_phys   += (u32)batch * 2;     /* 1 byte * 2 channels */
                    left_phys  += batch;
                    right_phys += batch;
                    i += batch;
                }
                w->deinterleaved_left  = left;
                w->deinterleaved_right = right;
                mixer_set_channel(0, left,  nf, 0, 0, HFALSE);
                mixer_set_channel(1, right, nf, 0, 0, HFALSE);
            }
            mixer_set_frequency(0, w->sample_rate);
            mixer_set_frequency(1, w->sample_rate);
            mixer_set_volume   (0, 255, 0);     /* full left */
            mixer_set_volume   (1, 255, 255);   /* full right */
        } else {
            if (w->bits_per_sample == 16) {
                mixer_set_channel16(0, (s16 *)w->data, w->num_frames, 0, 0, HFALSE);
            } else {
                mixer_set_channel(0, (s8 *)w->data, w->num_frames, 0, 0, HFALSE);
            }
            mixer_set_frequency(0, w->sample_rate);
            mixer_set_volume   (0, 255, 128);
        }
        break;
    }
    case DECODE_MOD:  mod_play_init(&h->u.mod, rate);  break;
    case DECODE_S3M:  s3m_play_init(&h->u.s3m, rate);  break;
    case DECODE_XM:   xm_play_init (&h->u.xm,  rate);  break;
    case DECODE_IT:   it_play_init (&h->u.it,  rate);  break;
    case DECODE_MIDI: midi_play_init(&h->u.midi, rate); break;
    case DECODE_VGM:  vgm_play_init (&h->u.vgm,  rate); break;
    default: break;
    }
}

void decode_advance(decode_handle_t *h, u16 frames)
{
    if (!h) return;
    switch (h->format) {
    case DECODE_MOD:  mod_advance(&h->u.mod, frames);  break;
    case DECODE_S3M:  s3m_advance(&h->u.s3m, frames);  break;
    case DECODE_XM:   xm_advance (&h->u.xm,  frames);  break;
    case DECODE_IT:   it_advance (&h->u.it,  frames);  break;
    case DECODE_MIDI: midi_advance(&h->u.midi, frames); break;
    case DECODE_VGM:  vgm_advance (&h->u.vgm,  frames); break;
    default: break;
    }
}

u8 decode_progress(const decode_handle_t *h)
{
    if (!h) return 0;
    if (h->format == DECODE_MOD && h->u.mod.song_length)
        return (u8)((u16)h->u.mod.current_order * 100 / h->u.mod.song_length);
    if (h->format == DECODE_VGM && h->u.vgm.file_size) {
        /* Cursor / file size approximates progress -- VGM byte density is
         * not perfectly uniform across a track (data blocks vs. dense
         * register-write runs), but this is good enough for a UI bar. */
        u32 pos = h->u.vgm.cursor;
        if (pos > h->u.vgm.file_size) pos = h->u.vgm.file_size;
        return (u8)((u32)pos * 100UL / h->u.vgm.file_size);
    }
    return 0;
}
