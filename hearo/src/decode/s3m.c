/*
 * decode/s3m.c - ScreamTracker 3 loader and player.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Reference: the canonical S3M layout document (FireLight, 1995). We
 * implement the sampled-instrument path: OPL instruments are recognized
 * during load but produce silence until the FM bridge lands.
 *
 * Pattern data is stored packed; we unpack on load so the per-tick player
 * can index rows directly. A row is up to 32 channel events; an event is 5
 * bytes (note, instrument, volume, effect, parameter) with bit flags
 * indicating which fields are present.
 */
#pragma off (check_stack)  /* see Makefile CF16_ISR -- belt-and-braces */
#include "s3m.h"
#include "../audio/mixer.h"
#include "../audio/audiodrv.h"
#include "../platform/io.h"
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROW_BYTES   (5 * S3M_MAX_CHANNELS)        /* unpacked row */
#define PAT_BYTES   (64 * ROW_BYTES)

static u16 le16(const u8 *p) { return (u16)p[0] | ((u16)p[1] << 8); }
static u32 le32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

/* Wrapper kept for local readability; real work in io_read_chunked which
 * normalizes segment:offset between chunks (Watcom large-model far ptr
 * arithmetic does not). */
static hbool read_chunk(FILE *f, void *dst, u32 n) { return io_read_chunked(f, dst, n); }

static hbool load_sample_at(FILE *f, u32 paragraph, u8 ffi, s3m_sample_t *s)
{
    u8 hdr[80];
    u32 file_offset = (u32)paragraph * 16UL;
    if (fseek(f, (long)file_offset, SEEK_SET) != 0) return HFALSE;
    if (fread(hdr, 1, 80, f) != 80) return HFALSE;
    s->type   = hdr[0];
    memcpy(s->filename, hdr + 1, 12); s->filename[12] = 0;
    s->length     = le32(hdr + 16);
    s->loop_start = le32(hdr + 20);
    s->loop_end   = le32(hdr + 24);
    s->volume     = hdr[28];
    s->pack       = hdr[30];
    s->flags      = hdr[31];
    s->c2spd      = le32(hdr + 32);
    memcpy(s->name, hdr + 48, 28); s->name[27] = 0;
    if (s->type != 1 || !s->length) return HTRUE;     /* OPL or empty */
    {
        u32 sample_para = ((u32)hdr[13] << 16) | ((u32)hdr[14]) | ((u32)hdr[15] << 8);
        u32 sample_off  = sample_para * 16UL;
        u32 byte_len    = s->length * ((s->flags & 0x04) ? 2 : 1);
        if (fseek(f, (long)sample_off, SEEK_SET) != 0) return HFALSE;
        s->data = _fmalloc(byte_len);
        if (!s->data) return HFALSE;
        if (!read_chunk(f, s->data, byte_len)) return HFALSE;
        /* ffi=1 means unsigned; convert to signed in place. */
        if (ffi == 1) {
            u32 i;
            if (s->flags & 0x04) {
                u16 far *p = (u16 far *)s->data;
                for (i = 0; i < s->length; i++) p[i] ^= 0x8000;
            } else {
                u8 far *p = (u8 far *)s->data;
                for (i = 0; i < byte_len; i++) p[i] ^= 0x80;
            }
        }
    }
    return HTRUE;
}

static hbool load_pattern_at(FILE *f, u32 paragraph, u8 *unpacked, u16 *out_used)
{
    u32 file_offset = (u32)paragraph * 16UL;
    u8 length_hdr[2];
    u16 packed_size;
    u8 *packed;
    u16 row = 0;
    u16 src = 0;
    hbool ok = HTRUE;

    memset(unpacked, 0, PAT_BYTES);
    if (fseek(f, (long)file_offset, SEEK_SET) != 0) return HFALSE;
    if (fread(length_hdr, 1, 2, f) != 2) return HFALSE;
    packed_size = le16(length_hdr);
    if (packed_size < 2) return HFALSE;
    packed_size -= 2;
    /* Use the far heap: packed pattern blocks can approach 64K and the near
     * heap inside DGROUP is much smaller after detection tables load. */
    packed = (u8 *)_fmalloc(packed_size);
    if (!packed) return HFALSE;
    if (!read_chunk(f, packed, packed_size)) { _ffree(packed); return HFALSE; }

    while (row < 64 && src < packed_size) {
        u8 what = packed[src++];
        u8 channel;
        u8 *cell;
        if (what == 0) { row++; continue; }
        channel = what & 0x1F;
        cell = unpacked + row * ROW_BYTES + channel * 5;
        if (what & 0x20) {                         /* note + instrument */
            if (src + 2 > packed_size) { ok = HFALSE; break; }
            cell[0] = packed[src++];
            cell[1] = packed[src++];
        }
        if (what & 0x40) {                         /* volume */
            if (src + 1 > packed_size) { ok = HFALSE; break; }
            cell[2] = packed[src++];
        }
        if (what & 0x80) {                         /* effect + param */
            if (src + 2 > packed_size) { ok = HFALSE; break; }
            cell[3] = packed[src++];
            cell[4] = packed[src++];
        }
    }
    _ffree(packed);
    *out_used = (u16)src;
    return ok;
}

hbool s3m_load(const char *filename, s3m_song_t *song)
{
    FILE *f;
    u8 hdr[96];
    u8 i;

    if (!filename || !song) return HFALSE;
    memset(song, 0, sizeof(*song));
    f = fopen(filename, "rb");
    if (!f) return HFALSE;

    if (fread(hdr, 1, 96, f) != 96) goto fail;
    if (memcmp(hdr + 44, "SCRM", 4) != 0) goto fail;
    if (hdr[29] != 16) goto fail;                  /* type 16 = S3M */

    memcpy(song->title, hdr, 28); song->title[27] = 0;
    song->ord_count   = (u8)le16(hdr + 32);
    song->smp_count   = (u8)le16(hdr + 34);
    song->pat_count   = (u8)le16(hdr + 36);
    song->flags       = le16(hdr + 38);
    song->cwt_v       = le16(hdr + 40);
    song->ffi         = le16(hdr + 42);
    song->global_vol  = hdr[48];
    song->init_speed  = hdr[49];
    song->init_tempo  = hdr[50];
    song->master_vol  = hdr[51];
    song->default_pan = hdr[53];
    memcpy(song->chan_settings, hdr + 64, 32);

    /* ord_count is u8 so it cannot exceed S3M_MAX_ORDERS=256; only sample
     * and pattern counts need a real bound. */
    if (song->smp_count > S3M_MAX_SAMPLES) goto fail;
    if (song->pat_count > S3M_MAX_PATTERNS) goto fail;

    if (fread(song->order, 1, song->ord_count, f) != song->ord_count) goto fail;
    for (i = 0; i < song->smp_count; i++) {
        u8 word[2];
        if (fread(word, 1, 2, f) != 2) goto fail;
        song->para_inst[i] = le16(word);
    }
    for (i = 0; i < song->pat_count; i++) {
        u8 word[2];
        if (fread(word, 1, 2, f) != 2) goto fail;
        song->para_pat[i] = le16(word);
    }
    if (song->default_pan == 0xFC) {
        if (fread(song->chan_pan, 1, 32, f) != 32) goto fail;
    } else {
        for (i = 0; i < 32; i++) song->chan_pan[i] = (i & 1) ? 12 : 3;
    }

    /* Walk samples and patterns by paragraph offset. */
    for (i = 0; i < song->smp_count; i++) {
        if (song->para_inst[i] == 0) continue;
        if (!load_sample_at(f, song->para_inst[i], (u8)song->ffi, &song->samples[i])) goto fail;
    }
    for (i = 0; i < song->pat_count; i++) {
        if (song->para_pat[i] == 0) continue;
        song->patterns[i] = (u8 *)_fmalloc(PAT_BYTES);
        if (!song->patterns[i]) goto fail;
        if (!load_pattern_at(f, song->para_pat[i], song->patterns[i],
                             &song->pattern_bytes[i])) goto fail;
    }

    /* Count enabled sampled channels. */
    {
        u8 n = 0;
        for (i = 0; i < 32; i++) {
            u8 c = song->chan_settings[i];
            if ((c & 0x80) == 0 && (c & 0x7F) < 16) n++;
        }
        song->num_channels = n ? n : 4;
        if (song->num_channels > MIXER_MAX_CHANNELS)
            song->num_channels = MIXER_MAX_CHANNELS;
    }

    song->speed = song->init_speed ? song->init_speed : 6;
    song->tempo = song->init_tempo ? song->init_tempo : 125;

    fclose(f);
    return HTRUE;
fail:
    fclose(f);
    s3m_free(song);
    return HFALSE;
}

void s3m_free(s3m_song_t *song)
{
    u8 i;
    if (!song) return;
    for (i = 0; i < S3M_MAX_SAMPLES; i++)
        if (song->samples[i].data) _ffree(song->samples[i].data);
    for (i = 0; i < S3M_MAX_PATTERNS; i++)
        if (song->patterns[i]) _ffree(song->patterns[i]);
    memset(song, 0, sizeof(*song));
}

static void recompute_tick_samples(s3m_song_t *song, u32 mixer_rate)
{
    /* Same timing as MOD: 2.5 * rate / tempo. */
    song->tick_samples = (mixer_rate * 5UL) / (2UL * song->tempo);
}

void s3m_play_init(s3m_song_t *song, u32 mixer_rate)
{
    const audio_driver_t *hw = audiodrv_get_hardware_mixer();
    u8 i;
    song->current_order = 0;
    song->current_row   = 0;
    song->tick          = 0;
    song->tick_acc      = 0;
    recompute_tick_samples(song, mixer_rate);
    for (i = 0; i < song->num_channels; i++) {
        u8 raw = song->chan_pan[i] & 0x0F;        /* 0..15 */
        song->chans[i].pan = (u8)((raw * 255) / 15);
    }
    mixer_stop_all();

    /* Upload samples to GUS DRAM if present. S3M sample IDs are 1-based. */
    if (hw && hw->upload_sample) {
        for (i = 0; i < S3M_MAX_SAMPLES; i++) {
            s3m_sample_t *s = &song->samples[i];
            if (!s->data || !s->length) continue;
            hw->upload_sample((u16)i, s->data,
                              s->length * ((s->flags & 0x04) ? 2 : 1),
                              (s->flags & 0x04) ? 16 : 8);
        }
    }
}

/* Quarter-period sine table (0..63 -> 0..255). Used by vibrato (effect H)
 * to perturb playback frequency around the channel's base frequency.
 * 64-entry resolution matches ProTracker / S3M conventions. */
static const u8 sine_q[64] = {
      0,  12,  25,  37,  49,  62,  74,  86,
     97, 108, 120, 130, 141, 151, 161, 171,
    180, 189, 197, 205, 212, 219, 225, 231,
    236, 240, 244, 247, 250, 252, 253, 254,
    255, 254, 253, 252, 250, 247, 244, 240,
    236, 231, 225, 219, 212, 205, 197, 189,
    180, 171, 161, 151, 141, 130, 120, 108,
     97,  86,  74,  62,  49,  37,  25,  12
};

/* Apply a per-tick frequency delta in 1/8192 units. Inlined to keep call
 * depth shallow under the audio ISR (Watcom's __STK probe + large-model
 * far calls used to push us past 4 KB stack on Phase-3 effect rows). */
static __inline void apply_freq_delta(u32 *freq, s32 delta_8192ths)
{
    s32 adj = (s32)((u32)*freq >> 13) * delta_8192ths;
    s32 next = (s32)*freq + adj;
    if (next < 1) next = 1;
    if (next > 1000000) next = 1000000;
    *freq = (u32)next;
}

/* S3M note: a packed byte oct*16 + key. C-5 = 5*16 + 0 = 80. Period for
 * C-5 derives from the sample's c2spd. To avoid re-deriving an Amiga period
 * table, we pre-compute the Hz directly. */
static u32 s3m_note_freq(u8 note, u32 c2spd)
{
    u8 octave = note >> 4;
    u8 key    = note & 0x0F;
    /* 12 semitone ratios * 4096 (so we can do integer math). */
    static const u16 semitone_x4096[12] = {
        4096, 4340, 4598, 4871, 5161, 5468, 5793, 6137, 6502, 6889, 7298, 7732
    };
    /* Hz = c2spd * 2^(octave-5) * 2^(key/12). */
    u32 hz = (c2spd * semitone_x4096[key]) >> 12;
    if (octave > 5) hz <<= (octave - 5);
    else            hz >>= (5 - octave);
    return hz;
}

static void process_row(s3m_song_t *song)
{
    const audio_driver_t *hw = audiodrv_get_hardware_mixer();
    u8 ch;
    u8 *row;
    u8 pat_idx = song->order[song->current_order];
    if (pat_idx >= S3M_MAX_PATTERNS || !song->patterns[pat_idx]) {
        /* Skip 0xFE markers / 0xFF end-of-song. */
        if (song->order[song->current_order] == 0xFF) {
            song->current_order = 0;
            return;
        }
        song->current_order++;
        return;
    }
    row = song->patterns[pat_idx] + song->current_row * ROW_BYTES;
    for (ch = 0; ch < song->num_channels; ch++) {
        u8 *cell = row + ch * 5;
        u8 note = cell[0];
        u8 ins  = cell[1];
        u8 vol  = cell[2];
        u8 eff  = cell[3];
        u8 par  = cell[4];

        if (ins) {
            song->chans[ch].sample_num = ins;
            if (ins <= S3M_MAX_SAMPLES && song->samples[ins - 1].volume)
                song->chans[ch].volume = song->samples[ins - 1].volume;
        }
        if (vol && vol <= 64) song->chans[ch].volume = vol;

        if (note && note != 0xFF && note != 0xFE) {
            s3m_sample_t *s = (ins && ins <= S3M_MAX_SAMPLES)
                              ? &song->samples[ins - 1]
                              : 0;
            if (s && s->data && s->length) {
                u32 freq = s3m_note_freq(note, s->c2spd ? s->c2spd : 8363);
                hbool loop = (s->flags & 0x01) != 0;
                /* G (tone portamento) does NOT retrigger the sample; it
                 * only updates the slide target. Other notes retrigger. */
                if (eff == 7 /* G */ && song->chans[ch].freq) {
                    song->chans[ch].target_freq = freq;
                } else {
                    if (hw && hw->trigger_voice) {
                        hw->trigger_voice(ch, (u16)(ins - 1), freq,
                                          song->chans[ch].volume * 4,
                                          song->chans[ch].pan);
                    } else if (s->flags & 0x04) {
                        mixer_set_channel16(ch, (s16 *)s->data, s->length,
                                            s->loop_start, s->loop_end, loop);
                        mixer_set_frequency(ch, freq);
                    } else {
                        mixer_set_channel(ch, (s8 *)s->data, s->length,
                                          s->loop_start, s->loop_end, loop);
                        mixer_set_frequency(ch, freq);
                    }
                    song->chans[ch].freq        = freq;
                    song->chans[ch].base_freq   = freq;
                    song->chans[ch].target_freq = freq;
                    song->chans[ch].vib_pos     = 0;
                }
            }
        }

        song->chans[ch].effect       = eff;
        song->chans[ch].effect_param = par;

        switch (eff) {
        case 1:  /* A: set speed */     if (par) song->speed = par; break;
        case 2:  /* B: position jump */ song->current_order = par; song->current_row = 0xFF; break;
        case 3:  /* C: pattern break */ song->current_row = 0xFF; song->current_order++; break;
        case 4: { /* D: volume slide -- handle fine variants on tick 0 */
            u8 up = par >> 4, dn = par & 0x0F;
            if (up == 0x0F && dn != 0) {
                /* D Fx: fine slide DOWN by x once */
                if (song->chans[ch].volume >= dn) song->chans[ch].volume -= dn;
                else song->chans[ch].volume = 0;
            } else if (dn == 0x0F && up != 0) {
                /* D xF: fine slide UP by x once */
                if (song->chans[ch].volume + up <= 64) song->chans[ch].volume += up;
                else song->chans[ch].volume = 64;
            }
            /* otherwise per-tick handled in process_tick */
            break;
        }
        case 5: { /* E: porta down -- fine variants on tick 0 */
            u8 hi = par & 0xF0;
            u8 lo = par & 0x0F;
            if (par) song->chans[ch].port_speed = par;
            else par = song->chans[ch].port_speed;
            if (hi == 0xF0 && song->chans[ch].freq) {
                /* EFx: fine porta down once */
                apply_freq_delta(&song->chans[ch].freq, -(s32)(lo << 2));
                mixer_set_frequency(ch, song->chans[ch].freq);
            } else if (hi == 0xE0 && song->chans[ch].freq) {
                /* EEx: extra-fine porta down once */
                apply_freq_delta(&song->chans[ch].freq, -(s32)lo);
                mixer_set_frequency(ch, song->chans[ch].freq);
            }
            break;
        }
        case 6: { /* F: porta up -- fine variants on tick 0 */
            u8 hi = par & 0xF0;
            u8 lo = par & 0x0F;
            if (par) song->chans[ch].port_speed = par;
            else par = song->chans[ch].port_speed;
            if (hi == 0xF0 && song->chans[ch].freq) {
                apply_freq_delta(&song->chans[ch].freq, (s32)(lo << 2));
                mixer_set_frequency(ch, song->chans[ch].freq);
            } else if (hi == 0xE0 && song->chans[ch].freq) {
                apply_freq_delta(&song->chans[ch].freq, (s32)lo);
                mixer_set_frequency(ch, song->chans[ch].freq);
            }
            break;
        }
        case 7:  /* G: tone porta */
            if (par) song->chans[ch].tone_speed = par;
            break;
        case 8: { /* H: vibrato Hxy: x=speed, y=depth */
            u8 x = par >> 4, y = par & 0x0F;
            if (x) song->chans[ch].vib_speed = x;
            if (y) song->chans[ch].vib_depth = y;
            break;
        }
        case 19: /* T: set tempo */     if (par) song->tempo = par; break;
        case 20: { /* U: vibrato + volume slide -- depth/speed param shape same as H */
            u8 x = par >> 4, y = par & 0x0F;
            if (x) song->chans[ch].vib_speed = x;
            if (y) song->chans[ch].vib_depth = y;
            break;
        }
        default: break;
        }

        if (!(hw && hw->trigger_voice))
            mixer_set_volume(ch, (u8)((u16)song->chans[ch].volume * 4),
                              song->chans[ch].pan);
    }
}

static void apply_volume_slide(s3m_song_t *song, u8 ch, u8 par)
{
    u8 up = par >> 4, dn = par & 0x0F;
    /* Skip fine slides (xF / Fx) -- already applied on tick 0. */
    if (up == 0x0F || dn == 0x0F) return;
    if (up && song->chans[ch].volume + up <= 64)
        song->chans[ch].volume += up;
    else if (up && song->chans[ch].volume + up > 64)
        song->chans[ch].volume = 64;
    else if (dn && song->chans[ch].volume >= dn)
        song->chans[ch].volume -= dn;
    else if (dn)
        song->chans[ch].volume = 0;
    mixer_set_volume(ch, (u8)((u16)song->chans[ch].volume * 4),
                      song->chans[ch].pan);
}

static void process_tick(s3m_song_t *song)
{
    u8 ch;
    for (ch = 0; ch < song->num_channels; ch++) {
        u8 e = song->chans[ch].effect;
        u8 p = song->chans[ch].effect_param;
        switch (e) {
        case 4:  /* D: volume slide */
            apply_volume_slide(song, ch, p);
            break;

        case 5: { /* E: porta down (per-tick variant; fine handled on tick 0) */
            u8 hi = p & 0xF0;
            if (hi != 0xF0 && hi != 0xE0 && song->chans[ch].freq) {
                u8 amt = song->chans[ch].port_speed;
                apply_freq_delta(&song->chans[ch].freq, -(s32)(amt << 2));
                mixer_set_frequency(ch, song->chans[ch].freq);
            }
            break;
        }
        case 6: { /* F: porta up (per-tick variant; fine handled on tick 0) */
            u8 hi = p & 0xF0;
            if (hi != 0xF0 && hi != 0xE0 && song->chans[ch].freq) {
                u8 amt = song->chans[ch].port_speed;
                apply_freq_delta(&song->chans[ch].freq, (s32)(amt << 2));
                mixer_set_frequency(ch, song->chans[ch].freq);
            }
            break;
        }
        case 7: { /* G: tone portamento -- slide freq toward target_freq */
            u32 cur = song->chans[ch].freq;
            u32 tgt = song->chans[ch].target_freq;
            u8  spd = song->chans[ch].tone_speed;
            if (cur && tgt && spd) {
                s32 step = (s32)(spd << 2);
                if (cur < tgt) {
                    apply_freq_delta(&cur, step);
                    if (cur > tgt) cur = tgt;
                } else if (cur > tgt) {
                    apply_freq_delta(&cur, -step);
                    if (cur < tgt) cur = tgt;
                }
                song->chans[ch].freq = cur;
                mixer_set_frequency(ch, cur);
            }
            break;
        }
        case 8: { /* H: vibrato -- modulate freq around base_freq */
            u8 vp  = song->chans[ch].vib_pos;
            u8 vsp = song->chans[ch].vib_speed;
            u8 vd  = song->chans[ch].vib_depth;
            u32 base = song->chans[ch].base_freq;
            s32 sample;
            if (base && vd) {
                /* sin amplitude scaled to roughly +/- (vd * 0.5%) of base */
                if (vp < 32) sample =  (s32)sine_q[vp];
                else         sample = -(s32)sine_q[vp - 32];
                /* depth 0..15 -> very small percentage swing */
                {
                    s32 mod = (s32)(base >> 12) * (s32)vd * sample / 256;
                    s32 next = (s32)base + mod;
                    if (next < 1) next = 1;
                    song->chans[ch].freq = (u32)next;
                    mixer_set_frequency(ch, song->chans[ch].freq);
                }
            }
            song->chans[ch].vib_pos = (u8)((vp + vsp * 4) & 0x3F);
            break;
        }
        case 20: { /* U: vibrato + volume slide */
            /* Vibrato leg, identical to H */
            u8 vp  = song->chans[ch].vib_pos;
            u8 vsp = song->chans[ch].vib_speed;
            u8 vd  = song->chans[ch].vib_depth;
            u32 base = song->chans[ch].base_freq;
            s32 sample;
            if (base && vd) {
                if (vp < 32) sample =  (s32)sine_q[vp];
                else         sample = -(s32)sine_q[vp - 32];
                {
                    s32 mod = (s32)(base >> 12) * (s32)vd * sample / 256;
                    s32 next = (s32)base + mod;
                    if (next < 1) next = 1;
                    song->chans[ch].freq = (u32)next;
                    mixer_set_frequency(ch, song->chans[ch].freq);
                }
            }
            song->chans[ch].vib_pos = (u8)((vp + vsp * 4) & 0x3F);
            /* Volume slide leg uses last D parameter (memory) */
            apply_volume_slide(song, ch, p);
            break;
        }
        default: break;
        }
    }
}

void s3m_advance(s3m_song_t *song, u16 frames)
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
                    /* Skip 0xFE pattern markers and end-of-song. current_order
                     * is u8 so it naturally wraps before exceeding the table. */
                    while (song->order[song->current_order] >= S3M_MAX_PATTERNS) {
                        if (song->order[song->current_order] == 0xFF) {
                            song->current_order = 0;
                            break;
                        }
                        song->current_order++;
                    }
                }
            }
        }
    }
}
