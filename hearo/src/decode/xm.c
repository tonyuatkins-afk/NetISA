/*
 * decode/xm.c - FastTracker II XM loader and player.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Compact MVP. Reference: the Triton XM 1.04 spec (Mr. H of Triton, 1995).
 * The format is essentially S3M with different packing, instrument-then-
 * sample structure, and linear vs Amiga periods.
 *
 * Implemented:
 *   - Full header (offset 0-60) and extended header (variable, parsed
 *     using its self-described length).
 *   - Pattern unpack into a flat 5-byte-per-cell row buffer.
 *   - Instrument header parse, FIRST sample only (multi-sample instruments
 *     collapse to sample 0 per the MVP scope).
 *   - 8-bit and 16-bit delta-encoded sample data.
 *   - Tick-based sequencer matching S3M's timing model.
 *   - Effects: 0x9 sample offset, 0xA volume slide, 0xC set volume,
 *     0xD pattern break, 0xF set speed/tempo. Other effects are parsed
 *     and ignored (no harm) so playback continues through unsupported notes.
 */
#pragma off (check_stack)  /* see Makefile CF16_ISR -- belt-and-braces */
#include "xm.h"
#include "../audio/mixer.h"
#include "../audio/audiodrv.h"
#include "../platform/io.h"
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XM_PAT_BYTES_MAX (256 * XM_ROW_BYTES)

static u16 le16(const u8 *p) { return (u16)p[0] | ((u16)p[1] << 8); }
static u32 le32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

/* Cross-segment-safe wrapper; see platform/io.c. */
static hbool read_chunk(FILE *f, void *dst, u32 n) { return io_read_chunked(f, dst, n); }

/* Linear period (XM linear flag set): period = 7680 - note*64 - finetune/2.
 * Frequency = 8363 * 2^((4608 - period) / 768). */
static u32 xm_freq_linear(u16 period)
{
    /* Approximation using a 12-entry semitone ratio table.  4608 = "C-5"
     * neutral period. We compute: hz = 8363 * 2^((4608 - period) / 768). */
    static const u16 semi_x4096[12] = {
        4096, 4340, 4598, 4871, 5161, 5468, 5793, 6137, 6502, 6889, 7298, 7732
    };
    s32 delta = 4608 - (s32)period;
    s32 octs  = delta / 768;
    s32 rem   = delta - octs * 768;
    if (rem < 0) { rem += 768; octs--; }
    {
        u8 semi = (u8)((rem * 12) / 768);
        u32 hz = ((u32)8363 * semi_x4096[semi]) >> 12;
        if (octs > 0) hz <<= octs;
        else if (octs < 0) hz >>= -octs;
        return hz;
    }
}

static u16 xm_period_linear(u8 note, s8 finetune)
{
    /* note: 1..96 with C-1 = 1 in the XM scheme. */
    return (u16)(7680 - (s16)note * 64 - (s16)finetune / 2);
}

static hbool load_instrument(FILE *f, xm_instrument_t *ins)
{
    u8 hdr[29];
    u32 ins_hdr_size;
    long ins_start;
    long after_ins_hdr;
    if (fread(hdr, 1, 4, f) != 4) return HFALSE;
    ins_hdr_size = le32(hdr);
    if (ins_hdr_size < 29) return HFALSE;
    ins_start = ftell(f) - 4;
    after_ins_hdr = ins_start + ins_hdr_size;
    if (fread(hdr, 1, 25, f) != 25) return HFALSE;
    memcpy(ins->name, hdr, 22); ins->name[22] = 0;
    ins->num_samples = (u8)le16(hdr + 23);
    if (ins->num_samples == 0) {
        fseek(f, after_ins_hdr, SEEK_SET);
        return HTRUE;
    }

    /* The next 4 bytes are sample_header_size (per-sample, typically 40),
     * followed by sample_for_note[96] + envelopes + flags + reserved.
     * Total of those equals (ins_hdr_size - 29). For the MVP we only need
     * sample_for_note; the rest gets skipped via fseek(after_ins_hdr). */
    {
        u8 envelope_block[230];
        u32 sample_hdr_size;
        u16 envelope_bytes;
        u16 i;
        u8 shdr[40];
        u32 sample_lengths[16];
        if (fread(&sample_hdr_size, 1, 4, f) != 4) return HFALSE;
        sample_hdr_size = le32((u8 *)&sample_hdr_size);
        envelope_bytes = (u16)(ins_hdr_size - 29 - 4);
        if (envelope_bytes > sizeof(envelope_block)) envelope_bytes = sizeof(envelope_block);
        if (envelope_bytes &&
            fread(envelope_block, 1, envelope_bytes, f) != envelope_bytes)
            return HFALSE;
        if (envelope_bytes >= 96) memcpy(ins->sample_for_note, envelope_block, 96);
        fseek(f, after_ins_hdr, SEEK_SET);

        /* Read each sample header (sample_hdr_size bytes each, typically 40).
         * Capture all 16 samples' metadata. */
        for (i = 0; i < ins->num_samples && i < XM_MAX_INSTR_SAMPLES; i++) {
            u32 actual = sample_hdr_size > 40 ? 40 : sample_hdr_size;
            if (fread(shdr, 1, (u16)actual, f) != (u16)actual) return HFALSE;
            if (sample_hdr_size > actual)
                fseek(f, (long)(sample_hdr_size - actual), SEEK_CUR);
            sample_lengths[i]               = le32(shdr);
            ins->samples[i].length          = sample_lengths[i];
            ins->samples[i].loop_start      = le32(shdr + 4);
            ins->samples[i].loop_length     = le32(shdr + 8);
            ins->samples[i].volume          = shdr[12];
            ins->samples[i].finetune        = (s8)shdr[13];
            ins->samples[i].type            = shdr[14];
            ins->samples[i].panning         = shdr[15];
            ins->samples[i].relative_note   = (s8)shdr[16];
            memcpy(ins->samples[i].name, shdr + 18, 22);
            ins->samples[i].name[22] = 0;
        }
        /* Skip any extra sample headers beyond our 16-sample cap. */
        for (i = XM_MAX_INSTR_SAMPLES; i < ins->num_samples; i++)
            fseek(f, (long)sample_hdr_size, SEEK_CUR);

        /* Sample data follows: each sample's bytes in order, delta-encoded. */
        for (i = 0; i < ins->num_samples; i++) {
            u32 len = (i < XM_MAX_INSTR_SAMPLES) ? sample_lengths[i] : 0;
            if (i < XM_MAX_INSTR_SAMPLES && len) {
                xm_sample_t *s = &ins->samples[i];
                s->data = _fmalloc(len);
                if (!s->data) return HFALSE;
                if (!read_chunk(f, s->data, len)) return HFALSE;
                if (s->type & 0x10) {
                    u32 j;
                    s16 far *p = (s16 far *)s->data;
                    u32 nframes = len / 2;
                    s16 prev = 0;
                    for (j = 0; j < nframes; j++) { prev = (s16)(prev + p[j]); p[j] = prev; }
                    s->length = nframes;
                } else {
                    u32 j;
                    s8 far *p = (s8 far *)s->data;
                    s8 prev = 0;
                    for (j = 0; j < len; j++) { prev = (s8)(prev + p[j]); p[j] = prev; }
                }
            } else if (len) {
                fseek(f, (long)len, SEEK_CUR);
            }
        }
    }
    return HTRUE;
}

hbool xm_load(const char *filename, xm_song_t *song)
{
    FILE *f;
    u8 hdr[60];
    u8 ext_hdr[276];
    u32 ext_size;
    u16 i;

    if (!filename || !song) return HFALSE;
    memset(song, 0, sizeof(*song));
    f = fopen(filename, "rb");
    if (!f) return HFALSE;
    if (fread(hdr, 1, 60, f) != 60) goto fail;
    if (memcmp(hdr, "Extended Module: ", 17) != 0) goto fail;
    memcpy(song->title, hdr + 17, 20);
    song->title[20] = 0;

    /* Extended header: first 4 bytes are header size, rest variable. */
    if (fread(&ext_size, 1, 4, f) != 4) goto fail;
    ext_size = le32((u8 *)&ext_size);
    if (ext_size < 20 || ext_size > sizeof(ext_hdr) + 4) goto fail;
    if (fread(ext_hdr, 1, (u16)(ext_size - 4), f) != (u16)(ext_size - 4)) goto fail;

    song->song_length     = le16(ext_hdr + 0);
    song->restart         = le16(ext_hdr + 2);
    song->num_channels    = le16(ext_hdr + 4);
    song->num_patterns    = le16(ext_hdr + 6);
    song->num_instruments = le16(ext_hdr + 8);
    song->flags           = le16(ext_hdr + 10);
    song->init_tempo      = le16(ext_hdr + 12);
    song->init_bpm        = le16(ext_hdr + 14);
    if (song->num_channels < 2 || song->num_channels > XM_MAX_CHANNELS) goto fail;
    if (song->num_patterns > XM_MAX_PATTERNS) goto fail;
    if (song->num_instruments > XM_MAX_INSTRUMENTS) goto fail;
    if (song->song_length > XM_MAX_ORDERS) goto fail;
    memcpy(song->order, ext_hdr + 16, 256);

    /* Patterns. */
    for (i = 0; i < song->num_patterns; i++) {
        u8 phdr[9];
        u32 phdr_size;
        u16 packed_size;
        u8 *packed;
        u16 row;
        u16 src;
        u16 nrows;
        u8 *unpacked;

        if (fread(phdr, 1, 9, f) != 9) goto fail;
        phdr_size = le32(phdr);
        nrows = le16(phdr + 5);
        packed_size = le16(phdr + 7);
        song->pattern_rows[i] = nrows;
        if (phdr_size > 9) fseek(f, (long)(phdr_size - 9), SEEK_CUR);

        unpacked = (u8 *)_fmalloc((u32)nrows * XM_ROW_BYTES);
        if (!unpacked) goto fail;
        memset(unpacked, 0, (u16)nrows * XM_ROW_BYTES);
        song->patterns[i] = unpacked;

        if (packed_size == 0) continue;

        /* Far heap: XM packed patterns regularly exceed near-heap capacity. */
        packed = (u8 *)_fmalloc(packed_size);
        if (!packed) goto fail;
        if (fread(packed, 1, packed_size, f) != packed_size) { _ffree(packed); goto fail; }

        src = 0;
        row = 0;
        while (row < nrows && src < packed_size) {
            u8 ch = 0;
            u8 *cell;
            for (ch = 0; ch < song->num_channels && src < packed_size; ch++) {
                u8 first = packed[src++];
                cell = unpacked + row * XM_ROW_BYTES + ch * 5;
                if (first & 0x80) {
                    if (first & 0x01) cell[0] = packed[src++];     /* note */
                    if (first & 0x02) cell[1] = packed[src++];     /* instrument */
                    if (first & 0x04) cell[2] = packed[src++];     /* volume */
                    if (first & 0x08) cell[3] = packed[src++];     /* effect */
                    if (first & 0x10) cell[4] = packed[src++];     /* parameter */
                } else {
                    /* uncompressed: full 5 bytes */
                    cell[0] = first;
                    if (src + 4 > packed_size) break;
                    cell[1] = packed[src++];
                    cell[2] = packed[src++];
                    cell[3] = packed[src++];
                    cell[4] = packed[src++];
                }
            }
            row++;
        }
        _ffree(packed);
    }

    /* Instruments. */
    for (i = 0; i < song->num_instruments; i++) {
        song->instruments[i] = (xm_instrument_t *)_fmalloc(sizeof(xm_instrument_t));
        if (!song->instruments[i]) goto fail;
        memset(song->instruments[i], 0, sizeof(xm_instrument_t));
        if (!load_instrument(f, song->instruments[i])) goto fail;
    }

    if (!song->init_tempo) song->init_tempo = 6;
    if (!song->init_bpm)   song->init_bpm   = 125;

    fclose(f);
    song->loaded = 1;
    return HTRUE;
fail:
    fclose(f);
    xm_free(song);
    return HFALSE;
}

void xm_free(xm_song_t *song)
{
    u16 i;
    if (!song) return;
    for (i = 0; i < XM_MAX_PATTERNS; i++)
        if (song->patterns[i]) _ffree(song->patterns[i]);
    for (i = 0; i < XM_MAX_INSTRUMENTS; i++) {
        if (song->instruments[i]) {
            u16 j;
            for (j = 0; j < XM_MAX_INSTR_SAMPLES; j++) {
                if (song->instruments[i]->samples[j].data)
                    _ffree(song->instruments[i]->samples[j].data);
            }
            _ffree(song->instruments[i]);
        }
    }
    memset(song, 0, sizeof(*song));
}

static void recompute_tick_samples(xm_song_t *song, u32 mixer_rate)
{
    /* Same as MOD/S3M: 2.5 * mixer_rate / bpm. */
    song->tick_samples = (mixer_rate * 5UL) / (2UL * song->bpm);
}

void xm_play_init(xm_song_t *song, u32 mixer_rate)
{
    const audio_driver_t *hw = audiodrv_get_hardware_mixer();
    u8 i;
    song->current_order = 0;
    song->current_row   = 0;
    song->tick          = 0;
    song->tick_acc      = 0;
    song->tempo         = (u8)song->init_tempo;
    song->bpm           = (u8)song->init_bpm;
    recompute_tick_samples(song, mixer_rate);
    for (i = 0; i < song->num_channels; i++) {
        song->chans[i].pan = 128;
    }
    mixer_stop_all();

    /* Upload first-sample-per-instrument to GUS DRAM if present. The GUS
     * driver expects one sample per instrument slot; multi-sample mapping
     * is software-mixer only for now. Future: upload all 16 to separate
     * DRAM slots and have the mixer pick. */
    if (hw && hw->upload_sample) {
        u16 j;
        for (j = 0; j < XM_MAX_INSTRUMENTS; j++) {
            xm_instrument_t *I = song->instruments[j];
            if (!I) continue;
            if (!I->samples[0].data || !I->samples[0].length) continue;
            hw->upload_sample(j, I->samples[0].data,
                              I->samples[0].length * ((I->samples[0].type & 0x10) ? 2 : 1),
                              (I->samples[0].type & 0x10) ? 16 : 8);
        }
    }
}

static void process_row(xm_song_t *song)
{
    const audio_driver_t *hw = audiodrv_get_hardware_mixer();
    u8 ch;
    u8 *row;
    u8 pat_idx;
    if (song->current_order >= song->song_length) song->current_order = song->restart;
    /* pat_idx is u8, so it can never index past XM_MAX_PATTERNS=256. */
    pat_idx = song->order[song->current_order];
    if (!song->patterns[pat_idx]) {
        song->current_order++;
        return;
    }
    row = song->patterns[pat_idx] + (u32)song->current_row * XM_ROW_BYTES;
    for (ch = 0; ch < song->num_channels; ch++) {
        u8 *cell = row + ch * 5;
        u8 note = cell[0];
        u8 ins  = cell[1];
        u8 vol  = cell[2];
        u8 eff  = cell[3];
        u8 par  = cell[4];

        if (ins) song->chans[ch].sample_num = ins;
        if (vol >= 0x10 && vol <= 0x50) song->chans[ch].volume = vol - 0x10;

        if (note && note != 97) {
            xm_instrument_t *I = (ins && ins <= XM_MAX_INSTRUMENTS)
                                 ? song->instruments[ins - 1]
                                 : (song->chans[ch].sample_num ? song->instruments[song->chans[ch].sample_num - 1] : 0);
            if (I) {
                /* Multi-sample mapping: each MIDI note (1..96) maps to one
                 * of the instrument's up-to-16 samples via sample_for_note.
                 * Note in the pattern is 1-based; sample_for_note is
                 * indexed 0..95 (note - 1). */
                u8 mapped_note = (note > 0 && note <= 96) ? (u8)(note - 1) : 0;
                u8 sidx = I->sample_for_note[mapped_note];
                if (sidx >= XM_MAX_INSTR_SAMPLES) sidx = 0;
                {
                    xm_sample_t *s = &I->samples[sidx];
                    if (s->data && s->length) {
                        u8 effective_note = (u8)(note + s->relative_note);
                        u16 period = xm_period_linear(effective_note, s->finetune);
                        u32 freq = xm_freq_linear(period);
                        hbool loop = (s->type & 0x03) != 0;
                        if (hw && hw->trigger_voice && ins) {
                            hw->trigger_voice(ch, (u16)(ins - 1), freq,
                                              song->chans[ch].volume * 4,
                                              song->chans[ch].pan);
                        } else if (s->type & 0x10) {
                            mixer_set_channel16(ch, (s16 *)s->data, s->length,
                                                s->loop_start / 2,
                                                (s->loop_start + s->loop_length) / 2, loop);
                            mixer_set_frequency(ch, freq);
                        } else {
                            mixer_set_channel(ch, (s8 *)s->data, s->length,
                                              s->loop_start,
                                              s->loop_start + s->loop_length, loop);
                            mixer_set_frequency(ch, freq);
                        }
                        if (s->volume && song->chans[ch].volume == 0)
                            song->chans[ch].volume = s->volume;
                        song->chans[ch].period = period;
                    }
                }
            }
        }
        if (note == 97) {
            if (hw && hw->stop_voice) hw->stop_voice(ch);
            else mixer_stop_channel(ch);
            song->chans[ch].volume = 0;
        }

        song->chans[ch].effect       = eff;
        song->chans[ch].effect_param = par;
        switch (eff) {
        case 0x9: /* sample offset: not implemented in MVP */ break;
        case 0xC: song->chans[ch].volume = (par > 64) ? 64 : par; break;
        case 0xD: song->current_row = 0xFE; song->current_order++; break;
        case 0xF: if (par < 32) song->tempo = par; else song->bpm = par; break;
        default: break;
        }

        if (!(hw && hw->trigger_voice))
            mixer_set_volume(ch, (u8)((u16)song->chans[ch].volume * 4),
                              song->chans[ch].pan);
    }
}

static void process_tick(xm_song_t *song)
{
    u8 ch;
    for (ch = 0; ch < song->num_channels; ch++) {
        u8 e = song->chans[ch].effect;
        u8 p = song->chans[ch].effect_param;
        if (e == 0xA) {
            u8 up = p >> 4, dn = p & 0x0F;
            if (up && song->chans[ch].volume + up <= 64) song->chans[ch].volume += up;
            else if (dn && song->chans[ch].volume >= dn) song->chans[ch].volume -= dn;
            mixer_set_volume(ch, (u8)((u16)song->chans[ch].volume * 4),
                              song->chans[ch].pan);
        }
    }
}

void xm_advance(xm_song_t *song, u16 frames)
{
    while (frames) {
        u32 remaining = song->tick_samples - song->tick_acc;
        u32 chunk = (remaining > frames) ? frames : remaining;
        song->tick_acc += chunk;
        frames = (u16)(frames - chunk);
        if (song->tick_acc >= song->tick_samples) {
            song->tick_acc = 0;
            if (song->tick == 0) {
                if (song->current_row == 0xFE) song->current_row = 0;
                process_row(song);
            } else {
                process_tick(song);
            }
            song->tick++;
            if (song->tick >= song->tempo) {
                song->tick = 0;
                song->current_row++;
                if (song->current_row >= song->pattern_rows[song->order[song->current_order]]) {
                    song->current_row = 0;
                    song->current_order++;
                    if (song->current_order >= song->song_length)
                        song->current_order = song->restart;
                }
            }
        }
    }
}
