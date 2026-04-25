/*
 * test/testbip.c - Bipartite sin/cos accuracy.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Compares bipartite_sin against libm sin at 1024 evenly spaced angles.
 * Prints max and average error in raw Q15 units and as bits.
 */
#include "../src/hearo.h"
#include "../src/math/bipartite.h"

#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#define N 1024

int main(void)
{
    long max_err = 0, sum_err = 0;
    int i;
    printf("HEARO TESTBIP %s\n", HEARO_VER_STRING);
    printf("================\n");
    for (i = 0; i < N; i++) {
        u16 angle = (u16)((u32)i * 64UL);
        double rad = ((double)angle / 65536.0) * 2.0 * 3.14159265358979;
        s16 ref = (s16)floor(sin(rad) * 32767.0 + 0.5);
        s16 got = bipartite_sin(angle);
        long e = labs((long)got - (long)ref);
        if (e > max_err) max_err = e;
        sum_err += e;
    }
    {
        double avg_err = (double)sum_err / (double)N;
        double bits = (max_err > 0) ? log10((double)max_err) / log10(2.0) : 0.0;
        printf("samples:  %d\n", N);
        printf("max err:  %ld (~%.1f bits)\n", max_err, 15.0 - bits);
        printf("avg err:  %.2f\n", avg_err);
    }
    return 0;
}
