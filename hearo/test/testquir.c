/*
 * test/testquir.c - Software quire vs naive accumulation.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Generates 1000 (a, b) pairs with deliberately ill conditioned magnitudes,
 * sums a*b naively into an s32 and into a quire, then prints both.
 */
#include "../src/hearo.h"
#include "../src/math/quire.h"

#include <stdio.h>
#include <stdlib.h>

#define N 1000

int main(void)
{
    quire_t q;
    s32 naive = 0;
    s32 a, b;
    int i;
    quire_clear(&q);
    srand(42);
    printf("HEARO TESTQUIR %s\n", HEARO_VER_STRING);
    printf("================\n");
    for (i = 0; i < N; i++) {
        if (i % 2 == 0) {
            a = (s32)(rand() % 30000) + 1;
            b = (s32)(rand() % 30000) + 1;
        } else {
            a = -((s32)(rand() % 30000) + 1);
            b = (s32)(rand() % 30000) + 1;
        }
        naive += (s32)((s64)a * (s64)b);
        quire_mac(&q, a, b);
    }
    {
        s32 q32 = quire_round(&q, 0);
        printf("naive 32-bit sum:  %ld\n", (long)naive);
        printf("quire low word:    %ld\n", (long)q32);
        printf("quire next word:   %lu\n", (unsigned long)q.w[1]);
        printf("quire nonzero:     %s\n", quire_nonzero(&q) ? "yes" : "no");
    }
    return 0;
}
