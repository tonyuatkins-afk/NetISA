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

/* Track the original allocation pointer so dma_free can release it; the
 * pointer we return to the caller may have been bumped past a boundary. */
static struct {
    void far *raw;
    void far *aligned;
} g_dma_allocs[4];

void far *dma_alloc(u16 size, hbool sixteen_bit, u32 *out_phys)
{
    /* Over-allocate by one DMA-page boundary so we can shift past a crossing.
     * size_t is u16 in Watcom DOS large model: an unguarded
     * (size_t)0x20000UL truncates to 0 and the boundary slack vanishes. We
     * compose the request as u32 and rely on _fmalloc accepting u32. */
    u32 boundary = sixteen_bit ? 0x20000UL : 0x10000UL;
    u32 want     = (u32)size + boundary;
    u8 far *raw  = (u8 far *)_fmalloc(want);
    u8 far *aligned;
    u32 phys;
    int slot;
    if (!raw) return 0;
    aligned = raw;
    phys = ((u32)FP_SEG(aligned) << 4) + FP_OFF(aligned);
    if (((phys + size - 1) & ~(boundary - 1)) != (phys & ~(boundary - 1))) {
        u32 next  = (phys + boundary) & ~(boundary - 1);
        u32 delta = next - phys;
        aligned += delta;
        phys    += delta;
    }
    for (slot = 0; slot < 4; slot++) {
        if (g_dma_allocs[slot].raw == 0) {
            g_dma_allocs[slot].raw     = raw;
            g_dma_allocs[slot].aligned = aligned;
            break;
        }
    }
    if (out_phys) *out_phys = phys;
    return aligned;
}

void dma_free(void far *buf)
{
    int slot;
    if (!buf) return;
    for (slot = 0; slot < 4; slot++) {
        if (g_dma_allocs[slot].aligned == buf) {
            _ffree(g_dma_allocs[slot].raw);
            g_dma_allocs[slot].raw     = 0;
            g_dma_allocs[slot].aligned = 0;
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

void dma_setup_8bit(u8 channel, u32 phys, u16 length)
{
    u8 ch = channel & 3;
    mask_8(ch);
    outp(0x0C, 0);                                  /* clear flip-flop */
    outp(0x0B, 0x58 | ch);                          /* mode: single, auto-init, read, ch */
    outp(dma8_addr[ch], (u8)(phys & 0xFF));
    outp(dma8_addr[ch], (u8)((phys >> 8) & 0xFF));
    outp(dma8_page[ch], (u8)((phys >> 16) & 0xFF));
    outp(dma8_count[ch], (u8)((length - 1) & 0xFF));
    outp(dma8_count[ch], (u8)(((length - 1) >> 8) & 0xFF));
    unmask_8(ch);
}

void dma_setup_16bit(u8 channel, u32 phys, u16 length)
{
    /* 16-bit channels address words: shift physical byte address right by 1
     * for the address register, but keep the page byte aligned. */
    u8 ch = (channel - 4) & 3;
    u32 word_addr = (phys >> 1);
    mask_16(ch);
    outp(0xD8, 0);
    outp(0xD6, 0x58 | ch);
    outp(dma16_addr[ch], (u8)(word_addr & 0xFF));
    outp(dma16_addr[ch], (u8)((word_addr >> 8) & 0xFF));
    outp(dma16_page[ch], (u8)((phys >> 16) & 0xFF));
    outp(dma16_count[ch], (u8)((length - 1) & 0xFF));
    outp(dma16_count[ch], (u8)(((length - 1) >> 8) & 0xFF));
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
