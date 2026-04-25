/*
 * math/bipartite.c - Bipartite tables for sin/cos/log/exp.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * The tables themselves live in tables.c (so the Watcom linker can put them
 * in their own segment without the code dragging them around).
 *
 * For sin/cos: angle is u16 covering [0, 2pi). Split the upper 8 bits as the
 * coarse index, the lower 8 as the fine index. sin(a) = A_sin[hi] +
 * B_sin[lo], where B is computed at table generation time as the residual.
 *
 * For log2: input is u16 in [1, 65535]. We use a single table; the bipartite
 * structure is degenerate but the API is uniform.
 */
#include "bipartite.h"

extern const s16 bp_sin_a[256];
extern const s16 bp_sin_b[256];
extern const s16 bp_log2_table[256];
extern const u16 bp_exp2_table[256];

s16 bipartite_sin(u16 angle)
{
    return (s16)((s32)bp_sin_a[(u8)(angle >> 8)] + (s32)bp_sin_b[(u8)(angle & 0xFF)]);
}

s16 bipartite_cos(u16 angle)
{
    /* cos(a) = sin(a + pi/2). pi/2 in u16 angle space is 16384. */
    return bipartite_sin((u16)(angle + 16384U));
}

s16 bipartite_log2(u16 x)
{
    if (x == 0) return -32768;
    /* Find the integer log2 (msb position). */
    {
        u8 msb = 0;
        u16 v = x;
        while (v >>= 1) msb++;
        /* Fractional part from the table indexed by the next 8 bits below msb. */
        {
            u8 idx;
            if (msb >= 8) idx = (u8)((x >> (msb - 8)) & 0xFF);
            else          idx = (u8)((x << (8 - msb)) & 0xFF);
            return (s16)((s16)((u16)msb << 8) + (bp_log2_table[idx] - (s16)256));
        }
    }
}

u16 bipartite_exp2(s16 q8)
{
    /* Split into integer and fractional parts. */
    int int_part = q8 >> 8;
    u8 frac = (u8)(q8 & 0xFF);
    u16 base = bp_exp2_table[frac];
    if (int_part >= 16)  return 0xFFFF;
    if (int_part <= -16) return 0;
    if (int_part >= 0) return (u16)((u32)base << int_part);
    return (u16)(base >> (-int_part));
}
