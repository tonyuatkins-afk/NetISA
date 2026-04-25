/*
 * math/quire.h - 256-bit fixed-point accumulator for exact mixing.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * The Posit Standard 2022 quire concept, adapted for fixed-point. We carry
 * 256 bits of two's-complement state across multiply-and-accumulate
 * operations so that arbitrary mixer pipelines yield bit exact results when
 * compared to the reference.
 */
#ifndef HEARO_QUIRE_H
#define HEARO_QUIRE_H

#include "../hearo.h"

typedef struct { u32 w[8]; } quire_t;

void  quire_clear(quire_t *q);
void  quire_mac(quire_t *q, s32 a, s32 b);   /* q += a * b, full carry */
s32   quire_round(const quire_t *q, int shift);
hbool quire_nonzero(const quire_t *q);

#endif
