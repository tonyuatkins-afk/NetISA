/*
 * decode/vgm.c - VGM chip-music player.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Inner loop pattern adopted from vgmslap (BSD): consume commands, accrue
 * waits, then return when the wait exceeds the buffer of mixer samples we
 * were asked to fill.  Register writes are issued immediately to OPL or
 * SN76489 ports; rate conversion only matters for waits.
 */
#include "vgm.h"
#include "../audio/adlib.h"
#include "../platform/io.h"
#include <conio.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SN76489_PORT 0xC0       /* Tandy 1000 PSG; standalone PSG often 0x7C */

static u32 le32(const u8 *p) { return (u32)p[0] | ((u32)p[1]<<8) | ((u32)p[2]<<16) | ((u32)p[3]<<24); }
static u16 le16(const u8 *p) { return (u16)p[0] | ((u16)p[1]<<8); }

hbool vgm_load(const char *filename, vgm_song_t *song)
{
    FILE *f;
    long len;
    u32 abs_data_offset;
    u32 abs_loop_offset;
    u32 version;

    if (!filename || !song) return HFALSE;
    memset(song, 0, sizeof(*song));
    f = fopen(filename, "rb");
    if (!f) return HFALSE;
    fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
    if (len < 64) { fclose(f); return HFALSE; }
    song->file_size = (u32)len;
    song->file_data = (u8 *)_fmalloc(song->file_size);
    if (!song->file_data) { fclose(f); return HFALSE; }
    if (!io_read_chunked(f, song->file_data, song->file_size)) {
        fclose(f); vgm_free(song); return HFALSE;
    }
    fclose(f);

    if (memcmp(song->file_data, "Vgm ", 4) != 0) { vgm_free(song); return HFALSE; }
    version = le32(song->file_data + 0x08);

    /* Header field offsets per VGM 1.50+. Older files have data starting
     * at 0x40 implicitly. */
    song->total_samples  = le32(song->file_data + 0x18);
    abs_loop_offset      = le32(song->file_data + 0x1C);
    if (abs_loop_offset) abs_loop_offset += 0x1C;
    song->loop_samples   = le32(song->file_data + 0x20);
    song->sn76489_clock  = le32(song->file_data + 0x0C);

    if (version >= 0x150) {
        u32 rel = le32(song->file_data + 0x34);
        abs_data_offset = rel ? (rel + 0x34) : 0x40;
    } else {
        abs_data_offset = 0x40;
    }
    if (abs_data_offset >= song->file_size) { vgm_free(song); return HFALSE; }

    if (version >= 0x151 && song->file_size >= 0x60) {
        song->ym3812_clock = le32(song->file_data + 0x50);
        song->ymf262_clock = le32(song->file_data + 0x5C);
    }

    song->vgm_data_start = abs_data_offset;
    song->loop_offset    = abs_loop_offset;
    song->loaded = 1;
    return HTRUE;
}

void vgm_free(vgm_song_t *song)
{
    if (!song) return;
    if (song->file_data) _ffree(song->file_data);
    memset(song, 0, sizeof(*song));
}

void vgm_play_init(vgm_song_t *song, u32 mixer_rate)
{
    /* q16_step = (mixer_rate / 44100) << 16. mixer_rate <= 44100 so the
     * intermediate (mixer_rate << 16) fits in u32. */
    song->q16_step = (mixer_rate << 16) / 44100UL;
    if (!song->q16_step) song->q16_step = 1;
    song->q16_acc = 0;
    song->cursor = song->vgm_data_start;
    song->pending_wait_samples = 0;
    song->ended = 0;
    song->loops_done = 0;
}

static void sn76489_write(u8 val)
{
    /* Single byte write; latch handled by the chip. */
    outp(SN76489_PORT, val);
}

/* Consume commands until either we've buffered enough wait samples to
 * cover `vgm_samples_needed`, or the song ends. */
static void consume_commands(vgm_song_t *song, u32 vgm_samples_needed)
{
    while (song->pending_wait_samples < vgm_samples_needed && !song->ended) {
        u8 cmd;
        if (song->cursor >= song->file_size) { song->ended = 1; break; }
        cmd = song->file_data[song->cursor++];
        switch (cmd) {
        case 0x50: {       /* SN76489 / Tandy PSG */
            if (song->cursor >= song->file_size) { song->ended = 1; break; }
            sn76489_write(song->file_data[song->cursor++]);
            break;
        }
        case 0x5A:         /* YM3812 (OPL2), one bank */
        case 0x5E: {       /* YMF262 (OPL3) port 0 */
            if (song->cursor + 2 > song->file_size) { song->ended = 1; break; }
            adlib_write(song->file_data[song->cursor],
                        song->file_data[song->cursor + 1]);
            song->cursor += 2;
            break;
        }
        case 0x5F: {       /* YMF262 (OPL3) port 1 */
            if (song->cursor + 2 > song->file_size) { song->ended = 1; break; }
            adlib_write_b(song->file_data[song->cursor],
                          song->file_data[song->cursor + 1]);
            song->cursor += 2;
            break;
        }
        case 0x61: {       /* wait N samples */
            if (song->cursor + 2 > song->file_size) { song->ended = 1; break; }
            song->pending_wait_samples += le16(song->file_data + song->cursor);
            song->cursor += 2;
            break;
        }
        case 0x62: song->pending_wait_samples += 735;  break; /* NTSC frame */
        case 0x63: song->pending_wait_samples += 882;  break; /* PAL frame */
        case 0x66: {       /* end of song */
            if (song->loop_offset && (song->loops_max == 0 || song->loops_done < song->loops_max)) {
                song->cursor = song->loop_offset;
                song->loops_done++;
            } else {
                song->ended = 1;
            }
            break;
        }
        case 0x67: {       /* data block: 0x66 type len32 data... */
            if (song->cursor + 6 > song->file_size) { song->ended = 1; break; }
            song->cursor++;                 /* compatibility 0x66 */
            song->cursor++;                 /* type */
            {
                u32 blklen = le32(song->file_data + song->cursor);
                song->cursor += 4 + blklen;
            }
            break;
        }
        default:
            if (cmd >= 0x70 && cmd <= 0x7F) {
                song->pending_wait_samples += (cmd & 0x0F) + 1;
            } else if (cmd >= 0x80 && cmd <= 0x8F) {
                /* YM2612 port 0 write + small wait. Skip the value byte. */
                if (song->cursor < song->file_size) song->cursor++;
                song->pending_wait_samples += (cmd & 0x0F);
            } else {
                /* Other chip writes we don't yet support. Compute the byte
                 * count to skip per the VGM spec opcode table. */
                u8 skip = 0;
                if (cmd >= 0x30 && cmd <= 0x3F) skip = 1;
                else if ((cmd >= 0x40 && cmd <= 0x4E) ||
                         (cmd >= 0x51 && cmd <= 0x5D && cmd != 0x5A &&
                          cmd != 0x5E && cmd != 0x5F)) skip = 2;
                else if (cmd >= 0xA0 && cmd <= 0xBF) skip = 2;
                else if (cmd >= 0xC0 && cmd <= 0xDF) skip = 3;
                else if (cmd >= 0xE0 && cmd <= 0xE1) skip = 4;
                if (skip == 0) {
                    /* Unknown opcode: bail to avoid silent runaway. */
                    song->ended = 1;
                } else {
                    if (song->cursor + skip > song->file_size) song->ended = 1;
                    else song->cursor += skip;
                }
            }
            break;
        }
    }
}

void vgm_advance(vgm_song_t *song, u16 frames)
{
    if (!song->loaded || song->ended) return;
    /* Convert mixer-rate frames into VGM samples to consume. */
    {
        u32 mixer_q16   = (u32)frames << 16;
        u32 vgm_samples = (mixer_q16 + song->q16_acc) / song->q16_step;
        song->q16_acc   = (mixer_q16 + song->q16_acc) % song->q16_step;
        consume_commands(song, vgm_samples);
        if (song->pending_wait_samples >= vgm_samples)
            song->pending_wait_samples -= vgm_samples;
        else
            song->pending_wait_samples = 0;
    }
}
