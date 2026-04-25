/*
 * dos_clock.h - INT 21h date/time get/set with CMOS write-through.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef CHIME_DOS_CLOCK_H
#define CHIME_DOS_CLOCK_H

#include "chime.h"

/* Read DOS calendar via INT 21h AH=2Ah/2Ch into out (UTC-naive: DOS does not
 * carry timezone). unix_ts is computed assuming the clock represents UTC. */
void dos_clock_get(chime_time_t *out);

/* Set DOS clock via INT 21h AH=2Bh/2Dh, then write CMOS RTC directly via
 * ports 0x70/0x71 as a backstop. The CMOS path is gated on host-build vs
 * Watcom: the host build calls only the C-runtime equivalent. */
void dos_clock_set(const chime_time_t *t);

#endif
