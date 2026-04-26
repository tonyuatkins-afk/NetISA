/*
 * decode/midifm.h - MIDI to OPL2/OPL3 FM dispatcher.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_DECODE_MIDIFM_H
#define HEARO_DECODE_MIDIFM_H

#include "../hearo.h"

/* Bank loading is optional: midifm operates with its built-in single patch
 * if no external bank is loaded.  GENMIDI.OP2 / DMXOPL formats land later. */
hbool midifm_load_bank(const char *path);

void  midifm_init           (hbool opl3_present);
void  midifm_note_on        (u8 channel, u8 program, u8 note, u8 velocity);
void  midifm_note_off       (u8 channel, u8 note);
void  midifm_program_change (u8 channel, u8 program);
void  midifm_control_change (u8 channel, u8 cc, u8 value);
void  midifm_silence        (void);

#endif
