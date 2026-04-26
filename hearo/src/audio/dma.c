/*
 * audio/dma.c - ISA DMA channel programming.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Standard 8237A pair: master (channels 0-3) at 00h-0Fh, slave (4-7) at
 * C0h-DFh.  Page registers live in the 8255 PPI scattered area (80h-8Fh).
 * ISA buffers must not cross a 64K boundary for 8-bit, 128K for 16-bit.
 */
#include "dma.h"
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <malloc.h>
#include <stdlib.h>

/* Per-channel registers for the master (8-bit) controller. */
static const u8 dma8_addr[4]  = { 0x00, 0x02, 0x04, 0x06 };
static const u8 dma8_count[4] = { 0x01, 0x03, 0x05, 0x07 };
static const u8 dma8_page[4]  = { 0x87, 0x83, 0x81, 0x82 };

/* Per-channel registers for the slave (16-bit) controller. The page register
 * holds bits 16-23 of the byte address even though the controller indexes
 * words. */
static const u8 dma16_addr[4]  = { 0xC0, 0xC4, 0xC8, 0xCC };
static const u8 dma16_count[4] = { 0xC2, 0xC6, 0xCA, 0xCE };
static const u8 dma16_page[4]  = { 0x8F, 0x8B, 0x89, 0x8A };

#define DMA_SLOTS 4

/* Track each allocation so dma_free can release the original DOS block.
 * We allocate via INT 21h AH=48h directly because the request size (up to
 * 0x20000 bytes for 16-bit DMA) exceeds the 16-bit size_t limit of _fmalloc
 * in Watcom large model. We bypass Watcom's _dos_allocmem prototype: it has
 * two overloads (void-double-pointer and unsigned-pointer) selected by
 * preprocessor symbols (__NT__ etc.) and the wrong one writes only 2 bytes.
 * Using int86 with AH=48h sidesteps the ambiguity entirely. */
static struct {
    u16  raw_seg;        /* segment returned by INT 21h AH=48h, 0 = unused */
    void far *aligned;   /* pointer handed to caller (may be bumped past boundary) */
    u32  aligned_end;    /* phys address one past the usable region */
} g_dma_allocs[DMA_SLOTS];

void far *dma_alloc(u32 size, hbool sixteen_bit, u32 *out_phys)
{
    u32 boundary = sixteen_bit ? 0x20000UL : 0x10000UL;
    u32 want;
    u32 paragraphs;
    u16 seg;
    u8 far *aligned;
    u32 phys;
    u32 end_phys;
    u32 bumped_seg;
    int slot;
    u16 prev_strategy;
    hbool strategy_saved = HFALSE;
    union REGS r;
    struct SREGS sr;

    /* Hard cap at the boundary itself: a single transfer can not exceed the
     * 8237 page register's window. Caller bug if size > boundary, refuse. */
    if (size == 0 || size > boundary) return 0;

    /* Find a free slot first; if the table is full, refuse the alloc rather
     * than handing out a buffer dma_free will leak. */
    for (slot = 0; slot < DMA_SLOTS; slot++) {
        if (g_dma_allocs[slot].raw_seg == 0) break;
    }
    if (slot == DMA_SLOTS) return 0;

    /* Worst case: starting phys is one byte past a boundary, so we lose
     * (boundary - 1) bytes to alignment slack. Round up to whole paragraphs. */
    want = size + boundary;
    paragraphs = (want + 15UL) >> 4;
    if (paragraphs > 0xFFFFUL) return 0;

    /* Force allocation strategy to "first-fit, low memory only" before the
     * AH=48h call. Under DOS=HIGH,UMB with EMM386 / QEMM / 386MAX, the MCB
     * chain spans both conventional memory (below A0000h) and one or more
     * UMB regions above A0000h; the default allocation strategy can return
     * a segment in the UMB region. The 8237 page register can address those
     * physical addresses (A0000h-FFFFFh), but EMM386 maps them as paged
     * windows backed by extended memory, and the page mapping is shared
     * with EMS handles owned by other programs. A DMA write into a UMB
     * scrambles whatever EMS handle currently maps that page frame.
     *
     * INT 21h AH=58h, AL=00h: get current strategy (returned in AX).
     * INT 21h AH=58h, AL=01h, BX=0: set first-fit, low memory only.
     * Restored after the alloc completes (success or failure).
     *
     * Strategy bits in BX:
     *   00h = first fit, low memory
     *   01h = best fit, low memory
     *   02h = last fit, low memory
     *   40h = first fit, high memory (UMB)
     *   80h = first fit, high then low
     * We want 00h. */
    r.h.ah = 0x58;
    r.h.al = 0x00;
    int86(0x21, &r, &r);
    if (!r.x.cflag) {
        prev_strategy = r.x.ax;
        strategy_saved = HTRUE;
        r.h.ah = 0x58;
        r.h.al = 0x01;
        r.x.bx = 0;
        int86(0x21, &r, &r);
        /* If the set fails (very old DOS, < 3.0), proceed anyway. We have
         * the explicit phys < A0000 check below as belt-and-suspenders. */
    }

    /* INT 21h AH=48h: allocate memory. BX = paragraphs requested. On success
     * CF clear, AX = segment of allocated block (offset is 0). On failure
     * CF set, AX = error code, BX = largest available block. */
    r.h.ah = 0x48;
    r.x.bx = (unsigned)paragraphs;
    int86(0x21, &r, &r);
    if (r.x.cflag) {
        if (strategy_saved) {
            r.h.ah = 0x58;
            r.h.al = 0x01;
            r.x.bx = prev_strategy;
            int86(0x21, &r, &r);
        }
        return 0;
    }
    seg = r.x.ax;

    /* Restore the caller's allocation strategy now that we have our segment.
     * Subsequent allocations from elsewhere in the program (mostly malloc
     * via _fmalloc) get the strategy they expected. */
    if (strategy_saved) {
        r.h.ah = 0x58;
        r.h.al = 0x01;
        r.x.bx = prev_strategy;
        int86(0x21, &r, &r);
    }

    aligned = (u8 far *)MK_FP(seg, 0);
    phys = (u32)seg << 4;
    if (((phys + size - 1UL) & ~(boundary - 1UL)) !=
        (phys & ~(boundary - 1UL))) {
        u32 next  = (phys + boundary) & ~(boundary - 1UL);
        u32 delta = next - phys;
        /* Compute the bumped segment in 32 bits and reject overflow past the
         * 1 MiB segment range. A near-1 MiB allocation could otherwise wrap
         * a u16 segment add and hand back a UMB-region pointer that does not
         * map to the buffer we just allocated. */
        bumped_seg = (u32)seg + (delta >> 4);
        if (bumped_seg >= 0x10000UL) {
            segread(&sr);
            sr.es = seg;
            r.h.ah = 0x49;
            int86x(0x21, &r, &r, &sr);
            return 0;
        }
        aligned = (u8 far *)MK_FP((u16)bumped_seg, 0);
        phys += delta;
    }
    end_phys = ((u32)seg << 4) + (paragraphs << 4);
    /* Defensive: bumped buffer must still fit inside what we allocated. */
    if (phys + size > end_phys) {
        segread(&sr);
        sr.es = seg;
        r.h.ah = 0x49;
        int86x(0x21, &r, &r, &sr);
        return 0;
    }

    /* Belt-and-suspenders: even with strategy 00h forced, on broken UMB
     * configurations (old QEMM, weird stacked drivers) the kernel can
     * occasionally honor the request from a high block anyway. The 8237's
     * page register CAN address phys A0000-FFFFF, but those pages are
     * EMM386-managed and a DMA write would corrupt the EMS handle currently
     * mapped there. Reject any allocation whose end falls at or above
     * A0000h, free it, and fail. The caller (sb_open) handles dma_alloc
     * failure by aborting open and retrying via the null driver. */
    if (phys + size > 0xA0000UL || phys >= 0xA0000UL) {
        segread(&sr);
        sr.es = seg;
        r.h.ah = 0x49;
        int86x(0x21, &r, &r, &sr);
        return 0;
    }

    g_dma_allocs[slot].raw_seg     = seg;
    g_dma_allocs[slot].aligned     = aligned;
    g_dma_allocs[slot].aligned_end = end_phys;

    if (out_phys) *out_phys = phys;
    return aligned;
}

void dma_free(void far *buf)
{
    int slot;
    union REGS r;
    struct SREGS sr;
    if (!buf) return;
    for (slot = 0; slot < DMA_SLOTS; slot++) {
        if (g_dma_allocs[slot].aligned == buf) {
            /* INT 21h AH=49h: free memory. ES = segment of block. */
            segread(&sr);
            sr.es = g_dma_allocs[slot].raw_seg;
            r.h.ah = 0x49;
            int86x(0x21, &r, &r, &sr);
            g_dma_allocs[slot].raw_seg     = 0;
            g_dma_allocs[slot].aligned     = 0;
            g_dma_allocs[slot].aligned_end = 0;
            return;
        }
    }
}

static void mask_8 (u8 ch) { outp(0x0A, 0x04 | (ch & 3)); }
static void unmask_8(u8 ch){ outp(0x0A, 0x00 | (ch & 3)); }
static void mask_16(u8 ch) { outp(0xD4, 0x04 | (ch & 3)); }
static void unmask_16(u8 ch){ outp(0xD4, 0x00 | (ch & 3)); }

void dma_disable(u8 channel) { if (channel < 4) mask_8(channel); else mask_16(channel - 4); }
void dma_enable (u8 channel) { if (channel < 4) unmask_8(channel); else unmask_16(channel - 4); }

void dma_setup_8bit(u8 channel, u32 phys, u32 length, hbool auto_init)
{
    u8 ch = channel & 3;
    u8 mode;
    u16 cnt;
    /* 8237 byte counter is 16-bit. Largest legal transfer is 65536 bytes,
     * encoded as count = 0xFFFF. Anything larger silently wraps. */
    if (length == 0 || length > 0x10000UL) return;
    /* 64K page boundary check: ISA 8-bit DMA can not cross 64K. The page
     * register holds the high byte of the address; the controller wraps the
     * low 16 bits silently, splattering the second half of the transfer at
     * a bogus address. */
    if ((phys & 0xFFFFUL) + length > 0x10000UL) return;
    cnt = (u16)(length - 1UL);
    /* Mode byte 8237: bits 7-6 = mode (01 single, 00 demand), bit 4 = auto
     * init, bits 3-2 = direction (10 = read from memory), bits 1-0 = channel.
     * 0x58 = single + auto-init + read; 0x48 = single + read (no auto-init).
     * SB 1.x style 0x14 commands halt-at-TC and re-arm from the ISR; if the
     * controller is in auto-init mode the next DREQ pulls a stale byte from
     * the wrap before the DSP re-issues the play command. */
    mode = auto_init ? 0x58 : 0x48;
    mode |= ch;
    /* The address / page / count writes are split across multiple ports and
     * the 8237 latches them via a flip-flop that toggles between the LSB
     * and MSB ports. An IRQ that fires between the LSB write and the MSB
     * write of any pair leaves the flip-flop in the WRONG state for whichever
     * pair is being written next, so the controller ends up programmed with
     * a scrambled address or count. The mask_8 / unmask_8 writes are a
     * single port write each (atomic on a 286+), but the body needs full
     * IF off for the duration of the multi-port programming. */
    _disable();
    mask_8(ch);
    outp(0x0C, 0);                                  /* clear flip-flop */
    outp(0x0B, mode);
    outp(dma8_addr[ch], (u8)(phys & 0xFF));
    outp(dma8_addr[ch], (u8)((phys >> 8) & 0xFF));
    outp(dma8_page[ch], (u8)((phys >> 16) & 0xFF));
    outp(dma8_count[ch], (u8)(cnt & 0xFF));
    outp(dma8_count[ch], (u8)((cnt >> 8) & 0xFF));
    _enable();
    unmask_8(ch);
}

void dma_setup_16bit(u8 channel, u32 phys, u32 length, hbool auto_init)
{
    /* 16-bit channels address words: shift physical byte address right by 1
     * for the address register, but keep the page byte aligned. length is
     * in WORDS for 16-bit DMA; max 65536 words = 128 KiB. */
    u8 ch = (channel - 4) & 3;
    u8 mode;
    u32 word_addr = (phys >> 1);
    u32 bytes;
    u16 cnt;
    if (length == 0 || length > 0x10000UL) return;
    /* 128K page boundary check: ISA 16-bit DMA can not cross 128K. Same
     * silent-wrap failure mode as 8-bit, just at the larger window. */
    bytes = length << 1;
    if ((phys & 0x1FFFFUL) + bytes > 0x20000UL) return;
    cnt = (u16)(length - 1UL);
    mode = auto_init ? 0x58 : 0x48;
    mode |= ch;
    /* See dma_setup_8bit for cli rationale: the split-byte port writes
     * latch through a flip-flop that an interrupt can desync. */
    _disable();
    mask_16(ch);
    outp(0xD8, 0);
    outp(0xD6, mode);
    outp(dma16_addr[ch], (u8)(word_addr & 0xFF));
    outp(dma16_addr[ch], (u8)((word_addr >> 8) & 0xFF));
    outp(dma16_page[ch], (u8)((phys >> 16) & 0xFF));
    outp(dma16_count[ch], (u8)(cnt & 0xFF));
    outp(dma16_count[ch], (u8)((cnt >> 8) & 0xFF));
    _enable();
    unmask_16(ch);
}

u16 dma_get_count(u8 channel)
{
    u8 ch;
    u8 lo, hi;
    if (channel < 4) {
        ch = channel & 3;
        outp(0x0C, 0);
        lo = inp(dma8_count[ch]);
        hi = inp(dma8_count[ch]);
    } else {
        ch = (channel - 4) & 3;
        outp(0xD8, 0);
        lo = inp(dma16_count[ch]);
        hi = inp(dma16_count[ch]);
    }
    return (u16)lo | ((u16)hi << 8);
}

/* Force the 8237 pair into a known idle state, regardless of any in-flight
 * cycle the chip may be servicing. The master-clear command (any write to
 * port 0x0D for the master controller, port 0xDA for the slave) resets the
 * command, status, request, and temporary registers, and sets all four mask
 * bits, leaving every channel masked. This is the only way to cancel an
 * in-flight DREQ from a wedged sound chip (notably the YMF715 in MS-DOS
 * mode) without waiting for the chip itself to drop the request, which on
 * a stuck chip never happens. Channel masks are intentionally NOT restored
 * after the master clear; callers that need a specific channel armed must
 * re-program it via dma_setup_*.
 *
 * !!! WARNING !!!
 * This is an EMERGENCY hammer. It masks ALL EIGHT DMA channels, which
 * includes:
 *   - DMA 0: memory refresh on some XT-class systems (DRAM corruption
 *     if left masked even briefly).
 *   - DMA 2: floppy controller. Masking floppy on shutdown prevents
 *     COMMAND.COM from being reloaded after program exit on a
 *     floppy-only boot, hanging the system at the prompt.
 *   - DMA 4: cascade between the two controllers. Masking this on AT+
 *     class systems disables channels 5-7 entirely.
 *
 * DO NOT call from a normal close / shutdown path. Use per-channel
 * dma_disable() there. Reserve dma_master_clear for situations where
 * the system is already wedged and a hard reset is the next step. */
void dma_master_clear(void)
{
    _disable();
    outp(0x0D, 0);   /* master controller (channels 0-3) master clear */
    outp(0xDA, 0);   /* slave controller (channels 4-7) master clear */
    _enable();
}

/* Wait for the channel's DMA count register to stop changing across two
 * consecutive reads. Returns HTRUE if the count was stable within the
 * iteration budget, HFALSE if the chip kept servicing DREQ until timeout.
 * Used by sb_close before dma_free to confirm there is no in-flight cycle
 * the freed buffer's MCB walk could collide with. The flip-flop is reset
 * inside dma_get_count (single port write) so reads do not interfere with
 * each other; the worst case is reading mid-cycle and seeing a transient
 * value, which the second read will catch. */
hbool dma_wait_quiescent(u8 channel, unsigned int max_iters)
{
    u16 a, b;
    a = dma_get_count(channel);
    while (max_iters--) {
        b = dma_get_count(channel);
        if (a == b) return HTRUE;
        a = b;
    }
    return HFALSE;
}
