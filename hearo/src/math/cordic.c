/*
 * math/cordic.c - Adaptive precision CORDIC, fixed-point 16.16.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Standard rotation mode CORDIC. Iteration count is supplied per call so the
 * visualiser can dial precision against frame budget. 24 iterations gives ~24
 * bits of mantissa; 8 iterations gives ~8 bits, plenty for 320x200 plasma.
 *
 * Angle is Q16.16 radians. Output is also Q16.16 in [-1.0 .. +1.0]. The K
 * scale (~0.6072529) is folded into the seed cosine.
 */
#include "cordic.h"

/* atan(2^-i) in Q16.16, for i = 0..23 */
static const s32 atan_table[24] = {
    51472,  30386,  16055,   8150,   4091,   2047,   1024,    512,
      256,    128,     64,     32,     16,      8,      4,      2,
        1,      0,      0,      0,      0,      0,      0,      0
};

/* K * 2^16, the cumulative shrinkage of unrolled CORDIC at 24 iter */
#define CORDIC_K_Q16 39797L  /* 0.6072529350088813 * 65536 */

#define PI_Q16        205887L /* pi * 65536 */
#define HALF_PI_Q16   102944L /* pi/2 * 65536 */

static s32 normalize(s32 a)
{
    while (a >  PI_Q16) a -= 2L * PI_Q16;
    while (a < -PI_Q16) a += 2L * PI_Q16;
    return a;
}

void cordic_sincos(s32 angle_q16, int iters, s32 *out_sin, s32 *out_cos)
{
    s32 x, y, z;
    int i;
    int negate = 0;
    if (iters < 1) iters = 1;
    if (iters > 24) iters = 24;
    angle_q16 = normalize(angle_q16);
    /* Reflect into [-pi/2, +pi/2] */
    if (angle_q16 > HALF_PI_Q16) {
        angle_q16 = PI_Q16 - angle_q16;
        negate = 1;
    } else if (angle_q16 < -HALF_PI_Q16) {
        angle_q16 = -PI_Q16 - angle_q16;
        negate = 1;
    }

    x = CORDIC_K_Q16;
    y = 0;
    z = angle_q16;

    for (i = 0; i < iters; i++) {
        s32 dx, dy;
        if (z >= 0) {
            dx = -(y >> i);
            dy =  (x >> i);
            z -= atan_table[i];
        } else {
            dx =  (y >> i);
            dy = -(x >> i);
            z += atan_table[i];
        }
        x += dx;
        y += dy;
    }
    if (negate) x = -x;
    if (out_sin) *out_sin = y;
    if (out_cos) *out_cos = x;
}

s32 cordic_sin(s32 angle_q16, int iters)
{
    s32 s, c;
    cordic_sincos(angle_q16, iters, &s, &c);
    return s;
}

s32 cordic_cos(s32 angle_q16, int iters)
{
    s32 s, c;
    cordic_sincos(angle_q16, iters, &s, &c);
    return c;
}

u32 cordic_bench(int iters)
{
    /* A coarse busy loop for relative comparison. The 1024 is chosen so even
     * a 286 finishes the bench in <1 second. */
    u32 work = 0;
    s32 s, c;
    s32 angle = 0;
    u32 i;
    for (i = 0; i < 1024UL; i++) {
        cordic_sincos(angle, iters, &s, &c);
        work ^= (u32)s ^ (u32)c;
        angle += 411L; /* ~0.0063 radians per step */
    }
    return work;
}
