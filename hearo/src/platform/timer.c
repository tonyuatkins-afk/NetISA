/*
 * platform/timer.c - Timer wrappers.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * BIOS tick at 0040:006C is a 32-bit counter incrementing at ~18.2 Hz.
 * timer_ticks reads it volatile-safe; arithmetic on the result MUST be
 * unsigned to handle the 24h rollover at tick 1573040 without surprises.
 */
#include "timer.h"

#ifdef HEARO_NOASM
#include <time.h>
u32 timer_ticks(void) { return (u32)((clock() * 182UL) / (CLOCKS_PER_SEC * 10UL / 1UL)); }
u32 timer_ms(void)    { return (u32)((clock() * 1000UL) / CLOCKS_PER_SEC); }
void timer_delay_ms(u16 ms) {
    clock_t end = clock() + (clock_t)((unsigned long)ms * CLOCKS_PER_SEC / 1000UL);
    while (clock() < end) {}
}
#else
#include <dos.h>
u32 timer_ticks(void)
{
    /* Read BIOS tick from 0040h:006Ch with interrupts off to avoid a torn
     * read across the low/high words. */
    u32 t;
    _disable();
    t = *((volatile u32 __far *)0x0040006CL);
    _enable();
    return t;
}

u32 timer_ms(void)
{
    /* Convert BIOS ticks to ms: ticks * 1000 / 18.2065 ~= ticks * 54925 / 1000.
     * We use the approximation ticks * 55 to avoid 32-bit overflow on slower
     * CPUs; the few percent error is acceptable for UI purposes. */
    return timer_ticks() * 55UL;
}

void timer_delay_ms(u16 ms)
{
    u32 start = timer_ms();
    while (timer_ms() - start < ms) {}
}
#endif
