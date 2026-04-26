/*
 * audio/sb.c - Sound Blaster family driver (SB1.x, SB2.0, SBPro, SB16, AWE).
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Notes:
 *   - All variants share the DSP command interface at base+06 (reset),
 *     base+0A (read data), base+0C (write data / status), base+0E (read
 *     status / 8-bit ack), base+0F (16-bit ack on DSP v4).
 *   - This driver targets a single auto-init double-buffered transfer driven
 *     by the SB IRQ.  The mixer fills one half while the DSP plays the other.
 *   - Tested target inside DOSBox-X is SB16 stereo 16-bit @ 22050 Hz.  Older
 *     variants are programmed via their canonical command set but only lightly
 *     exercised; that lights up once we have real iron in the loop.
 */
#include "audiodrv.h"
#include "dma.h"
#include "../detect/audio.h"
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DSP_RESET   0x06
#define DSP_READ    0x0A
#define DSP_WRITE   0x0C
#define DSP_RSTATUS 0x0E   /* read-data status / 8-bit ack */
#define DSP_ACK16   0x0F

#define BUF_HALVES         2
#define DEFAULT_BUF_FRAMES 2048   /* per half */

typedef struct {
    u16 base;
    u8  irq;
    u8  dma8;
    u8  dma16;
    u8  dsp_major;
    u8  dsp_minor;
    u8  has_stereo;
    u8  has_16bit;
    u32 rate;
    u8  format;
    audio_callback_t cb;
    void far *buffer;     /* large buffer covering both halves */
    u32  buffer_phys;
    u16  half_frames;
    u16  half_bytes;
    u8   active_half;     /* DSP is currently reading this half */
    void (__interrupt __far *prev_isr)(void);
    u8   prev_pic_mask;
    u8   running;
    u8   force_single_cycle;  /* SB_SINGLECYCLE env: force 0x14 mode + ISR re-arm */
    u8   active_dma;          /* tracks which DMA channel was programmed (8 or 16 bit) */
} sb_state_t;

static sb_state_t S;

/* ========================================================================
 *  Low-level DSP I/O
 * ====================================================================== */

static hbool dsp_reset(u16 base)
{
    int t;
    outp(base + DSP_RESET, 1);
    for (t = 0; t < 100; t++) (void)inp(base + DSP_RESET);
    outp(base + DSP_RESET, 0);
    for (t = 0; t < 1000; t++) {
        if (inp(base + DSP_RSTATUS) & 0x80) {
            if (inp(base + DSP_READ) == 0xAA) return HTRUE;
        }
    }
    return HFALSE;
}

static void dsp_write(u16 base, u8 val)
{
    int t = 0x4000;
    while ((inp(base + DSP_WRITE) & 0x80) && --t) ;
    outp(base + DSP_WRITE, val);
}

static u8 dsp_read(u16 base)
{
    int t = 0x4000;
    while (!(inp(base + DSP_RSTATUS) & 0x80) && --t) ;
    return inp(base + DSP_READ);
}

static void dsp_get_version(u16 base, u8 *maj, u8 *min)
{
    dsp_write(base, 0xE1);
    *maj = dsp_read(base);
    *min = dsp_read(base);
}

static void dsp_set_rate_v4(u16 base, u32 rate)
{
    dsp_write(base, 0x41);                 /* output rate */
    dsp_write(base, (u8)(rate >> 8));
    dsp_write(base, (u8)(rate & 0xFF));
}

static void dsp_set_rate_v2(u16 base, u32 rate)
{
    /* SB Pro / SB 2.0 time constant. The DSP outputs ONE sample per TC
     * tick; for stereo, L and R are alternating samples at the same TC,
     * so callers must pass `rate * 2` for stereo or stereo plays half
     * speed. */
    u32 tc = 256UL - (1000000UL / rate);
    dsp_write(base, 0x40);
    dsp_write(base, (u8)tc);
}

static void dsp_set_block_size(u16 base, u16 frames)
{
    dsp_write(base, 0x48);
    dsp_write(base, (u8)((frames - 1) & 0xFF));
    dsp_write(base, (u8)(((frames - 1) >> 8) & 0xFF));
}

/* ========================================================================
 *  IRQ / PIC helpers
 * ====================================================================== */

static u8 irq_pic_mask_bit(u8 irq)   { return (u8)(1 << (irq & 7)); }
static u16 irq_vector(u8 irq)        { return (irq < 8) ? (8 + irq) : (0x70 + (irq - 8)); }

static void __interrupt __far sb_isr(void)
{
    u8 idle;
    u8 far *halfp;

    /* Acknowledge the interrupt: 8-bit read at +0E, 16-bit read at +0F. */
    if (S.format == AFMT_S16_MONO || S.format == AFMT_S16_STEREO)
        (void)inp(S.base + DSP_ACK16);
    else
        (void)inp(S.base + DSP_RSTATUS);

    /* EOI to PIC immediately so lower-priority IRQs (timer 0, keyboard 1)
     * are not blocked while the audio callback runs.  IF is still cleared
     * because we are in __interrupt, so re-entry on the SB IRQ line is
     * gated by the PIC mask we set at install. */
    if (S.irq >= 8) outp(0xA0, 0x20);
    outp(0x20, 0x20);

    /* Toggle active half and refill the now-idle half. */
    S.active_half ^= 1;
    if (S.cb) {
        idle = S.active_half ^ 1;
        halfp = (u8 far *)S.buffer + ((u32)idle * S.half_bytes);
        S.cb(halfp, S.half_frames, S.format);
    }

    /* SB 1.x is single-cycle: re-arm the next play command for the half we
     * just released so the DSP has something to consume. Same path is used
     * for force_single_cycle quirk mode (SB_SINGLECYCLE env, see sb_init). */
    if ((S.dsp_major < 2 || S.force_single_cycle) && S.cb) {
        dsp_write(S.base, 0x14);
        dsp_write(S.base, (u8)((S.half_bytes - 1) & 0xFF));
        dsp_write(S.base, (u8)((S.half_bytes - 1) >> 8));
    }
}

/* ========================================================================
 *  Driver vtable
 * ====================================================================== */

static hbool sb_init(const hw_profile_t *hw)
{
    char *env;
    memset(&S, 0, sizeof(S));
    S.base      = hw->sb.base    ? hw->sb.base    : 0x220;
    S.irq       = hw->sb.irq     ? hw->sb.irq     : 5;
    S.dma8      = hw->sb.dma_lo  ? hw->sb.dma_lo  : 1;
    S.dma16     = hw->sb.dma_hi  ? hw->sb.dma_hi  : 5;
    S.dsp_major = hw->sb.dsp_major;
    S.dsp_minor = hw->sb.dsp_minor;
    /* SB_SINGLECYCLE=1 in env forces SB 1.x-style 0x14 single-cycle DMA with
     * ISR re-arm. Useful as a diagnostic and as a fallback for chips that
     * detect as SB Pro 2 / SB16 but whose auto-init mode (0x1C / 0x90 / 0xC6)
     * does not actually cycle. The Yamaha YMF715 OPL3-SAx in pure MS-DOS
     * mode (no vendor init utility) demonstrates this: TESTPLAY plays exactly
     * one half-block then halts. Single-cycle mode re-issues 0x14 from the
     * ISR after every block, sidestepping any auto-init implementation gap. */
    env = getenv("SB_SINGLECYCLE");
    S.force_single_cycle = (env && env[0] && env[0] != '0') ? HTRUE : HFALSE;
    if (!dsp_reset(S.base)) return HFALSE;
    if (!S.dsp_major) dsp_get_version(S.base, &S.dsp_major, &S.dsp_minor);
    S.has_stereo = (S.dsp_major >= 3);
    S.has_16bit  = (S.dsp_major >= 4);
    return HTRUE;
}

static void sb_close(void);

static void sb_shutdown(void)
{
    sb_close();
}

static void install_isr(void)
{
    u8 mask;
    S.prev_isr = _dos_getvect(irq_vector(S.irq));
    _dos_setvect(irq_vector(S.irq), sb_isr);
    if (S.irq < 8) {
        mask = inp(0x21);
        S.prev_pic_mask = mask & irq_pic_mask_bit(S.irq);
        outp(0x21, mask & ~irq_pic_mask_bit(S.irq));
    } else {
        mask = inp(0xA1);
        S.prev_pic_mask = mask & irq_pic_mask_bit(S.irq);
        outp(0xA1, mask & ~irq_pic_mask_bit(S.irq));
        /* Cascade IRQ2 must be enabled. */
        outp(0x21, inp(0x21) & ~0x04);
    }
}

static void uninstall_isr(void)
{
    if (!S.prev_isr) return;
    if (S.irq < 8) {
        u8 m = inp(0x21);
        if (S.prev_pic_mask) outp(0x21, m | irq_pic_mask_bit(S.irq));
    } else {
        u8 m = inp(0xA1);
        if (S.prev_pic_mask & 0x7F) outp(0xA1, m | irq_pic_mask_bit(S.irq));
        /* Re-mask cascade IRQ2 if we found it masked at install. */
        if (S.prev_pic_mask & 0x80) outp(0x21, inp(0x21) | 0x04);
    }
    _dos_setvect(irq_vector(S.irq), S.prev_isr);
    S.prev_isr = 0;
}

static hbool sb_open(u32 rate, u8 format, audio_callback_t cb)
{
    u16 total_frames;
    u16 frame_bytes;
    u8  use16, use_stereo;

    if (S.running) return HFALSE;
    if (!cb) return HFALSE;

    /* Clamp format to what this DSP can do. */
    use16      = (format & 2) && S.has_16bit;
    use_stereo = (format & 1) && (S.has_stereo || use16);
    if (use16 && use_stereo) format = AFMT_S16_STEREO;
    else if (use16)          format = AFMT_S16_MONO;
    else if (use_stereo)     format = AFMT_U8_STEREO;
    else                     format = AFMT_U8_MONO;

    S.rate = rate;
    S.format = format;
    S.cb = cb;
    S.half_frames = DEFAULT_BUF_FRAMES;
    frame_bytes = AFMT_FRAME_BYTES(format);
    S.half_bytes = S.half_frames * frame_bytes;
    total_frames = S.half_frames * BUF_HALVES;

    S.buffer = dma_alloc(S.half_bytes * BUF_HALVES, use16, &S.buffer_phys);
    if (!S.buffer) return HFALSE;
    /* Pre-fill both halves with silence-relative encoding. */
    if (use16) memset((u8 far *)S.buffer, 0,    S.half_bytes * BUF_HALVES);
    else       memset((u8 far *)S.buffer, 0x80, S.half_bytes * BUF_HALVES);

    install_isr();

    /* DAC speaker on. SB16 ignores this for output in theory, but DOSBox-X
     * faithfully emulates the gate; without 0xD1 the DSP never advances DMA
     * and the IRQ never fires.  Doslib / OCP / OSDev all set this first. */
    dsp_write(S.base, 0xD1);

    /* Program DMA. SB16 16-bit transfers go on the high (16-bit) channel and
     * count WORDS, not frames. For stereo each frame is 2 samples = 2 words.
     * The low channel handles 8-bit byte-counted transfers. */
    {
        u8  channels   = use_stereo ? 2 : 1;
        u32 nsamples_total = (u32)total_frames * channels;
        if (use16) {
            dma_setup_16bit(S.dma16, S.buffer_phys, (u16)nsamples_total);
            S.active_dma = S.dma16;
        } else {
            dma_setup_8bit (S.dma8,  S.buffer_phys, S.half_bytes * BUF_HALVES);
            S.active_dma = S.dma8;
        }

        /* Program DSP rate. SB16 (v4+) takes the actual Hz on its rate
         * command and handles stereo internally. SB Pro / 2.0 uses the
         * time-constant formula and needs `rate * 2` for stereo because
         * the DSP clocks L and R alternately at the same TC rate. */
        if (S.dsp_major >= 4) {
            dsp_set_rate_v4(S.base, rate);
        } else {
            dsp_set_rate_v2(S.base, use_stereo ? rate * 2 : rate);
        }

        if (S.dsp_major >= 4) {
            u8  cmd  = use16 ? 0xB6 : 0xC6;       /* auto-init output */
            u8  mode = (use_stereo ? 0x20 : 0x00) | (use16 ? 0x10 : 0x00);
            /* DSP length is samples-1 with L and R counted independently. */
            u16 length_param = (u16)(nsamples_total - 1);
            dsp_write(S.base, cmd);
            dsp_write(S.base, mode);
            dsp_write(S.base, (u8)(length_param & 0xFF));
            dsp_write(S.base, (u8)(length_param >> 8));
        } else if (S.dsp_major >= 2 && !S.force_single_cycle) {
            /* SB Pro / SB 2.0 normal-mode time constant range is 0x00..0xD2
             * (covers ~4 kHz to ~22 kHz mono). Above 0xD2, the DSP requires
             * HIGH-SPEED mode entered via command 0x90 (8-bit) or 0x91 (mono
             * input), with TC values up to 0xFF (~46 kHz). 22 kHz STEREO
             * passes rate*2 = 44100 to the TC formula, yielding TC=234,
             * which is well into high-speed territory. Sending 0x1C (normal
             * auto-init) at a high-speed TC plays exactly one block on
             * spec-strict chips like the Yamaha YMF715 (verified on the
             * Toshiba 320CDT, 2026-04-25): the chip honors the TC but the
             * 0x1C command does not actually arm continuous transfer at
             * that rate, so the ISR fires once and DMA stalls.
             *
             * High-speed mode locks the DSP (most commands are ignored
             * until a reset), but our shutdown path always calls
             * dsp_reset() at the end so this is fine.
             *
             * NOTE: high-speed mode REQUIRES the speaker to be on (0xD1),
             * which we already issued above before entering this branch. */
            u32 effective_rate = use_stereo ? rate * 2 : rate;
            u32 tc_check = 256UL - (1000000UL / effective_rate);
            u8  start_cmd = (tc_check > 0xD2UL) ? 0x90 : 0x1C;
            if (use_stereo) {
                /* SBPro mixer: enable stereo output.  Index port = base+04,
                 * data port = base+05.  Register 0x0E bit 1 = stereo. */
                outp(S.base + 0x04, 0x0E);
                outp(S.base + 0x05, inp(S.base + 0x05) | 0x02);
            }
            /* Block size = ONE HALF, not full buffer: DSP fires IRQ at every
             * half boundary so the ISR can refill the idle half.  Sending
             * the full buffer here means IRQ fires only once per wrap, half
             * the data goes stale. */
            dsp_set_block_size(S.base, S.half_bytes);
            dsp_write(S.base, start_cmd);             /* 0x1C normal or 0x90 high-speed */
        } else {
            /* SB 1.x: single-cycle 8-bit. ISR re-arms each half.
             * Also taken when force_single_cycle is set (SB_SINGLECYCLE env)
             * as a quirk fallback for chips whose auto-init mode is broken. */
            if (use_stereo && S.dsp_major >= 3) {
                /* SBPro mixer: enable stereo even in single-cycle mode. */
                outp(S.base + 0x04, 0x0E);
                outp(S.base + 0x05, inp(S.base + 0x05) | 0x02);
            }
            printf("(SB single-cycle mode: 0x14 with ISR re-arm)\n"); fflush(stdout);
            dsp_write(S.base, 0x14);
            dsp_write(S.base, (u8)((S.half_bytes - 1) & 0xFF));
            dsp_write(S.base, (u8)((S.half_bytes - 1) >> 8));
        }
    }

    S.running = 1;
    return HTRUE;
}

static void sb_close(void)
{
    /* sb_close must be safe to call from a partial-init state (e.g. when
     * sb_open allocated the buffer and installed the ISR but the DSP
     * playback command did not actually start, or when called from a
     * Ctrl-Break signal handler during init). The cleanup steps that
     * MUST happen unconditionally are: mask the DMA channel (so the chip
     * can not write garbage into freed low memory after we exit),
     * uninstall_isr (so a pending IRQ can not jump into freed code), and
     * dma_free (give the buffer back to the heap). */
    if (S.running) {
        /* DSP halt. In high-speed mode (0x90), most commands are ignored
         * until DSP_RESET, but sending the halt commands first is harmless
         * and matters for normal-mode 0x1C. */
        if (S.format == AFMT_S16_MONO || S.format == AFMT_S16_STEREO) {
            dsp_write(S.base, 0xD5);
            dsp_write(S.base, 0xDA);                  /* exit 16-bit auto-init */
        } else {
            dsp_write(S.base, 0xD0);
            dsp_write(S.base, 0xDA);                  /* exit 8-bit auto-init */
        }
        /* Speaker off cleanly so subsequent runs do not pick up our DAC bit. */
        dsp_write(S.base, 0xD3);
        dsp_reset(S.base);
        S.running = 0;
    }
    /* Mask the DMA channel we programmed so the chip can not continue to
     * pull bytes from (now-stale) memory after we free the buffer. This is
     * defense against COMMAND.COM "Memory allocation error" wedges seen on
     * the YMF715 when a stuck DSP keeps requesting DMA after our reset. */
    if (S.active_dma) {
        dma_disable(S.active_dma);
        S.active_dma = 0;
    }
    /* Always uninstall ISR if hooked. install_isr leaves S.prev_isr non-NULL. */
    if (S.prev_isr) uninstall_isr();
    /* Always free DMA buffer if allocated. dma_alloc leaves S.buffer non-NULL. */
    if (S.buffer) { dma_free(S.buffer); S.buffer = 0; }
}

static void sb_set_volume(u8 vol)
{
    /* SB16 master volume via mixer register 0x22 (left/right nibbles) or 0x30
     * (left) and 0x31 (right) for finer control.  Older cards do not have a
     * programmable mixer, so we accept the call silently. */
    if (S.dsp_major < 4) return;
    outp(S.base + 0x04, 0x22);
    outp(S.base + 0x05, (u8)((vol & 0xF0) | (vol >> 4)));
}

static void sb_get_caps(audio_caps_t *caps)
{
    caps->name = "Sound Blaster";
    caps->chip = (S.dsp_major >= 4) ? "SB16/AWE" :
                 (S.dsp_major >= 3) ? "SB Pro"   :
                 (S.dsp_major >= 2) ? "SB 2.0"   : "SB 1.x";
    caps->formats = (1u << AFMT_U8_MONO) |
                    (S.has_stereo ? (1u << AFMT_U8_STEREO) : 0) |
                    (S.has_16bit  ? (1u << AFMT_S16_MONO) | (1u << AFMT_S16_STEREO) : 0);
    caps->max_rate = S.has_16bit ? 44100 : (S.has_stereo ? 22050 : 23000);
    caps->max_channels = S.has_stereo ? 2 : 1;
    caps->max_bits     = S.has_16bit ? 16 : 8;
    caps->has_hardware_mix = HFALSE;
    caps->hardware_voices  = 0;
    caps->sample_ram       = 0;
}

const audio_driver_t sb_driver = {
    "sb",
    sb_init, sb_shutdown,
    sb_open, sb_close,
    sb_set_volume, sb_get_caps,
    0, 0, 0
};
