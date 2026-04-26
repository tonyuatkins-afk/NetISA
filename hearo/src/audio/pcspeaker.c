/*
 * audio/pcspeaker.c - PC speaker RealSound PWM driver.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Programs PIT channel 2 in mode 0 (interrupt-on-terminal-count) and uses
 * the resulting count as a PWM duty cycle.  Channel 0's interrupt fires at
 * the sample rate; the ISR pushes one byte per tick into channel 2's count
 * register and chains to the original 18.2 Hz handler when the BIOS counter
 * is due to advance.
 *
 * Quality is forgiving on a 486+ at ~18 kHz, dreadful on slow XTs.
 * The mixer asks for U8_MONO output for this driver.
 */

/* Disable Watcom's stack-overflow probe for this whole TU. pc_isr enters
 * from an INT 8 hardware interrupt; the probe at function entry can route
 * to a runtime helper that calls INT 21h on older Watcom builds, which is
 * unsafe from ISR context. */
#pragma off (check_stack)

#include "audiodrv.h"
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdlib.h>
#include <string.h>

#define BUF_FRAMES 1024

typedef struct {
    u32 rate;
    volatile audio_callback_t cb;
    volatile u8 *buf;        /* double-buffered, U8 mono, BUF_FRAMES*2 bytes */
    volatile u16 read_pos;
    volatile u8  active_half;
    void (__interrupt __far *prev_isr)(void);
    volatile u32 tick_accum;
    u32 bresenham_step;      /* Q0.16 fraction of a BIOS tick per audio sample */
    volatile u8  running;
    u8  pit_programmed;      /* set once channel 0 has been retuned in pc_open */
    /* Deferred-work flag: ISR sets to (idle_half + 1) when read_pos crosses a
     * half boundary and the half named (value - 1) needs a refill. Foreground
     * pc_pump() reads, clears, and runs the callback. Encoded as nonzero=
     * pending so a foreground reader can use a single atomic byte read.
     *   0 = no refill pending
     *   1 = half 0 needs refill (read_pos just hit BUF_FRAMES, half 0 idle)
     *   2 = half 1 needs refill (read_pos just hit 0,        half 1 idle) */
    volatile u8  refill_pending;
} pc_state_t;

static pc_state_t P;

static void __interrupt __far pc_isr(void)
{
    u8 sample;
    volatile u8 *buf;

    /* If a stale tick fires after pc_close cleared running, just EOI and
     * leave. P.buf may already be freed at that point. */
    if (!P.running || !P.buf) {
        outp(0x20, 0x20);
        return;
    }

    buf = P.buf;
    sample = buf[P.read_pos++];
    if (P.read_pos >= BUF_FRAMES * 2) P.read_pos = 0;

    /* Deferred refill: when read_pos crosses a half boundary, the half it
     * just released (the IDLE half) needs new data. Set a flag for the
     * foreground pump to consume, but DO NOT call cb directly here. The
     * mixer_render callback can take tens of milliseconds on a 386 to
     * render BUF_FRAMES=1024 samples; running it inside the ISR starves
     * the foreground for that whole window every ~57 ms at 18 kHz.
     * Setting a flag is an O(1) byte write.
     *
     * The flag encodes which half is idle (1 = half 0, 2 = half 1) so a
     * foreground that pumps slower than the audio rate can refill the
     * correct half without re-deriving it from read_pos (which has
     * already advanced past the boundary by the time pump runs).
     *
     * If the foreground does not pump in time, the ISR keeps reading
     * the OLD samples (the pre-fill from pc_open) and the audio stutters
     * but never crashes. Better than the previous behavior of starving
     * the foreground hard.
     *
     * REQUIRES CALLER COOPERATION: testplay.c and hearo.c main loops
     * MUST call pc_pump() periodically (every ~25 ms is enough for the
     * default BUF_FRAMES=1024 at 18 kHz). Without a pump, the speaker
     * loops the silence-prefill forever. The driver continues to function
     * but produces no audio. Tracked in project_hearo_ui_unwired. */
    if (P.read_pos == BUF_FRAMES) {
        P.refill_pending = 1;          /* half 0 idle */
    } else if (P.read_pos == 0) {
        P.refill_pending = 2;          /* half 1 idle */
    }

    /* Single-byte LSB write to PIT ch2 in mode 3 is the canonical PC speaker
     * PWM step (Goldplay-era convention). The latched MSB stays at 0. */
    outp(0x42, sample);

    /* Maintain the BIOS 18.2 Hz tick using a Bresenham-style 16.16 accumulator
     * against the true PIT input clock (1193182 Hz). When P.tick_accum
     * overflows the BIOS divisor, chain to the original ISR which itself
     * EOIs IRQ0; otherwise EOI ourselves.  Doing both would double-ack.
     * The Bresenham gate already keeps the chain rate at ~18.2 Hz, not at
     * the audio rate, so any TSR-hooked INT 1Ch handlers run at the cadence
     * they expect.
     *
     * Other known foreground-stall sources unrelated to this driver: large
     * mouse driver hooks (CTMOUSE2 at high resolution), Microsoft NETBIOS
     * shim, some keyboard layout TSRs chained off INT 8 / INT 1Ch. */
    P.tick_accum += P.bresenham_step;
    if (P.tick_accum >= 0x10000UL) {
        P.tick_accum -= 0x10000UL;
        _chain_intr(P.prev_isr);
    } else {
        outp(0x20, 0x20);
    }
}

/* Foreground pump: callers (testplay.c main loop, hearo.c playback loop)
 * call this from any non-ISR context to drain the refill flag and run
 * the mixer callback for the half that the ISR has named idle. Safe to
 * call frequently; if no refill is pending it returns immediately.
 *
 * The flag read/clear is bracketed in cli/sti so an IRQ can not set the
 * flag again between the read and the clear, which would lose a refill
 * notification. Snapshotting cb and buf inside the bracket also makes the
 * teardown race tight: pc_close clears running and cb under cli, so a
 * pump that opens the bracket after pc_close ran sees the cleared state
 * and returns without touching the freed buffer. */
void pc_pump(void)
{
    u8 pending;
    audio_callback_t cb;
    volatile u8 *buf;

    _disable();
    pending = P.refill_pending;
    P.refill_pending = 0;
    cb  = P.cb;
    buf = P.buf;
    if (!P.running || !cb || !buf) {
        _enable();
        return;
    }
    _enable();

    if (pending == 1) {
        cb((u8 *)buf + 0,           BUF_FRAMES, AFMT_U8_MONO);
    } else if (pending == 2) {
        cb((u8 *)buf + BUF_FRAMES,  BUF_FRAMES, AFMT_U8_MONO);
    }
}

static hbool pc_init(const hw_profile_t *hw) { (void)hw; memset(&P, 0, sizeof(P)); return HTRUE; }
static void  pc_shutdown(void);

static hbool pc_open(u32 rate, u8 format, audio_callback_t cb)
{
    u16 divisor;
    (void)format;
    if (P.running || !cb) return HFALSE;
    /* Cap rate at what the slowest realistic target can serve per-sample.
     * 286-class iron taps out around 7 kHz; 386SX around 10 kHz; 386DX
     * around 16 kHz; 486+ can hold 22 kHz cleanly. We pick a conservative
     * 18 kHz ceiling unless detection upgraded us. */
    if (rate > 18000) rate = 18000;
    if (!P.buf) P.buf = (u8 *)malloc(BUF_FRAMES * 2);
    if (!P.buf) return HFALSE;
    P.rate = rate;
    P.cb = cb;
    /* Pre-fill once so the first ticks aren't garbage. */
    cb((u8 *)P.buf, BUF_FRAMES, AFMT_U8_MONO);
    cb((u8 *)P.buf + BUF_FRAMES, BUF_FRAMES, AFMT_U8_MONO);
    /* Enable speaker output via port 61h bits 0 and 1. */
    outp(0x61, inp(0x61) | 0x03);
    /* Channel 2: square-wave mode 3, LSB-only access (control word 0x96).
     * The ISR writes one byte per sample to port 0x42; LSB-only access lets
     * that single outp atomically reload the count without an MSB byte that
     * would otherwise be expected in 0xB6 (LSB+MSB) access mode. The latched
     * MSB starts at 0 and stays 0; the audible step size is the LSB delta. */
    _disable();
    outp(0x43, 0x96);
    outp(0x42, 0x00);
    _enable();
    /* Channel 0 at the sample rate. BIOS ticks happen every 65536 PIT counts;
     * audio fires every `divisor` PIT counts. So one audio sample advances
     * the BIOS tick counter by `divisor / 65536` of a tick, which in Q0.16
     * fixed-point is exactly `divisor`. The ISR's accumulator overflows at
     * 0x10000, triggering the chain to the BIOS handler. */
    divisor = (u16)(1193182UL / rate);
    P.bresenham_step = (u32)divisor;
    if (P.bresenham_step == 0) P.bresenham_step = 1;
    /* PIT mode word + two count bytes must NOT be split by an interrupt:
     * a higher-priority IRQ firing between the mode write and the second
     * count byte leaves the latch half-loaded and a mid-reprogram read
     * returns garbage. cli covers the whole sequence. */
    _disable();
    outp(0x43, 0x36);
    outp(0x40, divisor & 0xFF);
    outp(0x40, divisor >> 8);
    _enable();
    P.pit_programmed = 1;
    P.prev_isr = _dos_getvect(0x08);
    _dos_setvect(0x08, pc_isr);
    P.running = 1;
    return HTRUE;
}

static void pc_close(void)
{
    /* Teardown order: a misordered close can leave INT 8 vectored at our
     * pc_isr while the buffer is freed, or restore the PIT divisor while
     * the vector still points at us. The safe order is:
     *   1) Mark not-running so any tick that does fire bails out of the
     *      ISR fast path before touching P.buf or P.cb.
     *   2) Mask IRQ0 at the PIC so no tick fires during teardown at all.
     *   3) Restore the original INT 8 vector.
     *   4) Reprogram PIT channel 0 back to BIOS 18.2 Hz.
     *   5) Mute speaker.
     *   6) Unmask IRQ0 so the BIOS keeps timing.
     *   7) Free the buffer LAST, after every path that could read it is
     *      shut down.
     * pc_close must be safe from a partial-init state; each step gates on
     * whether that resource was actually claimed. */

    /* Steps 1, 2, 3 must be atomic w.r.t. an INT 8 tick. Without cli, an
     * IRQ0 firing between running=0 (step 1) and the vector restore (step 3)
     * lands inside pc_isr after we already cleared P.cb but before INT 8
     * is re-pointed at the BIOS handler; pc_isr would EOI without chaining
     * to the BIOS, dropping a tick. Worse, an IRQ0 that fires between the
     * PIC mask write (step 2) and the vector swap (step 3) can latch a
     * pending edge that the unmask in step 6 below releases into a vector
     * that has just been restored to the BIOS handler, but only after our
     * pc_isr has already executed once on stale state. cli around the trio
     * makes the running=0 / mask / vector-restore sequence appear atomic
     * to the PIC. _enable() is restored after step 3. */
    _disable();

    /* Step 1. */
    P.running = 0;
    P.cb      = 0;

    /* Step 2: only mask IRQ0 if we actually hooked the vector; otherwise
     * we'd potentially mask a line owned by code that ran before us. */
    if (P.prev_isr) {
        outp(0x21, inp(0x21) | 0x01);
    }

    /* Step 3. */
    if (P.prev_isr) {
        _dos_setvect(0x08, P.prev_isr);
        P.prev_isr = 0;
    }

    _enable();

    /* Step 4: restore PIT channel 0 to BIOS 18.2 Hz (divisor 0 = 65536).
     * Skip if we never reprogrammed it, to avoid clobbering a configuration
     * set by something else when sb open fell through to us before PIT setup.
     * cli around the three-byte PIT sequence: see pc_open for the same
     * reason. The IRQ0 mask above protects against a recursive timer tick;
     * cli protects against any higher-priority line firing inside the
     * mode-word / count-low / count-high triple. */
    if (P.pit_programmed) {
        _disable();
        outp(0x43, 0x36);
        outp(0x40, 0); outp(0x40, 0);
        _enable();
        P.pit_programmed = 0;
    }

    /* Step 5: mute speaker, then restore PIT channel 2 to a known state so
     * the next program (or a re-open) does not inherit our LSB-only mode-3
     * configuration. Gate off via port 0x61 first, then reprogram ch2 with
     * mode 3 + LSB+MSB access (0xB6) and load count 0 to leave the latch in
     * the standard BIOS shape. cli around the triple keeps it atomic. */
    outp(0x61, inp(0x61) & ~0x03);
    _disable();
    outp(0x43, 0xB6);
    outp(0x42, 0); outp(0x42, 0);
    _enable();

    /* Step 6: unmask IRQ0 unconditionally. If we never masked it (no hook
     * was installed), this just leaves it as-is. The BIOS handler is back
     * in the vector table, so the system tick resumes. */
    outp(0x21, inp(0x21) & ~0x01);

    /* Step 7. */
    if (P.buf) {
        free((void *)P.buf);
        P.buf = 0;
    }
}

static void pc_shutdown(void) { pc_close(); }
static void pc_volume(u8 v) { (void)v; }
static void pc_caps(audio_caps_t *c)
{
    c->name = "PC Speaker"; c->chip = "PIT ch2 PWM";
    c->formats = (1u << AFMT_U8_MONO);
    c->max_rate = 18000; c->max_channels = 1; c->max_bits = 8;
    c->has_hardware_mix = HFALSE;
    c->hardware_voices = 0; c->sample_ram = 0;
}

const audio_driver_t pcspeaker_driver = {
    "pcspeaker", pc_init, pc_shutdown, pc_open, pc_close, pc_volume, pc_caps, 0, 0, 0
};
