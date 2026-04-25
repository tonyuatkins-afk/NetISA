/*
 * math/bipartite.h - Bipartite table sin/cos/log/exp.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Two precomputed tables per function. Result = A[hi] + B[lo].
 * ~99% of full precision in three loads and an add. Total memory ~4KB.
 */
#ifndef HEARO_BIPARTITE_H
#define HEARO_BIPARTITE_H

#include "../hearo.h"

s16 bipartite_sin(u16 angle);   /* 0..65535 == 0..2pi, returns Q15 */
s16 bipartite_cos(u16 angle);
s16 bipartite_log2(u16 x);      /* x in [1..65535], returns Q8 in [0..16) */
u16 bipartite_exp2(s16 q8);     /* exp2 of Q8 fixed point; returns u16 */

#endif
