/*
 * decode/midifm.c - MIDI to OPL2/OPL3 FM dispatcher with multi-patch bank.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * 16 hand-tuned patches mapped to General MIDI program groups (instruments
 * 0-127 / 8 = group 0-15). Voice allocation is round-robin across the
 * available OPL channels (9 on OPL2, 18 on OPL3). Percussion channel
 * (MIDI ch 9) gets a dedicated drum patch.
 *
 * MVP coverage: note on/off, program change (per-channel), MIDI volume CC7
 * scales the carrier output level, channel pan via OPL3 stereo bits in C0.
 */
#pragma off (check_stack)  /* see Makefile CF16_ISR -- belt-and-braces */
#include "midifm.h"
#include "genmidi.h"
#include "../audio/adlib.h"
#include <conio.h>
#include <string.h>

/* Direct port I/O for the SB16 mixer unmute below. */
extern unsigned int  inp(unsigned int port);
extern unsigned int  outp(unsigned int port, unsigned int val);

#define OPL_CHANNELS_OPL2 9
#define OPL_CHANNELS_OPL3 18

/* Patch layout: mod_char, car_char, mod_scale, car_scale,
                 mod_attack, car_attack, mod_sustain, car_sustain,
                 mod_wave, car_wave, feedback. */
typedef u8 fm_patch_t[11];

/* 16 patches keyed by GM program / 8. Hand-tuned approximations; replace
 * with a GENMIDI.OP2 / DMXOPL bank in a later pass for real fidelity. */
static const fm_patch_t bank[16] = {
    /* 0 piano       */ { 0x21, 0x21, 0x4F, 0x00, 0xF1, 0xF2, 0x53, 0x74, 0x00, 0x00, 0x06 },
    /* 1 chrom perc  */ { 0x01, 0x21, 0x1F, 0x00, 0xF8, 0xF1, 0x05, 0x07, 0x00, 0x00, 0x0E },
    /* 2 organ       */ { 0x71, 0x72, 0x00, 0x00, 0xF0, 0xE0, 0xF1, 0xF1, 0x00, 0x00, 0x0E },
    /* 3 guitar      */ { 0x21, 0x31, 0x9C, 0x00, 0xF3, 0xF1, 0x05, 0x07, 0x00, 0x00, 0x0A },
    /* 4 bass        */ { 0x31, 0x21, 0x12, 0x00, 0xF1, 0xF1, 0x14, 0x05, 0x00, 0x00, 0x06 },
    /* 5 strings     */ { 0xE1, 0xE1, 0x1A, 0x00, 0x73, 0x74, 0x70, 0x71, 0x00, 0x00, 0x0E },
    /* 6 ensemble    */ { 0x61, 0x21, 0x1A, 0x00, 0x73, 0x83, 0x80, 0x90, 0x00, 0x00, 0x0E },
    /* 7 brass       */ { 0x21, 0x21, 0x9C, 0x40, 0xF3, 0xF1, 0x05, 0x07, 0x02, 0x02, 0x06 },
    /* 8 reed        */ { 0x21, 0x21, 0x14, 0x00, 0xF1, 0xF2, 0x05, 0x06, 0x00, 0x00, 0x0E },
    /* 9 pipe        */ { 0xA1, 0x21, 0x9F, 0x00, 0x73, 0x75, 0x53, 0x57, 0x00, 0x00, 0x0E },
    /*10 synth lead  */ { 0x32, 0x21, 0x80, 0x40, 0xF1, 0xF1, 0xF8, 0xF8, 0x00, 0x00, 0x0E },
    /*11 synth pad   */ { 0xE1, 0xE1, 0x18, 0x00, 0x73, 0x74, 0x70, 0x71, 0x00, 0x00, 0x0C },
    /*12 synth FX    */ { 0x01, 0x21, 0x4F, 0x00, 0xF1, 0xF2, 0x05, 0x05, 0x01, 0x01, 0x0A },
    /*13 ethnic      */ { 0xE1, 0xE1, 0x1A, 0x00, 0x73, 0x74, 0x70, 0x71, 0x00, 0x00, 0x0E },
    /*14 percussion  */ { 0x0F, 0x0F, 0x00, 0x00, 0xF8, 0xF8, 0x77, 0x77, 0x00, 0x00, 0x06 },
    /*15 SFX         */ { 0x01, 0x01, 0x3F, 0x00, 0xF8, 0xF8, 0x77, 0x77, 0x01, 0x01, 0x0E }
};

/* Drum patch for MIDI percussion channel (ch 9) when no specific note map. */
static const fm_patch_t drum_patch =
    { 0x0E, 0x0E, 0x00, 0x00, 0xF8, 0xF8, 0xF7, 0xF7, 0x00, 0x00, 0x00 };

static const u8 op_offset[9] = { 0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12 };
static const u16 fnum_octave0[12] = {
    0x158, 0x16D, 0x183, 0x19A, 0x1B2, 0x1CC,
    0x1E7, 0x204, 0x223, 0x244, 0x267, 0x28C
};

typedef struct {
    u8 active;
    u8 midi_channel;
    u8 note;
} fm_voice_t;

static fm_voice_t voices[OPL_CHANNELS_OPL3];
static u8         voice_count;
static u8         next_alloc;
static u8         channel_program[16];        /* GM program per MIDI channel */
static u8         channel_volume [16];        /* CC7 0..127 per MIDI channel */
static u8         channel_pan    [16];        /* CC10 0..127 per MIDI channel */

static const fm_patch_t *patch_for(u8 midi_channel, u8 program, u8 note)
{
    /* If a GENMIDI.OP2 bank was loaded, use its 175 patches. */
    if (genmidi_loaded()) {
        const genmidi_patch_t *gp = genmidi_lookup(midi_channel, program, note);
        if (gp) return (const fm_patch_t *)gp;       /* layouts are identical */
    }
    /* Fallback: 16-patch hand-tuned built-in bank, plus single drum patch. */
    if (midi_channel == 9) return &drum_patch;
    return &bank[(program & 0x7F) >> 3];
}

static void program_voice(u8 v, const fm_patch_t *patch, u8 stereo_bits)
{
    u8 op_mod = op_offset[v % 9];
    u8 op_car = op_mod + 3;
    u8 connection_byte = (*patch)[10] | stereo_bits;
    if (v < 9) {
        adlib_write(0x20 + op_mod, (*patch)[0]);
        adlib_write(0x20 + op_car, (*patch)[1]);
        adlib_write(0x40 + op_mod, (*patch)[2]);
        adlib_write(0x40 + op_car, (*patch)[3]);
        adlib_write(0x60 + op_mod, (*patch)[4]);
        adlib_write(0x60 + op_car, (*patch)[5]);
        adlib_write(0x80 + op_mod, (*patch)[6]);
        adlib_write(0x80 + op_car, (*patch)[7]);
        adlib_write(0xE0 + op_mod, (*patch)[8]);
        adlib_write(0xE0 + op_car, (*patch)[9]);
        adlib_write(0xC0 + (v % 9), connection_byte);
    } else {
        u8 c = v - 9;
        adlib_write_b(0x20 + op_mod, (*patch)[0]);
        adlib_write_b(0x20 + op_car, (*patch)[1]);
        adlib_write_b(0x40 + op_mod, (*patch)[2]);
        adlib_write_b(0x40 + op_car, (*patch)[3]);
        adlib_write_b(0x60 + op_mod, (*patch)[4]);
        adlib_write_b(0x60 + op_car, (*patch)[5]);
        adlib_write_b(0x80 + op_mod, (*patch)[6]);
        adlib_write_b(0x80 + op_car, (*patch)[7]);
        adlib_write_b(0xE0 + op_mod, (*patch)[8]);
        adlib_write_b(0xE0 + op_car, (*patch)[9]);
        adlib_write_b(0xC0 + c, connection_byte);
    }
}

void midifm_init(hbool opl3_present)
{
    u8 i;
    voice_count = opl3_present ? OPL_CHANNELS_OPL3 : OPL_CHANNELS_OPL2;
    next_alloc = 0;
    memset(voices, 0, sizeof(voices));
    for (i = 0; i < 16; i++) {
        channel_program[i] = 0;        /* default to acoustic piano */
        channel_volume [i] = 100;      /* CC7 default per GM spec */
        channel_pan    [i] = 64;       /* center */
    }
    /* Tell the AdLib driver that OPL3 is reachable. Without this, when SB
     * (not AdLib) is the active driver and a_init has not run, adlib_write_b
     * short-circuits and our 18-voice mode silently drops half the voices. */
    adlib_set_opl3(opl3_present);

    /* Enable OPL3 mode and waveform select. The AdLib driver only runs
     * a_init when adlib is the *active* driver; with SB as primary, the
     * OPL3 NEW bit (reg 0x105 = 0x01) and the OPL2 waveform-select enable
     * (reg 0x01 = 0x20) would be left at their cold-power-up defaults,
     * which silences the second 9-voice bank on OPL3. Set them ourselves
     * so MIDI on a YMF715 / OPL3 actually gets 18-voice fidelity. */
    if (opl3_present) {
        adlib_write_b(0x05, 0x01);
        adlib_write  (0x01, 0x20);
    } else {
        adlib_write  (0x01, 0x20);
    }

    /* SB16 mixer init: if we ended up on a Sound Blaster Pro/16/AWE the
     * default mixer state can leave FM channel volume at zero so even
     * correctly-driven OPL output is silent. Unmute master + FM by writing
     * the SB mixer regs (port BLASTER+4 index, +5 data) at full level.
     * On non-SB systems these writes go to ports nothing's listening on
     * and have no effect. */
    {
        u16 mix_idx = 0x224;       /* BLASTER A220 default */
        u16 mix_dat = 0x225;
        static const u8 mixer_pairs[] = {
            0x22, 0xFF,    /* legacy master */
            0x26, 0xFF,    /* legacy FM */
            0x30, 0xFF,    /* SB16 master L */
            0x31, 0xFF,    /* SB16 master R */
            0x34, 0xFF,    /* SB16 FM L */
            0x35, 0xFF,    /* SB16 FM R */
            0x3E, 0xFF     /* output mixer */
        };
        u8 j;
        for (j = 0; j < (u8)sizeof(mixer_pairs); j += 2) {
            outp(mix_idx, mixer_pairs[j]);
            (void)inp(mix_idx);
            outp(mix_dat, mixer_pairs[j + 1]);
        }
    }
}

hbool midifm_load_bank(const char *path)
{
    return genmidi_load(path);
}

static void note_to_fnum(u8 note, u16 *fnum, u8 *block)
{
    /* OPL block field is 3 bits (0..7).  Without clamping, MIDI notes above
     * B-7 (=95) push octave to 8+ which the OPL silently masks back to 0,
     * dropping notes by 8 octaves.  Constrain note range so block stays valid. */
    u8 n = note;
    u8 octave;
    if (n < 12) n = 12;       /* C-0 */
    if (n > 95) n = 95;       /* B-7, last note that yields block <= 7 */
    octave = (n / 12) - 1;    /* 0..7 */
    *fnum = fnum_octave0[n % 12];
    *block = octave;
}

void midifm_note_on(u8 channel, u8 program_unused, u8 note, u8 velocity)
{
    u8  v = next_alloc;
    u16 fnum;
    u8  block;
    u8  attn;
    u8  effective_vel;
    u8  stereo_bits;
    const fm_patch_t *p;
    (void)program_unused;
    next_alloc = (next_alloc + 1) % voice_count;
    voices[v].active       = 1;
    voices[v].midi_channel = channel;
    voices[v].note         = note;

    /* GENMIDI.OP2 percussion entries carry a fixed OPL pitch. For melodic
     * instruments and the fallback bank, the MIDI note drives pitch as usual. */
    note = genmidi_fixed_note(channel, channel_program[channel], note);
    voices[v].note = note;
    p = patch_for(channel, channel_program[channel], note);
    /* OPL3 stereo bits in 0xCx: bit 4 = right, bit 5 = left. Bias by pan. */
    if (channel_pan[channel] < 32)       stereo_bits = 0x20;          /* left */
    else if (channel_pan[channel] > 96)  stereo_bits = 0x10;          /* right */
    else                                 stereo_bits = 0x30;          /* center */
    program_voice(v, p, stereo_bits);

    /* Carrier total level: combine note velocity with channel volume.
     * Both factors are 0..127; product / 127 stays 0..127. Map to 0..63
     * attenuation (lower = louder). */
    effective_vel = (u8)(((u16)velocity * channel_volume[channel]) / 127);
    attn = (effective_vel >= 127) ? 0 : ((127 - effective_vel) >> 1);

    note_to_fnum(note, &fnum, &block);
    if (v < 9) {
        u8 op_car = op_offset[v] + 3;
        adlib_write(0x40 + op_car, attn);
        adlib_write(0xA0 + v, (u8)(fnum & 0xFF));
        adlib_write(0xB0 + v, ((block & 0x07) << 2) | ((fnum >> 8) & 0x03) | 0x20);
    } else {
        u8 c = v - 9;
        u8 op_car = op_offset[c] + 3;
        adlib_write_b(0x40 + op_car, attn);
        adlib_write_b(0xA0 + c, (u8)(fnum & 0xFF));
        adlib_write_b(0xB0 + c, ((block & 0x07) << 2) | ((fnum >> 8) & 0x03) | 0x20);
    }
}

void midifm_note_off(u8 channel, u8 note)
{
    u8 v;
    for (v = 0; v < voice_count; v++) {
        if (voices[v].active && voices[v].midi_channel == channel
            && voices[v].note == note)
        {
            if (v < 9) adlib_write(0xB0 + v, 0);
            else       adlib_write_b(0xB0 + (v - 9), 0);
            voices[v].active = 0;
            return;
        }
    }
}

void midifm_silence(void)
{
    u8 v;
    for (v = 0; v < 9; v++) adlib_write(0xB0 + v, 0);
    for (v = 0; v < 9; v++) adlib_write_b(0xB0 + v, 0);
    memset(voices, 0, sizeof(voices));
}

/* New entry points for MIDI controller / program-change events.  Called from
 * decode/midi.c's dispatch_event. */
void midifm_program_change(u8 channel, u8 program)
{
    if (channel >= 16) return;
    channel_program[channel] = program & 0x7F;
}

void midifm_control_change(u8 channel, u8 cc, u8 value)
{
    if (channel >= 16) return;
    switch (cc) {
    case 7:   channel_volume[channel] = value & 0x7F; break;
    case 10:  channel_pan   [channel] = value & 0x7F; break;
    case 121: /* reset all controllers */
        channel_volume[channel] = 100;
        channel_pan   [channel] = 64;
        break;
    case 123: /* all notes off */
        {
            u8 v;
            for (v = 0; v < voice_count; v++) {
                if (voices[v].active && voices[v].midi_channel == channel) {
                    if (v < 9) adlib_write(0xB0 + v, 0);
                    else       adlib_write_b(0xB0 + (v - 9), 0);
                    voices[v].active = 0;
                }
            }
        }
        break;
    default: break;
    }
}
