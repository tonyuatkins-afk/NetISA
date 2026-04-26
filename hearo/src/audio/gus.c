/*
 * audio/gus.c - Gravis UltraSound GF1 driver (hardware-mixed voices).
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * GUS is the odd one out: the software mixer is bypassed.  Samples live in
 * card DRAM (256K to 1M depending on board), and the GF1 mixes up to 32
 * voices internally before sending stereo audio to its on-board codec.
 *
 * This implementation handles enough register programming to upload a
 * sample, trigger a voice, set its frequency / volume / pan, and stop it.
 * Volume ramping and the GUS-specific codec features are stubs for now.
 */
#include "audiodrv.h"
#include <conio.h>
#include <i86.h>
#include <stdlib.h>
#include <string.h>

#define DRAM_SIZE_DEFAULT 0x40000UL    /* 256K floor; real cards have more */

/* Sample tables go on the heap; carrying them in DGROUP would cost >2K. */
typedef struct {
    u16 base;
    u32 dram_used;
    u32 dram_size;
    u32 sample_addr[256];
    u32 sample_len [256];
    u8  sample_bits[256];
} gus_state_t;

static gus_state_t far *Gp;
#define G (*Gp)

static void sel_voice(u8 v) { outp(G.base + 0x102, v); }

static void wreg8(u8 reg, u8 v)
{
    outp(G.base + 0x103, reg);
    outp(G.base + 0x105, v);
}

static void wreg16(u8 reg, u16 v)
{
    outp(G.base + 0x103, reg);
    outpw(G.base + 0x104, v);
}

static void poke_dram(u32 addr, u8 v)
{
    /* The five-port sequence (register select, low addr, register select,
     * high addr, data) MUST NOT be split by an interrupt: the GF1 register
     * select latch (port 0x103) is shared across reads and writes, so an
     * IRQ handler that touches any GUS register between the address-low
     * write and the data write leaves the chip pointing at the wrong
     * register and our data byte lands at a random DRAM offset. cli for
     * the duration. */
    _disable();
    outp(G.base + 0x103, 0x43);
    outpw(G.base + 0x104, (u16)(addr & 0xFFFF));
    outp(G.base + 0x103, 0x44);
    outp(G.base + 0x105, (u8)(addr >> 16));
    outp(G.base + 0x107, v);
    _enable();
}

static hbool g_init(const hw_profile_t *hw)
{
    /* Gp persists across init/shutdown cycles so audiodrv_auto_select can
     * re-enter g_init (e.g. after a fallback to null and back) without
     * leaking the previous allocation. memset clears state in place. */
    if (!Gp) Gp = (gus_state_t far *)malloc(sizeof(gus_state_t));
    if (!Gp) return HFALSE;
    memset(Gp, 0, sizeof(gus_state_t));
    G.base = hw->gus.base ? hw->gus.base : 0x240;
    G.dram_size = (hw->gus.ram_kb ? (u32)hw->gus.ram_kb << 10 : DRAM_SIZE_DEFAULT);
    /* Reset GF1: write 0 then 1 to register 0x4C. */
    sel_voice(0);
    wreg8(0x4C, 0x00);
    wreg8(0x4C, 0x01);
    /* Default: 14 active voices = 44.1 kHz mix rate.  More voices = lower
     * per-voice rate; tracker code can crank this up later. */
    wreg8(0x0E, (14 - 1) | 0xC0);
    return HTRUE;
}

static void g_shutdown(void)
{
    u8 i;
    if (!Gp) return;
    for (i = 0; i < 32; i++) {
        sel_voice(i);
        wreg8(0x00, 0x03);                       /* stop voice */
    }
}

static hbool g_open(u32 r, u8 f, audio_callback_t cb) { (void)r; (void)f; (void)cb; return HTRUE; }
static void  g_close(void) {}
static void  g_volume(u8 v) { (void)v; }
static void  g_caps(audio_caps_t *c)
{
    c->name = "Gravis UltraSound"; c->chip = "GF1";
    c->formats = 0; c->max_rate = 44100;
    c->max_channels = 2; c->max_bits = 16;
    c->has_hardware_mix = HTRUE;
    c->hardware_voices = 32;
    c->sample_ram = Gp ? G.dram_size : 0;
}

static hbool g_upload(u16 id, const void *data, u32 len, u8 bits)
{
    const u8 *src = (const u8 *)data;
    u32 i;
    if (!Gp) return HFALSE;
    if (id >= 256) return HFALSE;
    if (G.dram_used + len > G.dram_size) return HFALSE;
    G.sample_addr[id] = G.dram_used;
    G.sample_len [id] = len;
    G.sample_bits[id] = bits;
    for (i = 0; i < len; i++) poke_dram(G.dram_used + i, src[i]);
    G.dram_used += len;
    return HTRUE;
}

static void g_trigger(u8 channel, u16 sample_id, u32 freq, u8 vol, u8 pan)
{
    u32 addr;
    u32 end;
    if (!Gp) return;
    if (channel >= 32 || sample_id >= 256) return;
    if (!G.sample_len[sample_id]) return;
    addr = G.sample_addr[sample_id];
    end  = addr + G.sample_len[sample_id];
    sel_voice(channel);
    wreg8 (0x00, 0x03);                          /* stop while we set up */
    wreg16(0x01, (u16)(freq >> 1));              /* frequency control */
    wreg16(0x02, (u16)(addr >> 7));              /* start hi */
    wreg16(0x03, (u16)(addr << 9));              /* start lo */
    wreg16(0x04, (u16)(end  >> 7));
    wreg16(0x05, (u16)(end  << 9));
    wreg8 (0x09, (u8)(vol >> 1));                /* current volume */
    wreg8 (0x0C, pan >> 4);                      /* pan: 0..15 */
    wreg8 (0x00, 0x00);                          /* go: looped if bit 3 set */
}

static void g_stop(u8 channel)
{
    if (channel >= 32) return;
    sel_voice(channel);
    wreg8(0x00, 0x03);
}

const audio_driver_t gus_driver = {
    "gus", g_init, g_shutdown, g_open, g_close, g_volume, g_caps,
    g_upload, g_trigger, g_stop
};
