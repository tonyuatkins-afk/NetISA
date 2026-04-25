/*
 * platform/timer.c - Timer wrappers.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * BIOS tick at 0040:006C is a 32-bit counter incrementing at ~18.2 Hz.
 * timer_ticks reads it under interrupt-disabled to avoid a torn read across
 * the low/high words. Arithmetic on the result MUST be unsigned to handle
 * the 24h rollover at tick 1573040 without surprises.
 */
#include "timer.h"
#include <dos.h>
#include <i86.h>

u32 timer_ticks(void)
{
    u32 t;
    _disable();
    t = *((volatile u32 __far *)0x0040006CL);
    _enable();
    return t;
}

u32 timer_ms(void)
{
    /* Convert BIOS ticks to ms: ticks * 1000 / 18.2065. Approximate with 55
     * to keep the multiply in u32 on slower CPUs. A few percent error is
     * acceptable for UI purposes. */
    return timer_ticks() * 55UL;
}

void timer_delay_ms(u16 ms)
{
    u32 start = timer_ms();
    while (timer_ms() - start < ms) {}
}
