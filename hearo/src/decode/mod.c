/*
 * decode/mod.c - ProTracker MOD loader and playback engine.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Implements the ProTracker effect set well enough for typical 4-channel
 * Amiga modules and the .MOD packs that shipped with PC trackers.  Effects
 * 0,1,2,3,4,5,6,8,9,A,B,C,D,F are handled; E sub-effects are partial; 7 is
 * tremolo (also partial). Period table is the standard PT 36-period one
 * across three octaves with finetune offsets folded into the increment.
 */
#pragma off (check_stack)  /* see Makefile CF16_ISR -- belt-and-braces */
#include "mod.h"
#include "../audio/mixer.h"
#include "../audio/audiodrv.h"
#include "../platform/io.h"
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* PT period table for finetune 0, 36 entries (3 octaves * 12 notes). */
static const u16 pt_periods[36] = {
    856,808,762,720,678,640,604,570,538,508,480,453,
    428,404,381,360,339,320,302,285,269,254,240,226,
    214,202,190,180,170,160,151,143,135,127,120,113
};

/* PAL Amiga clock used to convert period -> Hz. */
#define AMIGA_CLOCK 7093789UL

static u32 period_to_freq(u16 period)
{
    if (!period) return 0;
    return AMIGA_CLOCK / ((u32)period * 2UL);
}

static u16 read_be16(const u8 *p) { return ((u16)p[0]<<8)|p[1]; }

static u8 sig_to_channels(const u8 *sig)
{
    if (memcmp(sig, "M.K.", 4) == 0 || memcmp(sig, "M!K!", 4) == 0 ||
        memcmp(sig, "FLT4", 4) == 0 || memcmp(sig, "4CHN", 4) == 0)
        return 4;
    if (memcmp(sig, "6CHN", 4) == 0) return 6;
    if (memcmp(sig, "8CHN", 4) == 0 || memcmp(sig, "FLT8", 4) == 0 ||
        memcmp(sig, "OCTA", 4) == 0)
        return 8;
    return 0;
}

hbool mod_load(const char *filename, mod_song_t *song)
{
    FILE *f;
    u8 sig[4];
    u8 sample_hdr[30];
    u8 byte;
    u8 i, j;
    u8 max_pat = 0;
    u32 pattern_bytes;

    if (!filename || !song) return HFALSE;
    memset(song, 0, sizeof(*song));
    f = fopen(filename, "rb");
    if (!f) return HFALSE;

    if (fread(song->title, 1, 20, f) != 20) goto fail;
    song->title[20] = 0;

    /* 31 sample headers (length, finetune, volume, loop start, loop length). */
    for (i = 0; i < MOD_MAX_SAMPLES; i++) {
        if (fread(sample_hdr, 1, 30, f) != 30) goto fail;
        memcpy(song->samples[i].name, sample_hdr, 22);
        song->samples[i].name[22] = 0;
        song->samples[i].length_words      = read_be16(sample_hdr + 22);
        song->samples[i].finetune          = (s8)((sample_hdr[24] & 0x0F) > 7
                                                  ? (sample_hdr[24] & 0x0F) - 16
                                                  :  sample_hdr[24] & 0x0F);
        song->samples[i].volume            = sample_hdr[25];
        if (song->samples[i].volume > 64) song->samples[i].volume = 64;
        song->samples[i].loop_start_words  = read_be16(sample_hdr + 26);
        song->samples[i].loop_length_words = read_be16(sample_hdr + 28);
    }

    if (fread(&byte, 1, 1, f) != 1) goto fail;  song->song_length = byte;
    if (fread(&byte, 1, 1, f) != 1) goto fail;  song->restart     = byte;
    if (fread(song->order, 1, 128, f) != 128) goto fail;
    if (fread(sig, 1, 4, f) != 4) goto fail;
    song->num_channels = sig_to_channels(sig);
    if (!song->num_channels) goto fail;

    for (i = 0; i < 128; i++) if (song->order[i] > max_pat) max_pat = song->order[i];
    song->num_patterns = max_pat + 1;
    pattern_bytes = (u32)song->num_patterns * 64UL * song->num_channels * 4UL;
    song->patterns = (u8 *)_fmalloc(pattern_bytes);
    if (!song->patterns) goto fail;
    if (!io_read_chunked(f, song->patterns, pattern_bytes)) goto fail;

    song->num_samples = MOD_MAX_SAMPLES;
    for (i = 0; i < MOD_MAX_SAMPLES; i++) {
        u32 bytes = (u32)song->samples[i].length_words * 2UL;
        if (!bytes) continue;
        song->samples[i].data = (s8 *)_fmalloc(bytes);
        if (!song->samples[i].data) goto fail;
        if (!io_read_chunked(f, song->samples[i].data, bytes)) goto fail;
    }

    /* Defaults and pan layout (LRRL across 4 channels, simple fan otherwise). */
    song->speed = 6;
    song->tempo = 125;
    for (j = 0; j < song->num_channels; j++) {
        song->chans[j].pan = (j & 1) ? 192 : 64;
    }

    fclose(f);
    return HTRUE;
fail:
    fclose(f);
    mod_free(song);
    return HFALSE;
}

void mod_free(mod_song_t *song)
{
    u8 i;
    if (!song) return;
    if (song->patterns) _ffree(song->patterns);
    for (i = 0; i < MOD_MAX_SAMPLES; i++)
        if (song->samples[i].data) _ffree(song->samples[i].data);
    memset(song, 0, sizeof(*song));
}

static void recompute_tick_samples(mod_song_t *song, u32 mixer_rate)
{
    /* PT timing: 2.5 * mixer_rate / tempo samples per tick. */
    song->tick_samples = (mixer_rate * 5UL) / (2UL * song->tempo);
}

void mod_play_init(mod_song_t *song, u32 mixer_rate)
{
    const audio_driver_t *hw = audiodrv_get_hardware_mixer();
    song->current_order = 0;
    song->current_row   = 0;
    song->tick          = 0;
    song->tick_acc      = 0;
    recompute_tick_samples(song, mixer_rate);
    mixer_stop_all();

    /* If a hardware-mixed card (GUS) is present, upload every non-empty
     * sample to its DRAM in MOD-sample-index order. process_row will then
     * trigger voices in hardware instead of feeding the software mixer. */
    if (hw && hw->upload_sample) {
        u8 i;
        for (i = 0; i < MOD_MAX_SAMPLES; i++) {
            mod_sample_t *s = &song->samples[i];
            if (!s->data || !s->length_words) continue;
            hw->upload_sample((u16)i, s->data, (u32)s->length_words * 2UL, 8);
        }
    }
}

/* Process a row: read pattern data, apply note-on/effect, push state to
 * mixer (or GUS hardware voices when present). */
static void process_row(mod_song_t *song)
{
    const audio_driver_t *hw = audiodrv_get_hardware_mixer();
    u8 ch;
    u8 *pat;
    u8 pat_idx = song->order[song->current_order];
    /* Out-of-range order entry would index past the patterns allocation;
     * advance the order and bail. */
    if (pat_idx >= song->num_patterns) {
        song->current_order++;
        return;
    }
    pat = song->patterns + ((u32)pat_idx * 64UL * song->num_channels * 4UL)
                         + ((u32)song->current_row * song->num_channels * 4UL);
    for (ch = 0; ch < song->num_channels; ch++) {
        u8 *n = pat + ch * 4;
        u8 sample_num = (n[0] & 0xF0) | (n[2] >> 4);
        u16 period    = (((u16)(n[0] & 0x0F)) << 8) | n[1];
        u8 effect     = n[2] & 0x0F;
        u8 param      = n[3];

        song->chans[ch].effect       = effect;
        song->chans[ch].effect_param = param;
        song->chans[ch].arp_off1 = (param >> 4);
        song->chans[ch].arp_off2 = (param & 0x0F);

        if (sample_num) {
            song->chans[ch].sample_num = sample_num;
            song->chans[ch].volume     = song->samples[sample_num - 1].volume;
        }

        if (period) {
            if (effect == 3 || effect == 5) {
                song->chans[ch].target_period = period;       /* tone porta */
            } else {
                song->chans[ch].period = period;
                if (sample_num) {
                    mod_sample_t *s = &song->samples[song->chans[ch].sample_num - 1];
                    if (s->data && s->length_words) {
                        u32 loop_start = (u32)s->loop_start_words * 2UL;
                        u32 loop_end   = loop_start + (u32)s->loop_length_words * 2UL;
                        hbool loop = (s->loop_length_words > 1);
                        u32 freq = period_to_freq(period);
                        if (hw && hw->trigger_voice) {
                            hw->trigger_voice(ch, (u16)(sample_num - 1),
                                              freq, song->chans[ch].volume * 4,
                                              song->chans[ch].pan);
                        } else {
                            mixer_set_channel(ch, s->data,
                                              (u32)s->length_words * 2UL,
                                              loop_start, loop_end, loop);
                            mixer_set_frequency(ch, freq);
                        }
                    }
                }
            }
        }

        if (effect == 0x0C) { song->chans[ch].volume = (param > 64) ? 64 : param; }
        if (effect == 0x0F && param) {            /* F00 means "no change" */
            if (param < 32) song->speed = param;
            else            song->tempo = param;
        }
        if (effect == 0x0B) {
            song->current_order = param;
            song->current_row   = 0xFF;        /* break cleanly at row advance */
        }
        if (effect == 0x0D) {
            song->current_order++;
            song->current_row   = 0xFF;
        }
        if (effect == 0x09 && param && song->chans[ch].sample_num) {
            /* sample_num is 1-based; guard against the case where the cell has
             * no sample assigned (sample_num == 0 underflowing to 255). */
            mixer_set_channel(ch, song->samples[song->chans[ch].sample_num - 1].data,
                              (u32)song->samples[song->chans[ch].sample_num - 1].length_words * 2UL,
                              0, 0, HFALSE);
            /* TODO: programmatic position seek to (param * 256) */
        }
        if (effect == 0x08) song->chans[ch].pan = param;

        if (!(hw && hw->trigger_voice))
            mixer_set_volume(ch, (u8)((u16)song->chans[ch].volume * 4), song->chans[ch].pan);
    }
}

static void process_tick(mod_song_t *song)
{
    u8 ch;
    for (ch = 0; ch < song->num_channels; ch++) {
        u8 e = song->chans[ch].effect;
        u8 p = song->chans[ch].effect_param;
        switch (e) {
        case 0x00:                                       /* arpeggio */
            if (p && song->tick) {
                u16 base = song->chans[ch].period;
                u16 use  = base;
                u8 off = (song->tick % 3 == 1) ? song->chans[ch].arp_off1
                       : (song->tick % 3 == 2) ? song->chans[ch].arp_off2 : 0;
                if (off) {
                    /* Period -> ~half-step approximation using table lookup. */
                    u8 i;
                    for (i = 0; i < 36 - off; i++) if (pt_periods[i] == base) { use = pt_periods[i + off]; break; }
                }
                mixer_set_frequency(ch, period_to_freq(use));
            }
            break;
        case 0x01:                                       /* portamento up */
            if (song->tick) {
                if (song->chans[ch].period > p) song->chans[ch].period -= p;
                mixer_set_frequency(ch, period_to_freq(song->chans[ch].period));
            }
            break;
        case 0x02:                                       /* portamento down */
            if (song->tick) {
                /* Clamp to PT's lowest period to avoid u16 wrap and
                 * inaudible / aliased pitch. */
                u16 max_period = 856 + 64;
                if ((u32)song->chans[ch].period + p > max_period)
                    song->chans[ch].period = max_period;
                else
                    song->chans[ch].period += p;
                mixer_set_frequency(ch, period_to_freq(song->chans[ch].period));
            }
            break;
        case 0x03:                                       /* tone portamento */
            if (song->tick && song->chans[ch].target_period) {
                u8 sp = p ? p : song->chans[ch].port_speed;
                song->chans[ch].port_speed = sp;
                if (song->chans[ch].period < song->chans[ch].target_period) {
                    song->chans[ch].period += sp;
                    if (song->chans[ch].period > song->chans[ch].target_period)
                        song->chans[ch].period = song->chans[ch].target_period;
                } else if (song->chans[ch].period > song->chans[ch].target_period) {
                    if (song->chans[ch].period > sp) song->chans[ch].period -= sp;
                    if (song->chans[ch].period < song->chans[ch].target_period)
                        song->chans[ch].period = song->chans[ch].target_period;
                }
                mixer_set_frequency(ch, period_to_freq(song->chans[ch].period));
            }
            break;
        case 0x0A:                                       /* volume slide */
            if (song->tick) {
                u8 up = p >> 4, dn = p & 0x0F;
                if (up && song->chans[ch].volume + up <= 64) song->chans[ch].volume += up;
                else if (dn) song->chans[ch].volume = (song->chans[ch].volume > dn) ? song->chans[ch].volume - dn : 0;
                mixer_set_volume(ch, (u8)((u16)song->chans[ch].volume * 4), song->chans[ch].pan);
            }
            break;
        default: break;
        }
    }
}

void mod_advance(mod_song_t *song, u16 frames)
{
    while (frames) {
        u32 remaining = song->tick_samples - song->tick_acc;
        u32 chunk = (remaining > frames) ? frames : remaining;
        song->tick_acc += chunk;
        frames = (u16)(frames - chunk);
        if (song->tick_acc >= song->tick_samples) {
            song->tick_acc = 0;
            if (song->tick == 0) {
                if (song->current_row == 0xFF) song->current_row = 0;
                process_row(song);
            } else {
                process_tick(song);
            }
            song->tick++;
            if (song->tick >= song->speed) {
                song->tick = 0;
                song->current_row++;
                if (song->current_row >= 64) {
                    song->current_row = 0;
                    song->current_order++;
                    if (song->current_order >= song->song_length)
                        song->current_order = song->restart;
                }
            }
        }
    }
}

u8 mod_get_position(const mod_song_t *song) { return song->current_order; }
