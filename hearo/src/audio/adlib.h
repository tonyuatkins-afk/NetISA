/*
 * audio/adlib.h - OPL register write helpers.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_AUDIO_ADLIB_H
#define HEARO_AUDIO_ADLIB_H

#include "../hearo.h"

void adlib_write  (u8 reg, u8 val);   /* OPL2 / OPL3 bank A */
void adlib_write_b(u8 reg, u8 val);   /* OPL3 bank B (no-op on OPL2) */

#endif
