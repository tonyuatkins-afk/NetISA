/*
 * math/cordic.h - Adaptive precision CORDIC, fixed-point 16.16.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_CORDIC_H
#define HEARO_CORDIC_H

#include "../hearo.h"

void cordic_sincos(s32 angle_q16, int iters, s32 *out_sin_q16, s32 *out_cos_q16);
s32  cordic_sin(s32 angle_q16, int iters);
s32  cordic_cos(s32 angle_q16, int iters);

/* Run a self-bench: returns relative cycles (loop counts) for `iters` iter. */
u32 cordic_bench(int iters);

#endif
