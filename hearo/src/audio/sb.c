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

/* Disable Watcom's stack-overflow probe for this whole TU. The probe is a
 * call to __STK at every C function entry that subtracts the local frame
 * size from SP and traps if it would underflow. Inside an ISR (sb_isr) the
 * probe can route to a runtime helper that calls INT 21h on older Watcom
 * runtimes, which is unsafe from interrupt context (DOS is not reentrant).
 * Foreground code in this TU is short enough that we accept losing the
 * stack check here in exchange for ISR safety. */
#pragma off (check_stack)

#include "audiodrv.h"
#include "dma.h"
#include "wake.h"
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

/* Half-buffer sizing scales linearly with playback rate to hold a roughly
 * constant wall-clock time per half (~93 ms at the reference). Without this,
 * a fixed 2048-frame half-buffer covers 46 ms at 44100 Hz, short enough that
 * a 50 ms foreground hiccup (file IO, mixer cycle on a 386) underruns. At
 * 11025 Hz the same fixed buffer covers 186 ms, well over what is needed
 * and twice the memory required. Reference is 22050 Hz, the rate HEARO has
 * been smoke-tested at most heavily in DOSBox-X SB16 stereo s16. Pattern is
 * the same shape as Mpxplay MDma_get_max_pcmoutbufsize (DMAIRQ.C:87) but
 * pegged in frames at HEARO's nominal rate; independent implementation. */
#define BUF_REFERENCE_RATE   22050UL
#define BUF_REFERENCE_FRAMES 2048
#define BUF_MIN_FRAMES         256   /* below this IRQ rate exceeds 30 Hz/half */
#define BUF_MAX_FRAMES        8192   /* keeps total bytes inside dma_alloc cap */

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
    volatile u8  format;
    volatile audio_callback_t cb;
    void far *buffer;     /* large buffer covering both halves */
    u32  buffer_phys;
    u16  half_frames;
    u16  half_bytes;
    volatile u8   active_half;     /* DSP is currently reading this half */
    void (__interrupt __far *prev_isr)(void);
    u8   prev_pic_master;          /* full 0x21 byte saved at install */
    u8   prev_pic_slave;           /* full 0xA1 byte saved at install */
    u8   pic_state_saved;
    volatile u8   running;
    u8   force_single_cycle;  /* SB_SINGLECYCLE env: force 0x14 mode + ISR re-arm */
    u8   active_dma;          /* tracks which DMA channel was programmed (8 or 16 bit) */
    u8   in_high_speed;       /* 0x90 entered: most DSP commands ignored until reset */
    u8   sbpro_mixer_0e_saved;/* set after sb_open snapshotted mixer reg 0x0E */
    u8   sbpro_mixer_0e_prev; /* original value of mixer reg 0x0E to restore on close */
    volatile u8 isr_in_progress;/* set at ISR entry past the !running gate, cleared at exit */
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

/* Returns HTRUE on success, HFALSE if the busy bit never cleared. On a
 * wedged chip (notably the YMF715 in MS-DOS mode without vendor init) the
 * busy bit can stay high indefinitely; the previous unconditional outp
 * after timeout could latch a command into a stuck state machine. */
static hbool dsp_write_to(u16 base, u8 val, unsigned int timeout)
{
    unsigned int t = timeout;
    while ((inp(base + DSP_WRITE) & 0x80) && --t) ;
    if (t == 0) return HFALSE;
    outp(base + DSP_WRITE, val);
    return HTRUE;
}

/* Returns HTRUE on success, HFALSE on timeout. Callers in the start path
 * (sb_open, dsp_get_version) must check this before relying on a follow-up
 * dsp_read; a timed-out write means the chip never accepted the command,
 * so any subsequent read is reading whatever stale byte the read-data
 * register still holds. */
static hbool dsp_write(u16 base, u8 val)
{
    return dsp_write_to(base, val, 0x4000);
}

static u8 dsp_read(u16 base)
{
    int t = 0x4000;
    while (!(inp(base + DSP_RSTATUS) & 0x80) && --t) ;
    return inp(base + DSP_READ);
}

/* Returns HTRUE on success and writes the version into *maj / *min.
 * Returns HFALSE if the 0xE1 query write timed out, in which case *maj
 * and *min are LEFT UNTOUCHED so the caller can fall back to the version
 * the detect layer (BLASTER parse, DSP probe) supplied. The previous
 * unconditional read of two stale bytes from a timed-out write produced
 * phantom SB16 detection on chips that briefly glitched during init,
 * because dsp_read returns whatever inp(...DSP_READ) yields after a
 * timeout (often 0xFF, parsed as DSP v255.x and treated as SB16+). */
static hbool dsp_get_version(u16 base, u8 *maj, u8 *min)
{
    if (!dsp_write(base, 0xE1)) return HFALSE;
    *maj = dsp_read(base);
    *min = dsp_read(base);
    return HTRUE;
}

/* Returns HTRUE iff all three command bytes (0x41 + rate hi + rate lo)
 * were accepted by the DSP. Mirrors dsp_set_rate_v2's contract: a partial
 * acceptance leaves the DSP expecting more bytes, so the caller must
 * treat any failure as fatal and tear down the open. */
static hbool dsp_set_rate_v4(u16 base, u32 rate)
{
    if (!dsp_write(base, 0x41)) return HFALSE;
    if (!dsp_write(base, (u8)(rate >> 8))) return HFALSE;
    if (!dsp_write(base, (u8)(rate & 0xFF))) return HFALSE;
    return HTRUE;
}

/* Clamp range derived from the SB Pro 2 / SB 2.0 TC formula. rate=0 would
 * trap on divide; rate>1000000 wraps tc; rate<3906 underflows tc. The upper
 * bound covers stereo 22050 Hz (callers pass rate*2 = 44100 for that). */
#define SB_V2_RATE_MIN 4000UL
#define SB_V2_RATE_MAX 45000UL

static hbool dsp_set_rate_v2(u16 base, u32 rate)
{
    u32 tc;
    if (rate < SB_V2_RATE_MIN || rate > SB_V2_RATE_MAX) return HFALSE;
    tc = 256UL - (1000000UL / rate);
    dsp_write(base, 0x40);
    dsp_write(base, (u8)tc);
    return HTRUE;
}

/* count is in transfer units (bytes for 8-bit DMA, words for 16-bit DMA). */
static void dsp_set_block_count(u16 base, u16 count)
{
    /* count==0 would encode 0xFFFF and program a 64K transfer. Refuse. */
    if (count == 0) return;
    dsp_write(base, 0x48);
    dsp_write(base, (u8)((count - 1) & 0xFF));
    dsp_write(base, (u8)(((count - 1) >> 8) & 0xFF));
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
    audio_callback_t cb;
    u8 mask_bit;
    u16 mask_port;

    /* Snapshot teardown-sensitive state once. sb_close clears running and cb
     * before doing anything that frees memory or resets the DSP, so a stale
     * IRQ that fires mid-shutdown must not touch the DSP and must not call
     * the (possibly freed-by-now) callback or re-arm DMA. */
    if (!S.running) {
        if (S.irq >= 8) outp(0xA0, 0x20);
        outp(0x20, 0x20);
        return;
    }

    /* Mark ourselves in the ISR body BEFORE any DSP I/O. sb_close spins on
     * this flag (with a bounded timeout) before it touches the DSP, so a
     * dsp_write triple in this ISR can not be preempted by a foreground
     * dsp_reset on the same chip. Cleared at every return path below. */
    S.isr_in_progress = 1;

    /* Mask our own IRQ at the PIC for the duration of the ISR body. EOI
     * still happens (so lower-priority lines unblock) but a reentrant SB
     * edge can not preempt while we are mid-dsp_write. The chip latches
     * one pending edge and re-fires after we unmask before IRET. */
    mask_bit  = irq_pic_mask_bit(S.irq);
    mask_port = (S.irq < 8) ? 0x21 : 0xA1;
    outp(mask_port, inp(mask_port) | mask_bit);

    /* Acknowledge the interrupt: 8-bit read at +0E, 16-bit read at +0F. */
    if (S.format == AFMT_S16_MONO || S.format == AFMT_S16_STEREO)
        (void)inp(S.base + DSP_ACK16);
    else
        (void)inp(S.base + DSP_RSTATUS);

    /* EOI to PIC immediately so lower-priority IRQs (timer 0, keyboard 1)
     * are not blocked while the audio callback runs. */
    if (S.irq >= 8) outp(0xA0, 0x20);
    outp(0x20, 0x20);

    cb = S.cb;
    if (!cb) {
        outp(mask_port, inp(mask_port) & ~mask_bit);
        S.isr_in_progress = 0;
        return;
    }

    /* IRQ fires when the DSP finished a block and started the next one. The
     * half DSP just released is the current S.active_half (stale until we
     * toggle below); refill THAT half so it has fresh data when the DSP wraps
     * back around. Toggle active_half AFTER the refill so the field correctly
     * names the half DSP is currently reading once we IRET. Keeping the use
     * of active_half before the toggle (rather than the prior toggle-then-XOR
     * pattern) makes the playhead-vs-refill relationship obvious and removes
     * the implicit double-XOR that obscured the off-by-one analysis. */
    idle = S.active_half;
    halfp = (u8 far *)S.buffer + ((u32)idle * S.half_bytes);
    cb(halfp, S.half_frames, S.format);
    S.active_half ^= 1;

    /* Re-check after callback: a long-running callback could conceivably
     * straddle a teardown on a multitasking shell. */
    if (!S.running) {
        outp(mask_port, inp(mask_port) & ~mask_bit);
        S.isr_in_progress = 0;
        return;
    }

    /* SB 1.x is single-cycle: re-arm the next play command for the half we
     * just released so the DSP has something to consume. Same path is used
     * for force_single_cycle quirk mode (SB_SINGLECYCLE env, see sb_init).
     * Re-arm happens with our IRQ masked at PIC so a reentrant edge can
     * not interrupt the dsp_write busy-wait state machine.
     *
     * Use dsp_write_to with a TIGHT timeout (256, ~10us at 386 speeds): the
     * old bare dsp_write spins ~64K iterations per write, which on a wedged
     * chip means 192K iterations per ISR call, which on an XT can exceed
     * the audio sample interval and starve the foreground for the rest of
     * the playback. On first failure clear running so the next IRQ short
     * circuits before any DSP write. */
    if (S.dsp_major < 2 || S.force_single_cycle) {
        u8 sc_cmd = S.in_high_speed ? 0x91 : 0x14;
        if (!dsp_write_to(S.base, sc_cmd, 256) ||
            !dsp_write_to(S.base, (u8)((S.half_bytes - 1) & 0xFF), 256) ||
            !dsp_write_to(S.base, (u8)((S.half_bytes - 1) >> 8), 256)) {
            /* Partial command latched into the DSP. If we leave it there,
             * the next dsp_write (from foreground sb_close) sees a chip
             * still expecting the missing length bytes and feeds them
             * the wrong opcode. Issue an immediate reset pulse here so
             * the DSP state machine returns to idle; mirror dsp_reset's
             * first phase (write 1 to DSP_RESET) without the full read
             * loop because we just want to abort the stuck command, not
             * fully re-handshake. The full reset happens in sb_close. */
            outp(S.base + DSP_RESET, 1);
            S.running = 0;
        }
    }

    /* Unmask before IRET so any pending edge latched during ISR body fires
     * and refills the next half. */
    outp(mask_port, inp(mask_port) & ~mask_bit);
    S.isr_in_progress = 0;
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
    /* SB_SINGLECYCLE in env overrides the clone-detect default. Useful as a
     * diagnostic and as a fallback for chips that detect as SB Pro 2 / SB16
     * but whose auto-init mode (0x1C / 0x90 / 0xC6) does not actually cycle.
     * The Yamaha YMF715 OPL3-SAx in pure MS-DOS mode (no vendor init utility)
     * demonstrates this: TESTPLAY plays exactly one half-block then halts.
     * Single-cycle mode re-issues 0x14 from the ISR after every block,
     * sidestepping any auto-init implementation gap.
     *
     * Resolution order:
     *   1. Env set to "0": explicit user-off, force HFALSE even on clones.
     *   2. Env set to anything else (including "1"): explicit user-on.
     *   3. Env unset: default from hw->sb.flag_clone (set by detect/audio.c
     *      via the DSP 0xE3 copyright probe). Real Creative DSPs return a
     *      string containing CREATIVE; clones either time out or return
     *      something else, and either way flag_clone is set, defaulting
     *      this driver to single-cycle. */
    env = getenv("SB_SINGLECYCLE");
    if (env && env[0]) {
        S.force_single_cycle = (env[0] != '0') ? HTRUE : HFALSE;
    } else {
        S.force_single_cycle = hw->sb.flag_clone ? HTRUE : HFALSE;
    }
    /* Hard-cap IRQ to legal PIC range. install_isr's irq_vector() would
     * compute 0x70 + (irq - 8) for any irq >= 8, so an irq of 16 would
     * vector to 0x78 (free DOS slot), 17 to 0x79, etc., quietly hooking
     * an unrelated interrupt. Refuse rather than scribble on a stranger.
     *
     * Also reject the system IRQs that no Sound Blaster card legally uses:
     *   IRQ 0 = system timer, IRQ 1 = keyboard, IRQ 2 = cascade,
     *   IRQ 6 = floppy.
     * A typo or malicious BLASTER (e.g. BLASTER=A220 I0 ...) on these
     * lines would silently overwrite INT 8 / INT 9 / INT A / INT E with
     * sb_isr, then on uninstall run sb_close (which writes to the SB
     * ports) while the timer or keyboard handler is pointed at our ISR.
     * SB IRQs in the wild are 2/3/5/7/9/10/11/12 (and 5 is the canonical
     * default); accept only those, plus IRQ 4 and IRQ 8 which appear in
     * some clone BLASTER configs. */
    if (S.irq >= 16 || S.irq < 3 || S.irq == 6) return HFALSE;

    /* Wake step. Some chips that report SB-compatible respond to DSP
     * version queries but stay PCM-gated until vendor-specific control
     * registers are touched (notably the Yamaha YMF715 OPL3-SAx in pure
     * MS-DOS mode with its SBPDR bit set). The wake registry iterates
     * any registered backends; the first one that claims the chip via
     * probe() runs its wake(). HFALSE return is normal on real Creative
     * hardware (no backend claims the chip) and the dsp_reset below
     * proceeds as before. */
    (void)wake_chip(hw);

    if (!dsp_reset(S.base)) return HFALSE;
    /* If the detect layer did not pre-fill the version, query the chip.
     * A timeout here leaves dsp_major / dsp_minor at zero which gates the
     * card down to SB 1.x mode (single-cycle, mono, 8-bit). That is the
     * correct conservative behavior; better than the prior code which
     * silently committed whatever stale bytes were on the data port,
     * occasionally yielding spurious SB16 detection on a sick chip. */
    if (!S.dsp_major) {
        if (!dsp_get_version(S.base, &S.dsp_major, &S.dsp_minor)) {
            /* Leave dsp_major == 0; downstream caps fall through to SB 1.x. */
        }
    }
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
    /* Save full master + slave mask bytes verbatim so uninstall can restore
     * the exact pre-install state, including IRQ2 cascade and any unrelated
     * lines that other code (TSRs, BIOS) had masked. */
    S.prev_pic_master = inp(0x21);
    S.prev_pic_slave  = inp(0xA1);
    S.pic_state_saved = 1;

    S.prev_isr = _dos_getvect(irq_vector(S.irq));
    _dos_setvect(irq_vector(S.irq), sb_isr);
    if (S.irq < 8) {
        outp(0x21, S.prev_pic_master & ~irq_pic_mask_bit(S.irq));
    } else {
        outp(0xA1, S.prev_pic_slave & ~irq_pic_mask_bit(S.irq));
        /* Cascade IRQ2 must be enabled while we're using a slave-PIC line. */
        outp(0x21, S.prev_pic_master & ~0x04);
    }
}

static void uninstall_isr(void)
{
    if (!S.prev_isr) return;
    /* Mask the SB IRQ at the PIC FIRST so a pending edge can not fire into
     * the moment between vector swap and PIC restore. */
    if (S.irq < 8) {
        outp(0x21, inp(0x21) | irq_pic_mask_bit(S.irq));
    } else {
        outp(0xA1, inp(0xA1) | irq_pic_mask_bit(S.irq));
    }
    _dos_setvect(irq_vector(S.irq), S.prev_isr);
    S.prev_isr = 0;
    /* Restore pre-install PIC state, but always force the SB IRQ bit to be
     * MASKED on exit. If the line was unmasked at install time (because some
     * earlier code, BIOS, or a rude TSR had it open) a verbatim restore
     * would leave the slot pointing at a bare vector with our (now-freed)
     * ISR no longer hooked. Forcing the mask bit keeps that line silent
     * until a future driver legitimately re-hooks it. */
    if (S.pic_state_saved) {
        u8 master = S.prev_pic_master;
        u8 slave  = S.prev_pic_slave;
        if (S.irq < 8) master |= irq_pic_mask_bit(S.irq);
        else           slave  |= irq_pic_mask_bit(S.irq);
        outp(0x21, master);
        outp(0xA1, slave);
        S.pic_state_saved = 0;
    }
}

/* Shared rollback helper for sb_open failure paths between speaker-on and
 * a successful start command. Mirrors sb_close's step order: kill the ISR
 * fast path first (running=0, cb=0) so any IRQ that has somehow latched
 * before this call short-circuits before touching the now-stale callback
 * pointer, then mask DMA, mute speaker, reset the DSP, unhook the vector,
 * and finally free the buffer. Safe at any point after install_isr has run.
 * No high-speed branch is needed: every rollback site that calls this is
 * before the start command (the only place we enter high-speed). */
static void sb_open_cleanup(void)
{
    /* Step 1: mark not-running BEFORE any teardown I/O. The ISR snapshots
     * cb after the !running gate, so writing running=0 first guarantees
     * the next IRQ short-circuits before reading our (about to be cleared)
     * callback pointer. The previous order (dma_disable / dsp_reset before
     * cb=0 / running=0) left a window where an ISR that had already passed
     * the !running gate would call cb with state we were tearing down. */
    S.running = 0;
    S.cb      = 0;

    /* Step 2: stop DMA cold so the chip can not pull more bytes from the
     * buffer we are about to free. */
    if (S.active_dma) {
        dma_disable(S.active_dma);
        S.active_dma = 0;
    }

    /* Step 3: mute speaker, then reset DSP. Reset is the canonical "stop
     * everything" hammer; on a chip that is mid-transfer it forces the
     * state machine back to idle. Bounded timeout on the 0xD3 write so a
     * wedged chip can not stall rollback. */
    (void)dsp_write_to(S.base, 0xD3, 256);
    dsp_reset(S.base);

    /* Step 4: unhook ISR, free buffer last. */
    uninstall_isr();
    if (S.buffer) { dma_free(S.buffer); S.buffer = 0; }
}

static hbool sb_open(u32 rate, u8 format, audio_callback_t cb)
{
    u16 total_frames;
    u16 frame_bytes;
    u8  use16, use_stereo;
    u8  use_single_cycle;
    hbool need_high_speed = HFALSE;

    if (S.running) return HFALSE;
    if (!cb) return HFALSE;

    /* Clamp format to what this DSP can do. */
    use16      = (format & 2) && S.has_16bit;
    use_stereo = (format & 1) && (S.has_stereo || use16);
    if (use16 && use_stereo) format = AFMT_S16_STEREO;
    else if (use16)          format = AFMT_S16_MONO;
    else if (use_stereo)     format = AFMT_U8_STEREO;
    else                     format = AFMT_U8_MONO;

    /* Single-cycle is taken when the DSP predates auto-init (SB 1.x) OR
     * when the user forced it via SB_SINGLECYCLE for a quirky clone.
     * Computed once so the high-speed decision and the DMA mode selection
     * agree on which branch we are in. */
    use_single_cycle = (S.dsp_major < 2) || S.force_single_cycle;

    /* INVARIANT: S.format, S.cb, S.buffer, S.half_bytes, and S.half_frames
     * are all set BEFORE install_isr below. The ISR snapshots all five on
     * entry and assumes they are consistent. Any reordering that defers
     * one of these past install_isr opens a window where a stale IRQ from
     * a prior session can fire into a half-initialized state. */
    S.rate = rate;
    S.format = format;
    S.cb = cb;
    {
        /* Scale frames-per-half by rate / reference. Clamp on both sides:
         * floor keeps IRQ cadence under the mixer's render budget; ceiling
         * keeps total buffer (half_frames * BUF_HALVES * frame_bytes) below
         * dma_alloc's 64K boundary even for stereo s16 at the highest rates
         * the SB16 will accept (48 kHz). */
        u32 scaled = ((u32)BUF_REFERENCE_FRAMES * rate) / BUF_REFERENCE_RATE;
        if (scaled < BUF_MIN_FRAMES) scaled = BUF_MIN_FRAMES;
        if (scaled > BUF_MAX_FRAMES) scaled = BUF_MAX_FRAMES;
        S.half_frames = (u16)scaled;
    }
    frame_bytes = AFMT_FRAME_BYTES(format);
    S.half_bytes = S.half_frames * frame_bytes;
    total_frames = S.half_frames * BUF_HALVES;

    S.buffer = dma_alloc((u32)S.half_bytes * BUF_HALVES, use16, &S.buffer_phys);
    if (!S.buffer) { S.cb = 0; return HFALSE; }
    /* Pre-fill both halves with silence-relative encoding. */
    if (use16) memset((u8 far *)S.buffer, 0,    (u32)S.half_bytes * BUF_HALVES);
    else       memset((u8 far *)S.buffer, 0x80, (u32)S.half_bytes * BUF_HALVES);

    install_isr();

    /* DAC speaker on. SB16 ignores this for output in theory, but DOSBox-X
     * faithfully emulates the gate; without 0xD1 the DSP never advances DMA
     * and the IRQ never fires.  Doslib / OCP / OSDev all set this first.
     * If the chip never accepts 0xD1 (busy bit stuck high, e.g. wedged
     * YMF715), there is no point programming rate or DMA. The follow-on
     * dsp_writes would all time out into a chip that already has half a
     * command latched and we would leak an ISR hook + DMA buffer. */
    if (!dsp_write(S.base, 0xD1)) {
        sb_open_cleanup();
        return HFALSE;
    }

    /* Order matters: program rate FIRST so that a v2 rate failure aborts
     * before DMA is armed against a buffer we are about to free. Then arm
     * DMA. Then issue the start command. Programming DMA before rate (the
     * old order) opened a window where, if dsp_set_rate_v2 failed, the 8237
     * was already unmasked against the freed page; a chip that had already
     * issued 0xD1 (speaker on, above) plus a stale DREQ could pull garbage
     * before the rollback dma_disable landed. Same shape as the YMF715
     * close-path wedge. */
    {
        u8  channels   = use_stereo ? 2 : 1;
        u32 nsamples_total = (u32)total_frames * channels;
        /* DMA mode: SB 1.x and force_single_cycle drive single-cycle DSP
         * commands (0x14) and re-arm from the ISR. The 8237 must match: in
         * auto-init mode the controller would re-prime DREQ between the
         * 0x14 commands and pull stale bytes from the wrap. */
        hbool dma_auto = use_single_cycle ? HFALSE : HTRUE;

        /* Program DSP rate. SB16 (v4+) takes the actual Hz on its rate
         * command and handles stereo internally. SB Pro / 2.0 uses the
         * time-constant formula and needs `rate * 2` for stereo because
         * the DSP clocks L and R alternately at the same TC rate. */
        if (S.dsp_major >= 4) {
            if (!dsp_set_rate_v4(S.base, rate)) {
                /* Same teardown as the v2 rate failure path: DMA was not
                 * armed yet, only ISR hook + speaker + buffer alloc. */
                sb_open_cleanup();
                return HFALSE;
            }
        } else {
            u32 v2_rate = use_stereo ? rate * 2 : rate;
            if (!dsp_set_rate_v2(S.base, v2_rate)) {
                /* Rate out of TC formula range. DMA was NOT armed yet (we
                 * deferred it to after this point) so all we have to undo
                 * is the ISR hook, speaker mute, and the buffer alloc. */
                sb_open_cleanup();
                return HFALSE;
            }
            /* Decide once whether THIS rate needs high-speed mode (TC > 0xD2,
             * regardless of which DMA arming branch we take below). The
             * SB Pro 2 auto-init branch and the single-cycle branch both
             * consult this flag so they pick the right start command. */
            {
                u32 tc_check = 256UL - (1000000UL / v2_rate);
                if (tc_check > 0xD2UL) need_high_speed = HTRUE;
            }
        }

        /* Program DMA. SB16 16-bit transfers go on the high (16-bit) channel
         * and count WORDS, not frames. For stereo each frame is 2 samples =
         * 2 words. The low channel handles 8-bit byte-counted transfers. */
        if (use16) {
            dma_setup_16bit(S.dma16, S.buffer_phys, nsamples_total, dma_auto);
            S.active_dma = S.dma16;
        } else {
            dma_setup_8bit (S.dma8,  S.buffer_phys, (u32)S.half_bytes * BUF_HALVES, dma_auto);
            S.active_dma = S.dma8;
        }

        if (S.dsp_major >= 4) {
            u8  cmd  = use16 ? 0xB6 : 0xC6;       /* auto-init output */
            u8  mode = (use_stereo ? 0x20 : 0x00) | (use16 ? 0x10 : 0x00);
            /* DSP length is HALF-buffer-1 in TRANSFER UNITS:
             *   - 0xC6 (8-bit auto-init) counts BYTES.
             *     Half = half_frames * channels * 1 byte/sample.
             *   - 0xB6 (16-bit auto-init) counts WORDS.
             *     Half = half_frames * channels * 1 word/sample.
             * The previous formula `(nsamples_total >> 1) - 1` happened to
             * compute the right number for both cases when BUF_HALVES == 2,
             * but only by coincidence: `nsamples_total` is in samples (not
             * bytes, not words), and `>> 1` extracts a half-buffer in
             * samples, which equals bytes for 8-bit and words for 16-bit
             * because each sample is one transfer unit. The unit identity
             * breaks the moment a future format adds a per-sample size that
             * is not equal to the transfer size (e.g. a packed format).
             * Compute directly from half_frames in the natural transfer
             * unit for each command to make the math obvious and unbreakable.
             *
             * IRQ cadence: with auto-init, the SB16 fires its IRQ at every
             * block-count boundary while DMA wraps at the full physical
             * buffer. Programming half-buffer here gives us two IRQs per
             * DMA wrap, one per half, which is what active_half flip
             * refills. Programming full-buffer would IRQ only on wrap and
             * leave the second half one cycle stale. */
            u16 length_param = (u16)(((u32)S.half_frames * channels) - 1UL);
            /* Treat any command-byte timeout as fatal: a half-eaten 0xC6 /
             * 0xB6 leaves the DSP expecting more bytes; any subsequent
             * dsp_write would feed the wrong opcode. DMA is already armed
             * at this point, so sb_open_cleanup must mask it (which it
             * does via S.active_dma). */
            if (!dsp_write(S.base, cmd) ||
                !dsp_write(S.base, mode) ||
                !dsp_write(S.base, (u8)(length_param & 0xFF)) ||
                !dsp_write(S.base, (u8)(length_param >> 8))) {
                sb_open_cleanup();
                return HFALSE;
            }
        } else if (S.dsp_major >= 2 && !use_single_cycle) {
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
            u8 start_cmd = need_high_speed ? 0x90 : 0x1C;
            if (use_stereo) {
                /* SBPro mixer: enable stereo output.  Index port = base+04,
                 * data port = base+05.  Register 0x0E bit 1 = stereo. Save
                 * the original byte first so sb_close can restore it; without
                 * the restore, the next program inherits a mixer in stereo
                 * mode and a mono Adlib title gets two unrelated channels
                 * panned hard.
                 *
                 * SB Pro mixer chip needs an ISA-bus settle time between the
                 * index write at base+04 and the data port at base+05. The
                 * mixer chip is slower than the DSP and a back-to-back
                 * outp/inp on a fast bus latches stale data. Use the same
                 * port-0x80 read pattern as adlib.c delay_short. */
                outp(S.base + 0x04, 0x0E);
                (void)inp(0x80); (void)inp(0x80); (void)inp(0x80);
                S.sbpro_mixer_0e_prev  = inp(S.base + 0x05);
                S.sbpro_mixer_0e_saved = 1;
                outp(S.base + 0x04, 0x0E);
                (void)inp(0x80); (void)inp(0x80); (void)inp(0x80);
                outp(S.base + 0x05, S.sbpro_mixer_0e_prev | 0x02);
            }
            /* Block size = ONE HALF, not full buffer: DSP fires IRQ at every
             * half boundary so the ISR can refill the idle half.  Sending
             * the full buffer here means IRQ fires only once per wrap, half
             * the data goes stale. */
            dsp_set_block_count(S.base, S.half_bytes);
            if (start_cmd == 0x90) S.in_high_speed = 1;
            if (!dsp_write(S.base, start_cmd)) {
                /* If 0x90 was the start command we already flagged
                 * in_high_speed, sb_open_cleanup's reset path will
                 * still bring the DSP back to idle. */
                sb_open_cleanup();
                return HFALSE;
            }
        } else {
            /* SB 1.x: single-cycle 8-bit. ISR re-arms each half.
             * Also taken when force_single_cycle is set (SB_SINGLECYCLE env)
             * as a quirk fallback for chips whose auto-init mode is broken.
             *
             * If the rate is in high-speed territory (TC > 0xD2), use the
             * single-cycle high-speed command (0x91) instead of 0x14. The
             * old code unconditionally issued 0x14 which on a real SB Pro 2
             * would underrun TC and play one block of garbage at half-rate
             * before stalling. */
            if (use_stereo && S.dsp_major >= 3) {
                /* SBPro mixer: enable stereo even in single-cycle mode.
                 * port-0x80 settle delays around the index/data pair: see
                 * the auto-init branch above for rationale. */
                outp(S.base + 0x04, 0x0E);
                (void)inp(0x80); (void)inp(0x80); (void)inp(0x80);
                S.sbpro_mixer_0e_prev  = inp(S.base + 0x05);
                S.sbpro_mixer_0e_saved = 1;
                outp(S.base + 0x04, 0x0E);
                (void)inp(0x80); (void)inp(0x80); (void)inp(0x80);
                outp(S.base + 0x05, S.sbpro_mixer_0e_prev | 0x02);
            }
            printf("(SB single-cycle mode: %s with ISR re-arm)\n",
                   need_high_speed ? "0x91 high-speed" : "0x14 normal");
            fflush(stdout);
            /* half_bytes==0 would encode a 64K transfer; same trap as
             * dsp_set_block_count. half_frames is set from a compile-time
             * constant above so this is defensive. */
            if (S.half_bytes != 0) {
                u8 sc_cmd = need_high_speed ? 0x91 : 0x14;
                if (need_high_speed) S.in_high_speed = 1;
                if (!dsp_write(S.base, sc_cmd) ||
                    !dsp_write(S.base, (u8)((S.half_bytes - 1) & 0xFF)) ||
                    !dsp_write(S.base, (u8)((S.half_bytes - 1) >> 8))) {
                    sb_open_cleanup();
                    return HFALSE;
                }
            }
        }
    }

    S.running = 1;
    return HTRUE;
}

static void sb_close(void)
{
    /* Teardown order matters: a stuck DSP (notably the Yamaha YMF715 in
     * MS-DOS mode without vendor init) can keep pulling DMA after we ask
     * it to stop, which would corrupt the MCB chain once we free the
     * buffer. We therefore:
     *   1) Mark ourselves not-running so any IRQ that fires from here on
     *      bails out of the ISR body before it can re-arm DMA or call
     *      back into a callback whose state we are about to release.
     *   2) Mask the DMA channel at the controller so even if the chip
     *      keeps asserting DREQ, it gets no DACK and writes nothing.
     *   3) Mask the SB IRQ at the PIC so any pending edge can not fire
     *      our ISR while we tear the DSP down.
     *   4) Reset the DSP. In high-speed mode this is the only thing that
     *      will actually stop transfers; the 0xD0/0xDA halt commands are
     *      ignored. In normal mode we still send 0xD3 (speaker off) for
     *      a clean exit.
     *   4b) Restore the SB Pro mixer reg 0x0E we touched in sb_open.
     *   5) Restore the IRQ vector and PIC mask state, then re-mask the
     *      DMA channel a second time to catch any edge the reset provoked.
     *   6) Free the DMA buffer.
     * sb_close must also be safe from a partial-init state, so each step
     * is gated on whether that resource was actually claimed. */

    u8 dma_to_remask = 0;

    /* Step 1: kill the ISR fast path. ISR re-checks running before any
     * dsp_write or callback. */
    S.running = 0;
    S.cb      = 0;

    /* Step 1b: wait for any in-flight sb_isr to finish its DSP I/O before
     * the foreground touches the same chip. Without this, dsp_reset below
     * can preempt the ISR's dsp_write_to triple (the SB 1.x / single-cycle
     * re-arm path), latching half a command into the DSP and leaving the
     * state machine wedged on real iron. The ISR sets isr_in_progress
     * after the !running short-circuit, so by this point the running=0
     * above guarantees that the next IRQ short-circuits without setting
     * the flag; only an IRQ that was ALREADY past the gate when we wrote
     * running=0 keeps the flag high. Bounded spin (~0x1000 iterations of
     * a port read on the SB read-status port, well under 1ms even on slow
     * iron) so a permanently-wedged ISR can not deadlock close. */
    {
        unsigned int spin = 0x1000;
        while (S.isr_in_progress && spin--) {
            (void)inp(S.base + DSP_RSTATUS);
        }
    }

    /* Step 2: stop DMA cold. Snapshot the channel so step 5b can re-mask
     * it after the DSP reset, even though S.active_dma is cleared here to
     * keep the partial-init invariant. */
    if (S.active_dma) {
        dma_to_remask = S.active_dma;
        dma_disable(S.active_dma);
        S.active_dma = 0;
    }

    /* Step 3: mask the SB IRQ at the PIC. uninstall_isr (step 5) will do
     * this too, but doing it now closes the window before dsp_reset. */
    if (S.prev_isr) {
        if (S.irq < 8)  outp(0x21, inp(0x21) | irq_pic_mask_bit(S.irq));
        else            outp(0xA1, inp(0xA1) | irq_pic_mask_bit(S.irq));
    }

    /* Step 4: DSP halt + reset. In high-speed mode skip the halt commands
     * (the DSP ignores them and each write burns ~16K wait-loop iterations
     * while DMA was still live, which we already masked above). 0xDA on
     * a high-speed chip can also lock state on spec-strict implementations.
     *
     * Use dsp_write_to with a TIGHT timeout (256) on the halt path. The
     * full 0x4000 timeout in dsp_write was burning ~64K port reads per
     * stuck command on a wedged YMF715 (4 commands = 256K port reads, all
     * after we have already declared the player wedged). dsp_write_to
     * returns FALSE without latching an OUT into a stuck state machine. */
    if (S.in_high_speed) {
        dsp_reset(S.base);
        /* Spec says reset is the only thing that stops a high-speed DSP;
         * issue a second reset so any lingering ack edge clears before
         * we hand the DMA page back to DOS. */
        dsp_reset(S.base);
    } else if (S.prev_isr || S.buffer) {
        if (S.format == AFMT_S16_MONO || S.format == AFMT_S16_STEREO) {
            (void)dsp_write_to(S.base, 0xD5, 256);
            (void)dsp_write_to(S.base, 0xDA, 256);    /* exit 16-bit auto-init */
        } else {
            (void)dsp_write_to(S.base, 0xD0, 256);
            (void)dsp_write_to(S.base, 0xDA, 256);    /* exit 8-bit auto-init */
        }
        (void)dsp_write_to(S.base, 0xD3, 256);        /* speaker off */
        dsp_reset(S.base);
    }
    S.in_high_speed = 0;

    /* Step 4b: restore SB Pro mixer reg 0x0E to its pre-open value if we
     * touched it. Without this, a mono-only program launched after a stereo
     * HEARO session inherits the stereo bit and gets two unrelated channels
     * panned hard left/right. Only meaningful on SB Pro and clones (DSP v3);
     * SB 2.0 and SB16 either lack this register or ignore the write. */
    if (S.sbpro_mixer_0e_saved) {
        /* port-0x80 settle delay between index and data writes: matches
         * the delay we use on the open side. Without it, the data byte
         * can land before the mixer chip latches the index, leaving a
         * stale register selected. */
        outp(S.base + 0x04, 0x0E);
        (void)inp(0x80); (void)inp(0x80); (void)inp(0x80);
        outp(S.base + 0x05, S.sbpro_mixer_0e_prev);
        S.sbpro_mixer_0e_saved = 0;
    }

    /* Step 5: unhook ISR (also restores full PIC mask state). */
    if (S.prev_isr) uninstall_isr();

    /* Step 5b: defeat any lingering DREQ from a wedged chip BEFORE we hand
     * the buffer's MCB back to DOS. The 8237 channel mask bit alone does
     * NOT cancel an in-flight cycle on a chip that is mid-DREQ; the
     * controller services the pending request until DACK lands. On a
     * wedged YMF715 in MS-DOS mode, dma_disable returns immediately while
     * the chip continues to assert DREQ, and the next ISA refresh tick
     * re-issues the cycle into whatever the page register still points at.
     *
     * Sequence (all under cli so an IRQ can not slip in, re-arm DMA, and
     * stage another cycle between the steps):
     *   1) dma_disable: re-assert the channel mask in case dsp_reset
     *      above provoked a final DREQ pulse.
     *   2) dma_wait_quiescent: confirm the count register has stopped
     *      changing across two reads, i.e. no cycles are landing.
     *   3) dma_disable a second time: belt-and-suspenders for the case
     *      where step 1's mask write was racing with a DACK. Per-channel
     *      mask only; DOES NOT touch other DMA channels.
     *
     * IMPORTANT: this used to call dma_master_clear() here. That function
     * masks ALL FOUR channels on BOTH 8237 controllers, including DMA 2
     * (floppy) and DMA 0 (memory refresh on legacy XT-class boards).
     * Masking floppy on shutdown means COMMAND.COM can not be reloaded
     * from floppy, hanging the box at exit. Masking refresh corrupts DRAM
     * on systems that rely on the 8237 refresh tick (rare on AT+ but real
     * on some clones). Per-channel dma_disable is sufficient for the SB
     * shutdown case because we already issued dsp_reset above, which
     * stops the DSP's own DREQ generator; the only remaining concern is
     * a single in-flight cycle, which the dma_wait_quiescent loop catches.
     *
     * cli is dropped before dma_free below: dma_free issues INT 21h AH=49h
     * which DOS may need to service with IF on. The protection for the
     * MCB walk is the channel-mask + dsp_reset combination: no DREQ from
     * the DSP produces a cycle, and the channel mask blocks any stray
     * DREQ that the chip might re-assert.
     *
     * We DO NOT use dsp_wait_idle: it polled the DSP COMMAND-PORT BUSY
     * bit which is unrelated to DMA progress, and on a wedged chip stays
     * high until timeout, providing only false reassurance while the
     * actual safety comes from mastering the 8237 channel directly. */
    _disable();
    if (dma_to_remask) {
        dma_disable(dma_to_remask);
        (void)dma_wait_quiescent(dma_to_remask, 0x1000);
        dma_disable(dma_to_remask);
    }
    _enable();

    /* Step 6: release the DMA buffer last, after every path that could
     * write to it is shut down. With the channel masked at the 8237 and
     * the DSP reset, no DREQ produces a bus cycle, so the MCB walk inside
     * INT 21h AH=49h can not collide with a residual DMA cycle. */
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
