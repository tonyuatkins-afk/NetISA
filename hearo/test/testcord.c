/*
 * test/testcord.c - CORDIC accuracy/iteration sweep.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Walks iteration counts from 8 to 24 in steps of 4, computes sin/cos at 64
 * sample points using CORDIC, and compares against a high-resolution
 * reference (libm sin/cos converted to Q16.16). Reports max and average
 * absolute error in fractional units.
 */
#include "../src/hearo.h"
#include "../src/math/cordic.h"

#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#define SAMPLES 64
#define Q16_ONE 65536L

static s32 ref_sin_q16(double rad)  { return (s32)floor(sin(rad) * Q16_ONE + 0.5); }
static s32 ref_cos_q16(double rad)  { return (s32)floor(cos(rad) * Q16_ONE + 0.5); }

int main(void)
{
    int iters[] = { 8, 12, 16, 20, 24 };
    int k;
    printf("HEARO TESTCORD %s\n", HEARO_VER_STRING);
    printf("================\n");
    printf("CORDIC accuracy sweep over %d sample points (Q16.16)\n\n", SAMPLES);
    printf("Iter   max-err-sin   avg-err-sin   max-err-cos   avg-err-cos\n");
    printf("----   -----------   -----------   -----------   -----------\n");
    for (k = 0; k < (int)(sizeof(iters)/sizeof(iters[0])); k++) {
        long max_s = 0, max_c = 0; long sum_s = 0, sum_c = 0;
        int i;
        for (i = 0; i < SAMPLES; i++) {
            double rad = (i - SAMPLES / 2) * (3.14159265 / (SAMPLES / 2));
            s32 ang_q16 = (s32)floor(rad * Q16_ONE + 0.5);
            s32 cs, cc;
            long es, ec;
            cordic_sincos(ang_q16, iters[k], &cs, &cc);
            es = labs((long)cs - ref_sin_q16(rad));
            ec = labs((long)cc - ref_cos_q16(rad));
            if (es > max_s) max_s = es;
            if (ec > max_c) max_c = ec;
            sum_s += es; sum_c += ec;
        }
        printf("%4d   %10ld    %10ld    %10ld    %10ld\n",
               iters[k], max_s, sum_s / SAMPLES, max_c, sum_c / SAMPLES);
    }
    return 0;
}
