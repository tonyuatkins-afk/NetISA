/*
 * decode/genmidi.c - DMX OPL2 / GENMIDI.OP2 bank loader.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Parses a 175-instrument bank into an internal fm_patch_t-compatible
 * table. Designed to slot in behind midifm.c's existing patch_for() so
 * MIDI playback gains real GM instrument fidelity without rewriting the
 * sequencer.
 *
 * Per-instrument layout (offsets in 36-byte record):
 *   0x00  u16 LE  flags (bit 0 = fixed-pitch, bit 1 = delayed vib, bit 2 = double voice)
 *   0x02  u8      finetune (used to detune voice 2 in double-voice patches)
 *   0x03  u8      fixed_note (the OPL pitch for percussion / fixed-pitch)
 *   0x04..0x13    voice 1 (16 bytes)
 *   0x14..0x23    voice 2 (16 bytes, used only when flags bit 2 set)
 *
 * Per-voice 16-byte sub-record (we use only voice 1 for now -- voice 2
 * support is a follow-up; flags.bit2 patches will sound a bit thinner):
 *   0x00  mod 0x20 reg (AM/VIB/EG/KSR/MULT)
 *   0x01  mod 0x60 reg (AR/DR)
 *   0x02  mod 0x80 reg (SL/RR)
 *   0x03  mod 0xE0 reg (waveform)
 *   0x04  mod KSL  (top 2 bits of 0x40)
 *   0x05  mod TL   (bottom 6 bits of 0x40)
 *   0x06  feedback / connection (0xC0)
 *   0x07  car 0x20
 *   0x08  car 0x60
 *   0x09  car 0x80
 *   0x0A  car 0xE0
 *   0x0B  car KSL
 *   0x0C  car TL
 *   0x0D  reserved
 *   0x0E..0x0F  i16 LE note offset (signed)
 */
#pragma off (check_stack)  /* see Makefile CF16_ISR -- belt-and-braces */
#include "genmidi.h"
#include <stdio.h>
#include <string.h>
#include <malloc.h>

#define HDR_SIZE       8
#define INSTR_SIZE     36
#define VOICE1_OFFSET  4

/* Bank tables live in the FAR heap. Static near allocation pushed DGROUP
 * over 64 KB (175 * 14 bytes = 2.4 KB). Far allocation keeps DGROUP free
 * for the main player state and we only pay the indirection at note-on. */
static genmidi_patch_t far *patches;
static u8              far *fixed_notes;
static u16             far *flags_tab;
static hbool                is_loaded = HFALSE;

static void unpack_voice(const u8 *v, genmidi_patch_t out)
{
    /* Voice 1 starts 4 bytes into the record (after flags+finetune+note).
     * The voice bytes use the layout described in the file header comment. */
    out[0]  = v[0x00];                                     /* mod 0x20 */
    out[1]  = v[0x07];                                     /* car 0x20 */
    out[2]  = (u8)((v[0x04] << 6) | (v[0x05] & 0x3F));     /* mod 0x40 (KSL+TL) */
    out[3]  = (u8)((v[0x0B] << 6) | (v[0x0C] & 0x3F));     /* car 0x40 (KSL+TL) */
    out[4]  = v[0x01];                                     /* mod 0x60 (AR/DR) */
    out[5]  = v[0x08];                                     /* car 0x60 */
    out[6]  = v[0x02];                                     /* mod 0x80 (SL/RR) */
    out[7]  = v[0x09];                                     /* car 0x80 */
    out[8]  = v[0x03];                                     /* mod 0xE0 (waveform) */
    out[9]  = v[0x0A];                                     /* car 0xE0 */
    out[10] = v[0x06];                                     /* feedback/conn */
}

hbool genmidi_load(const char *path)
{
    FILE *f;
    u8    hdr[HDR_SIZE];
    u8    rec[INSTR_SIZE];
    u16   i;

    is_loaded = HFALSE;
    if (!path) return HFALSE;
    /* Lazy-allocate far-heap tables on first successful load */
    if (!patches) {
        patches     = (genmidi_patch_t far *)_fmalloc(sizeof(genmidi_patch_t) * GENMIDI_NUM_INSTR);
        fixed_notes = (u8              far *)_fmalloc(sizeof(u8)              * GENMIDI_NUM_INSTR);
        flags_tab   = (u16             far *)_fmalloc(sizeof(u16)             * GENMIDI_NUM_INSTR);
        if (!patches || !fixed_notes || !flags_tab) {
            if (patches)     { _ffree(patches);     patches     = 0; }
            if (fixed_notes) { _ffree(fixed_notes); fixed_notes = 0; }
            if (flags_tab)   { _ffree(flags_tab);   flags_tab   = 0; }
            return HFALSE;
        }
    }
    f = fopen(path, "rb");
    if (!f) return HFALSE;
    if (fread(hdr, 1, HDR_SIZE, f) != HDR_SIZE) { fclose(f); return HFALSE; }
    if (memcmp(hdr, "#OPL_II#", HDR_SIZE) != 0) { fclose(f); return HFALSE; }

    for (i = 0; i < GENMIDI_NUM_INSTR; i++) {
        if (fread(rec, 1, INSTR_SIZE, f) != INSTR_SIZE) {
            fclose(f);
            return HFALSE;
        }
        flags_tab[i]   = (u16)rec[0] | ((u16)rec[1] << 8);
        fixed_notes[i] = rec[3];
        unpack_voice(rec + VOICE1_OFFSET, patches[i]);
    }
    fclose(f);
    is_loaded = HTRUE;
    return HTRUE;
}

hbool genmidi_loaded(void)
{
    return is_loaded;
}

const genmidi_patch_t *genmidi_lookup(u8 channel, u8 program, u8 note)
{
    u16 idx;
    if (!is_loaded) return 0;
    if (channel == 9) {
        if (note < GENMIDI_PERC_BASE || note > GENMIDI_PERC_LAST) return 0;
        idx = (u16)GENMIDI_NUM_MELODIC + (u16)(note - GENMIDI_PERC_BASE);
    } else {
        idx = (u16)(program & 0x7F);
    }
    /* Caller treats this as a near pointer; for far-heap data we copy into
     * a small near-static buffer so the caller's u8[11] dereference works.
     * Only one note-on at a time means a single static is safe. */
    {
        static genmidi_patch_t scratch;
        u8 j;
        for (j = 0; j < 11; j++) scratch[j] = patches[idx][j];
        return (const genmidi_patch_t *)scratch;
    }
}

u8 genmidi_fixed_note(u8 channel, u8 program, u8 note)
{
    u16 idx;
    if (!is_loaded) return note;
    if (channel == 9) {
        if (note < GENMIDI_PERC_BASE || note > GENMIDI_PERC_LAST) return note;
        idx = (u16)GENMIDI_NUM_MELODIC + (u16)(note - GENMIDI_PERC_BASE);
        return fixed_notes[idx];
    }
    idx = (u16)(program & 0x7F);
    if (flags_tab[idx] & 0x0001) return fixed_notes[idx];
    return note;
}
