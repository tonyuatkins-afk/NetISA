/*
 * platform/timer.h - BIOS tick and millisecond timer wrappers.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_PLATFORM_TIMER_H
#define HEARO_PLATFORM_TIMER_H

#include "../hearo.h"

u32 timer_ticks(void);          /* BIOS ticks at 18.2 Hz, unsigned for safe wrap math */
u32 timer_ms(void);             /* approximate milliseconds since boot, unsigned */
void timer_delay_ms(u16 ms);

#endif
