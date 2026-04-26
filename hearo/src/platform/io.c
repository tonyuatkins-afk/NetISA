/*
 * platform/io.c - Cross-segment-safe file IO helpers.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#include "io.h"
#include <dos.h>

#define CHUNK 0x4000        /* well under 64K, leaves headroom for any segment offset */

static void normalize(u32 *phys, u8 far **out)
{
    u16 seg = (u16)((*phys >> 4) & 0xFFFF);
    u16 off = (u16)(*phys & 0x000F);
    *out = (u8 far *)MK_FP(seg, off);
}

hbool io_read_chunked(FILE *f, void *dst, u32 n)
{
    u32 phys = ((u32)FP_SEG(dst) << 4) + FP_OFF(dst);
    while (n) {
        u16 want = n > CHUNK ? CHUNK : (u16)n;
        u8 far *p;
        normalize(&phys, &p);
        if (fread(p, 1, want, f) != want) return HFALSE;
        phys += want;
        n    -= want;
    }
    return HTRUE;
}

void io_unsigned_to_signed_8(void *dst, u32 n)
{
    u32 phys = ((u32)FP_SEG(dst) << 4) + FP_OFF(dst);
    while (n) {
        u16 chunk = n > CHUNK ? CHUNK : (u16)n;
        u8 far *p;
        u16 i;
        normalize(&phys, &p);
        for (i = 0; i < chunk; i++) p[i] = (u8)(p[i] - 128);
        phys += chunk;
        n    -= chunk;
    }
}
