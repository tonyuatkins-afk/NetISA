/*
 * audio/adlib.c - AdLib / OPL2 / OPL3 FM synthesis driver.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Programs the OPL register file at base+0/1 (OPL2 + OPL3 bank 0) and
 * base+2/3 (OPL3 bank 1).  Exposes a thin note-on / note-off interface used
 * by the MIDI sequencer; PCM playback is not supported on FM hardware.
 *
 * The audio_driver_t open/close hooks are no-ops because OPL has no DMA
 * stream.  The driver still claims to be "playing" so the higher layers
 * have a target to talk to.
 */
#pragma off (check_stack)  /* see Makefile CF16_ISR -- belt-and-braces */
#include "audiodrv.h"
#include <conio.h>

#define OPL_BASE 0x388

static u16 opl_base = OPL_BASE;
static u8  opl3_present;

/* OPL timing: use ISA-bus-paced reads of port 0x80 (manufacturer test port,
 * always present, ~1us per read on any ISA bus regardless of CPU speed).
 * This is what vgmslap and most reference DOS code do; counted NOPs are CPU
 * speed dependent and break on fast emulators. */
static void delay_short(void)
{
    int i;
    for (i = 0; i < 6; i++) (void)inp(0x80);
}

static void delay_long(void)
{
    int i;
    for (i = 0; i < 35; i++) (void)inp(0x80);
}

void adlib_write(u8 reg, u8 val)
{
    outp(opl_base, reg);
    delay_short();
    outp(opl_base + 1, val);
    delay_long();
}

void adlib_write_b(u8 reg, u8 val)
{
    if (!opl3_present) return;
    outp(opl_base + 2, reg);
    delay_short();
    outp(opl_base + 3, val);
    delay_long();
}

/* Allow callers like midifm to force OPL3 mode on when the AdLib driver
 * itself is not the active audio driver (e.g., SB16 owns DMA but the OPL3
 * chip lives on the same card and is reachable at 388/389/38A/38B). Without
 * this, midifm's bank-B writes for voices 9..17 are silently dropped. */
void adlib_set_opl3(hbool present)
{
    opl3_present = present ? 1 : 0;
}

static void adlib_silence(void)
{
    u8 i;
    for (i = 0; i < 9; i++) {
        adlib_write((u8)(0xB0 + i), 0);     /* key off, frequency low high */
        adlib_write((u8)(0xA0 + i), 0);
    }
    if (opl3_present) {
        for (i = 0; i < 9; i++) {
            adlib_write_b((u8)(0xB0 + i), 0);
            adlib_write_b((u8)(0xA0 + i), 0);
        }
    }
}

static hbool a_init(const hw_profile_t *hw)
{
    opl3_present = (hw->opl == OPL_OPL3);
    /* OPL3 needs the "new" bit set in register 0x105 to expose the second
     * register set.  Doing this on an OPL2 has no effect. */
    if (opl3_present) {
        adlib_write_b(0x05, 0x01);
        adlib_write(0x01, 0x20);            /* enable waveform select on OPL2 */
    } else {
        adlib_write(0x01, 0x20);
    }
    adlib_silence();
    return HTRUE;
}

static void a_shutdown(void) { adlib_silence(); }
static hbool a_open(u32 r, u8 f, audio_callback_t cb) { (void)r; (void)f; (void)cb; return HTRUE; }
static void  a_close(void) { adlib_silence(); }
static void  a_volume(u8 v) { (void)v; /* per-voice volume goes through midifm */ }
static void  a_caps(audio_caps_t *c)
{
    c->name = opl3_present ? "AdLib (OPL3)" : "AdLib (OPL2)";
    c->chip = opl3_present ? "Yamaha YMF262" : "Yamaha YM3812";
    c->formats = 0;
    c->max_rate = 0;
    c->max_channels = 2; c->max_bits = 0;
    c->has_hardware_mix = HTRUE;
    c->hardware_voices  = opl3_present ? 18 : 9;
    c->sample_ram = 0;
}

const audio_driver_t adlib_driver = {
    "adlib", a_init, a_shutdown, a_open, a_close, a_volume, a_caps, 0, 0, 0
};
