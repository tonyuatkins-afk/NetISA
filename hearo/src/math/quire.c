/*
 * math/quire.c - 256-bit accumulator.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Layout: w[0] is the low word, w[7] is the high word. The high word
 * carries the sign, so subtract operations handle the carry correctly via
 * unsigned arithmetic.
 *
 * 16-bit Watcom build: we compute a*b as 64-bit by hand (32x32 -> 64). On the
 * 32-bit Watcom build we let the compiler produce a real 64-bit multiply.
 */
#include "quire.h"

void quire_clear(quire_t *q)
{
    int i;
    for (i = 0; i < 8; i++) q->w[i] = 0;
}

hbool quire_nonzero(const quire_t *q)
{
    int i;
    for (i = 0; i < 8; i++) if (q->w[i] != 0) return HTRUE;
    return HFALSE;
}

#if defined(__WATCOMC__) && (defined(__386__) || defined(__I86__))
/* Watcom 2.0 supports long long even on 16-bit. */
typedef unsigned long long u64;
typedef long long s64;

static void product_64(s32 a, s32 b, u32 *lo, u32 *hi)
{
    s64 p = (s64)a * (s64)b;
    *lo = (u32)((u64)p & 0xFFFFFFFFULL);
    *hi = (u32)((u64)p >> 32);
}
#else
/* Portable: 16x16 -> 32, four pieces, recombine with carries. */
static void product_64(s32 a, s32 b, u32 *lo, u32 *hi)
{
    int neg = 0;
    u32 ua, ub, a_lo, a_hi, b_lo, b_hi;
    u32 ll, lh, hl, hh;
    u32 mid;
    u32 carry = 0;
    if (a < 0) { ua = (u32)(-a); neg ^= 1; } else ua = (u32)a;
    if (b < 0) { ub = (u32)(-b); neg ^= 1; } else ub = (u32)b;

    a_lo = ua & 0xFFFFu;
    a_hi = (ua >> 16) & 0xFFFFu;
    b_lo = ub & 0xFFFFu;
    b_hi = (ub >> 16) & 0xFFFFu;

    ll = a_lo * b_lo;
    lh = a_lo * b_hi;
    hl = a_hi * b_lo;
    hh = a_hi * b_hi;

    mid = (ll >> 16) + (lh & 0xFFFFu) + (hl & 0xFFFFu);
    *lo = (ll & 0xFFFFu) | (mid << 16);
    carry = mid >> 16;
    *hi = hh + (lh >> 16) + (hl >> 16) + carry;

    if (neg) {
        *lo = ~(*lo) + 1;
        *hi = ~(*hi) + (*lo == 0 ? 1u : 0u);
    }
}
#endif

void quire_mac(quire_t *q, s32 a, s32 b)
{
    u32 lo, hi;
    u32 sign_extend;
    int i;
    u32 carry;
    product_64(a, b, &lo, &hi);

    /* Sign-extend the 64-bit product into all 8 words. */
    sign_extend = (hi & 0x80000000UL) ? 0xFFFFFFFFUL : 0;

    /* Add into w[0..7] with carry propagation. */
    {
        u32 sum = q->w[0] + lo;
        carry = (sum < q->w[0]) ? 1u : 0u;
        q->w[0] = sum;
    }
    {
        u32 sum = q->w[1] + hi;
        u32 c2  = (sum < q->w[1]) ? 1u : 0u;
        sum += carry;
        c2  += (sum < carry) ? 1u : 0u;
        carry = c2;
        q->w[1] = sum;
    }
    for (i = 2; i < 8; i++) {
        u32 sum = q->w[i] + sign_extend;
        u32 c2 = (sum < q->w[i]) ? 1u : 0u;
        sum += carry;
        c2 += (sum < carry) ? 1u : 0u;
        carry = c2;
        q->w[i] = sum;
    }
}

s32 quire_round(const quire_t *q, int shift)
{
    /* Naive: extract bits [shift..shift+31] from the 256-bit state. The
     * caller selects the binary point by choosing shift. */
    int word = shift >> 5;
    int bit  = shift & 31;
    u32 lo, hi;
    if (word < 0) word = 0;
    if (word > 7) word = 7;
    lo = q->w[word];
    hi = (word + 1 < 8) ? q->w[word + 1] : ((q->w[7] & 0x80000000UL) ? 0xFFFFFFFFUL : 0);
    if (bit == 0) return (s32)lo;
    return (s32)((lo >> bit) | (hi << (32 - bit)));
}
