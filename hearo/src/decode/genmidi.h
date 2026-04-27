/*
 * decode/genmidi.h - DMX OPL2 instrument bank loader (GENMIDI.OP2 format).
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Loads a 175-instrument FM patch bank in the DMX format used by Doom and
 * its source ports (and HEARO once a bank is provided). The format is:
 *
 *   8 bytes  "#OPL_II#" magic
 *   175 x 36-byte instrument records (128 melodic + 47 percussion)
 *   175 x 32-byte name records
 *
 * Per-record layout per moddingwiki.shikadi.net/wiki/OP2_Bank_Format
 * (CC0). HEARO repacks the OP2 voice bytes into the local fm_patch_t
 * representation that the OPL writer understands.
 *
 * License-clean banks are available; the bundled DMXOPL bank
 * (data/banks/GENMIDI.OP2 by Sneakernets, MIT-licensed) is a
 * Sound-Canvas-tuned default.
 */
#ifndef HEARO_DECODE_GENMIDI_H
#define HEARO_DECODE_GENMIDI_H

#include "../hearo.h"

/* Number of instruments in a GENMIDI.OP2 bank. 0..127 = GM melodic,
 * 128..174 = GM percussion (note 35..81 maps to instrument 128..174). */
#define GENMIDI_NUM_INSTR    175
#define GENMIDI_NUM_MELODIC  128
#define GENMIDI_PERC_BASE    35       /* lowest GM percussion note */
#define GENMIDI_PERC_LAST    81       /* highest GM percussion note */

/* Patch is the 11-byte view used by midifm.c when writing to OPL.  */
typedef u8 genmidi_patch_t[11];

hbool                  genmidi_load   (const char *path);
hbool                  genmidi_loaded (void);
const genmidi_patch_t *genmidi_lookup (u8 channel, u8 program, u8 note);
/* For percussion (channel 9), the OP2 entry carries a fixed note that
 * overrides the MIDI note number. Returns the original note for melodic
 * channels or for unknown drums. */
u8                     genmidi_fixed_note(u8 channel, u8 program, u8 note);

#endif
