/*
 * decode/midi.c - Standard MIDI File parser and event sequencer.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Loads format 0 / 1 SMF, walks per-track event streams, dispatches each
 * event to either the OPL2/OPL3 FM voice allocator (midifm.c) or the
 * MPU-401 UART driver based on the active audio driver.  We compute the
 * scheduling clock as samples-per-tick in Q16.16, advanced inside the audio
 * callback so timing follows the mixer rather than any wall clock.
 */
#pragma off (check_stack)  /* see Makefile CF16_ISR -- belt-and-braces */
#include "midi.h"
#include "midifm.h"
#include "../audio/audiodrv.h"
#include "../audio/mpu401.h"
#include "../platform/io.h"
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u32 be32(const u8 *p) { return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3]; }
static u16 be16(const u8 *p) { return ((u16)p[0]<<8)|p[1]; }

static u32 read_vlq(const u8 *data, u32 *pos)
{
    u32 value = 0;
    u8  byte;
    do {
        byte = data[(*pos)++];
        value = (value << 7) | (byte & 0x7F);
    } while (byte & 0x80);
    return value;
}

static void track_advance_to_next(midi_song_t *song, u8 t)
{
    u32 delta;
    if (song->tracks[t].pos >= song->tracks[t].length) {
        song->tracks[t].ended = 1;
        return;
    }
    delta = read_vlq(song->tracks[t].data, &song->tracks[t].pos);
    song->tracks[t].next_tick = song->current_tick + delta;
}

hbool midi_load(const char *filename, midi_song_t *song)
{
    FILE *f;
    long file_len;
    u8 *p;
    u32 off;
    u16 t;

    if (!filename || !song) return HFALSE;
    memset(song, 0, sizeof(*song));
    f = fopen(filename, "rb");
    if (!f) return HFALSE;
    fseek(f, 0, SEEK_END); file_len = ftell(f); fseek(f, 0, SEEK_SET);
    if (file_len < 14) { fclose(f); return HFALSE; }
    song->file_size = (u32)file_len;
    song->file_data = (u8 *)_fmalloc(song->file_size);
    if (!song->file_data) { fclose(f); return HFALSE; }
    if (!io_read_chunked(f, song->file_data, song->file_size)) {
        fclose(f); midi_free(song); return HFALSE;
    }
    fclose(f);

    p = song->file_data;
    if (memcmp(p, "MThd", 4) != 0 || be32(p + 4) != 6) { midi_free(song); return HFALSE; }
    song->format        = (u8)be16(p + 8);
    song->num_tracks    = be16(p + 10);
    song->ticks_per_qn  = be16(p + 12);
    song->tempo_us_per_qn = 500000UL;       /* default 120 BPM */
    if (song->num_tracks > MIDI_MAX_TRACKS) song->num_tracks = MIDI_MAX_TRACKS;

    /* Walk MTrk chunks */
    off = 14;
    for (t = 0; t < song->num_tracks; t++) {
        u32 chunk_len;
        if (off + 8 > song->file_size) { midi_free(song); return HFALSE; }
        if (memcmp(song->file_data + off, "MTrk", 4) != 0) {
            /* skip unknown chunks */
            chunk_len = be32(song->file_data + off + 4);
            off += 8 + chunk_len;
            t--; continue;
        }
        chunk_len = be32(song->file_data + off + 4);
        if (off + 8 + chunk_len > song->file_size) { midi_free(song); return HFALSE; }
        song->tracks[t].data   = song->file_data + off + 8;
        song->tracks[t].length = chunk_len;
        song->tracks[t].pos    = 0;
        song->tracks[t].running_status = 0;
        song->tracks[t].ended  = 0;
        off += 8 + chunk_len;
    }

    song->loaded = 1;
    return HTRUE;
}

void midi_free(midi_song_t *song)
{
    if (!song) return;
    if (song->file_data) _ffree(song->file_data);
    memset(song, 0, sizeof(*song));
}

static void recompute_clock(midi_song_t *song, u32 mixer_rate)
{
    /* samples_per_tick_q16 = mixer_rate * 65536 * tempo_us_per_qn /
     *                       (1000000 * ticks_per_qn).
     *
     * Restated to avoid u32 overflow: let inv_ratio be ticks_per_second
     * scaled to fit in u32 (always > 1 for sensible MIDI).
     *   inv_ratio = (1000000 * ticks_per_qn) / tempo_us_per_qn
     *   q16 = (mixer_rate << 16) / inv_ratio
     *
     * Worked example: mixer_rate=22050, tpqn=96, tempo=500000 (120 BPM).
     *   inv_ratio = (1000000 * 96) / 500000 = 192
     *   q16 = (22050 << 16) / 192 = 7524693
     *   samples_per_tick = 7524693 / 65536 = 114.83 (114.84 expected) */
    u32 inv_ratio;
    if (!song->ticks_per_qn || !song->tempo_us_per_qn) {
        song->samples_per_tick_q16 = 0;
        return;
    }
    inv_ratio = ((u32)song->ticks_per_qn * 1000000UL) / song->tempo_us_per_qn;
    if (!inv_ratio) { song->samples_per_tick_q16 = 0; return; }
    song->samples_per_tick_q16 = (mixer_rate << 16) / inv_ratio;
}

void midi_play_init(midi_song_t *song, u32 mixer_rate)
{
    u8 t;
    song->current_tick   = 0;
    song->sample_acc_q16 = 0;
    song->tempo_us_per_qn = 500000UL;
    song->cached_mixer_rate = mixer_rate;
    recompute_clock(song, mixer_rate);
    for (t = 0; t < song->num_tracks; t++) {
        song->tracks[t].pos     = 0;
        song->tracks[t].ended   = 0;
        song->tracks[t].running_status = 0;
        track_advance_to_next(song, t);
    }
    midifm_silence();
}

/* Dispatch a complete MIDI event. MVP: always to OPL FM (driven in parallel
 * with whatever PCM driver is producing the timing source).  When MPU-401
 * is the active driver, also tee the message to it so external synths get
 * the same stream. */
static void dispatch_event(u8 status, u8 d1, u8 d2)
{
    const audio_driver_t *drv = audiodrv_active();
    u8 ch   = status & 0x0F;
    u8 type = status & 0xF0;
    switch (type) {
    case 0x90: if (d2) midifm_note_on(ch, 0, d1, d2); else midifm_note_off(ch, d1); break;
    case 0x80: midifm_note_off(ch, d1); break;
    case 0xB0: midifm_control_change(ch, d1, d2); break;
    case 0xC0: midifm_program_change(ch, d1); break;
    default:   break;
    }
    /* Identify the MPU driver by exact name to avoid collisions with future
     * drivers that happen to start with 'm'. */
    if (drv && drv->name && strcmp(drv->name, "mpu401") == 0) {
        mpu_send_msg(status, d1, d2);
    }
}

static void process_track_events_at_tick(midi_song_t *song, u8 t)
{
    u8 *data = song->tracks[t].data;
    u32 length = song->tracks[t].length;

    while (!song->tracks[t].ended &&
           song->tracks[t].next_tick == song->current_tick)
    {
        u8 status, d1 = 0, d2 = 0;
        if (song->tracks[t].pos >= length) {
            song->tracks[t].ended = 1;
            return;
        }
        status = data[song->tracks[t].pos];
        if (status & 0x80) {
            song->tracks[t].pos++;
            song->tracks[t].running_status = status;
        } else {
            status = song->tracks[t].running_status;
        }

        if (status == 0xFF) {
            /* meta event */
            u8 meta_type;
            u32 meta_len;
            if (song->tracks[t].pos >= length) { song->tracks[t].ended = 1; return; }
            meta_type = data[song->tracks[t].pos++];
            meta_len = read_vlq(data, &song->tracks[t].pos);
            if (song->tracks[t].pos + meta_len > length) { song->tracks[t].ended = 1; return; }
            if (meta_type == 0x2F) { song->tracks[t].ended = 1; return; }
            if (meta_type == 0x51 && meta_len == 3) {
                song->tempo_us_per_qn = ((u32)data[song->tracks[t].pos] << 16)
                                      | ((u32)data[song->tracks[t].pos + 1] << 8)
                                      |  (u32)data[song->tracks[t].pos + 2];
                /* Tempo change must recompute samples-per-tick; the mixer
                 * rate captured at play_init drives the clock. */
                recompute_clock(song, song->cached_mixer_rate);
            }
            song->tracks[t].pos += meta_len;
        } else if (status == 0xF0 || status == 0xF7) {
            u32 sysex_len = read_vlq(data, &song->tracks[t].pos);
            song->tracks[t].pos += sysex_len;
        } else {
            u8 hi = status & 0xF0;
            d1 = data[song->tracks[t].pos++];
            if (hi != 0xC0 && hi != 0xD0) {
                d2 = data[song->tracks[t].pos++];
            }
            dispatch_event(status, d1, d2);
        }
        track_advance_to_next(song, t);
    }
}

void midi_advance(midi_song_t *song, u16 frames)
{
    u8 t;
    u32 acc = song->sample_acc_q16;
    u32 step = song->samples_per_tick_q16;
    if (!step) return;
    while (frames) {
        u16 chunk = frames > 64 ? 64 : frames;
        acc += (u32)chunk << 16;
        while (acc >= step) {
            acc -= step;
            song->current_tick++;
            for (t = 0; t < song->num_tracks; t++) {
                if (!song->tracks[t].ended)
                    process_track_events_at_tick(song, t);
            }
        }
        frames = (u16)(frames - chunk);
    }
    song->sample_acc_q16 = acc;
}
