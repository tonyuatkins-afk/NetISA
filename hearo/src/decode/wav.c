/*
 * decode/wav.c - RIFF/WAVE PCM loader.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Supports format tag 1 (PCM) only: 8-bit unsigned and 16-bit signed,
 * mono and stereo, any sample rate the host can program.
 */
#pragma off (check_stack)  /* see Makefile CF16_ISR -- belt-and-braces */
#include "wav.h"
#include "../platform/io.h"
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u32 read_le32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static u16 read_le16(const u8 *p) { return (u16)p[0] | ((u16)p[1] << 8); }

hbool wav_load(const char *filename, wav_file_t *wav)
{
    FILE *f;
    u8 hdr[12];
    u8 chunk[8];
    u8 fmt[16];
    u32 sz;

    if (!filename || !wav) return HFALSE;
    memset(wav, 0, sizeof(*wav));
    f = fopen(filename, "rb");
    if (!f) return HFALSE;
    if (fread(hdr, 1, 12, f) != 12) { fclose(f); return HFALSE; }
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) { fclose(f); return HFALSE; }

    /* Walk chunks until we find "fmt " then "data". */
    while (fread(chunk, 1, 8, f) == 8) {
        sz = read_le32(chunk + 4);
        if (memcmp(chunk, "fmt ", 4) == 0) {
            u32 to_read = sz < 16 ? sz : 16;
            if (fread(fmt, 1, to_read, f) != to_read) { fclose(f); return HFALSE; }
            if (sz > 16) fseek(f, (long)(sz - 16), SEEK_CUR);
            wav->format          = read_le16(fmt + 0);
            wav->channels        = read_le16(fmt + 2);
            wav->sample_rate     = read_le32(fmt + 4);
            wav->byte_rate       = read_le32(fmt + 8);
            wav->block_align     = read_le16(fmt + 12);
            wav->bits_per_sample = read_le16(fmt + 14);
        } else if (memcmp(chunk, "data", 4) == 0) {
            wav->data_length = sz;
            wav->data = _fmalloc(sz);
            if (!wav->data) { fclose(f); return HFALSE; }
            /* io_read_chunked re-normalizes the destination pointer between
             * 16K chunks so reads larger than 64K do not wrap the offset. */
            if (!io_read_chunked(f, wav->data, sz)) {
                wav_free(wav); fclose(f); return HFALSE;
            }
            break;
        } else {
            fseek(f, (long)sz, SEEK_CUR);
        }
    }
    fclose(f);
    if (wav->format != 1) { wav_free(wav); return HFALSE; }      /* PCM only */
    if (wav->channels < 1 || wav->channels > 2) { wav_free(wav); return HFALSE; }
    if (wav->bits_per_sample != 8 && wav->bits_per_sample != 16) { wav_free(wav); return HFALSE; }
    if (!wav->data) { return HFALSE; }
    wav->num_frames = wav->data_length / (wav->channels * (wav->bits_per_sample / 8));
    /* Convert 8-bit unsigned to signed in place; helper handles >64K spans. */
    if (wav->bits_per_sample == 8) {
        io_unsigned_to_signed_8(wav->data, wav->data_length);
    }
    return HTRUE;
}

void wav_free(wav_file_t *wav)
{
    if (!wav) return;
    if (wav->data)              _ffree(wav->data);
    if (wav->deinterleaved_left)  _ffree(wav->deinterleaved_left);
    if (wav->deinterleaved_right) _ffree(wav->deinterleaved_right);
    memset(wav, 0, sizeof(*wav));
}
