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
#include "audiodrv.h"
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdlib.h>
#include <string.h>

#define BUF_FRAMES 1024

typedef struct {
    u32 rate;
    audio_callback_t cb;
    u8 *buf;                 /* double-buffered, U8 mono, BUF_FRAMES*2 bytes */
    u16 read_pos;
    u8  active_half;
    void (__interrupt __far *prev_isr)(void);
    u32 tick_accum;
    u32 bresenham_step;      /* Q0.16 fraction of a BIOS tick per audio sample */
    u8  running;
} pc_state_t;

static pc_state_t P;

static void __interrupt __far pc_isr(void)
{
    u8 sample;

    sample = P.buf[P.read_pos++];
    if (P.read_pos >= BUF_FRAMES * 2) P.read_pos = 0;
    /* Refill the half we just released. */
    if (P.read_pos == 0 || P.read_pos == BUF_FRAMES) {
        u8 idle = (P.read_pos == 0) ? 1 : 0;
        if (P.cb) P.cb(P.buf + (idle * BUF_FRAMES), BUF_FRAMES, AFMT_U8_MONO);
    }
    /* Single-byte LSB write to PIT ch2 in mode 3 is the canonical PC speaker
     * PWM step (Goldplay-era convention). The latched MSB stays at 0. */
    outp(0x42, sample);

    /* Maintain the BIOS 18.2 Hz tick using a Bresenham-style 16.16 accumulator
     * against the true PIT input clock (1193182 Hz). When P.tick_accum
     * overflows the BIOS divisor, chain to the original ISR which itself
     * EOIs IRQ0; otherwise EOI ourselves.  Doing both would double-ack. */
    P.tick_accum += P.bresenham_step;
    if (P.tick_accum >= 0x10000UL) {
        P.tick_accum -= 0x10000UL;
        _chain_intr(P.prev_isr);
    } else {
        outp(0x20, 0x20);
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
    cb(P.buf, BUF_FRAMES, AFMT_U8_MONO);
    cb(P.buf + BUF_FRAMES, BUF_FRAMES, AFMT_U8_MONO);
    /* Enable speaker output via port 61h bits 0 and 1. */
    outp(0x61, inp(0x61) | 0x03);
    /* Channel 2 in mode 0 (terminal-count interrupt), binary. */
    outp(0x43, 0xB0);
    outp(0x42, 0xFF); outp(0x42, 0x00);
    /* Channel 0 at the sample rate. BIOS ticks happen every 65536 PIT counts;
     * audio fires every `divisor` PIT counts. So one audio sample advances
     * the BIOS tick counter by `divisor / 65536` of a tick, which in Q0.16
     * fixed-point is exactly `divisor`. The ISR's accumulator overflows at
     * 0x10000, triggering the chain to the BIOS handler. */
    divisor = (u16)(1193182UL / rate);
    P.bresenham_step = (u32)divisor;
    if (P.bresenham_step == 0) P.bresenham_step = 1;
    outp(0x43, 0x36);
    outp(0x40, divisor & 0xFF);
    outp(0x40, divisor >> 8);
    P.prev_isr = _dos_getvect(0x08);
    _dos_setvect(0x08, pc_isr);
    P.running = 1;
    return HTRUE;
}

static void pc_close(void)
{
    /* pc_close must be safe to call from a partial-init state (same hardening
     * pattern as sb_close). The cleanup steps that MUST happen unconditionally
     * are: restore the timer ISR (so a pending IRQ does not jump into freed
     * code) and free the buffer. Without this, an aborted pc_open leaves
     * INT 0x08 pointing at our pc_isr; the next timer tick after process exit
     * jumps to garbage and wedges the box. */
    if (P.running) {
        /* Restore BIOS 18.2 Hz tick. */
        outp(0x43, 0x36);
        outp(0x40, 0); outp(0x40, 0);
        /* Mute speaker. */
        outp(0x61, inp(0x61) & ~0x03);
        P.running = 0;
    }
    /* Always restore the timer ISR if we hooked it. */
    if (P.prev_isr) {
        _dos_setvect(0x08, P.prev_isr);
        P.prev_isr = 0;
    }
    /* Always free the buffer if allocated. */
    if (P.buf) {
        free(P.buf);
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
