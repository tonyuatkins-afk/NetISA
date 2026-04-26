/*
 * decode/it.c - Impulse Tracker loader. Phase 3 placeholder.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#include "it.h"
#include <stdio.h>
#include <string.h>

hbool it_load(const char *filename, it_song_t *song)
{
    FILE *f;
    char hdr[34];
    if (!filename || !song) return HFALSE;
    memset(song, 0, sizeof(*song));
    f = fopen(filename, "rb");
    if (!f) return HFALSE;
    if (fread(hdr, 1, 34, f) != 34) { fclose(f); return HFALSE; }
    fclose(f);
    if (memcmp(hdr, "IMPM", 4) != 0) return HFALSE;
    memcpy(song->title, hdr + 4, 26);
    song->title[26] = 0;
    song->loaded = 1;
    return HTRUE;
}

void it_free(it_song_t *s)                       { if (s) memset(s, 0, sizeof(*s)); }
void it_play_init(it_song_t *s, u32 r)           { (void)s; (void)r; }
void it_advance(it_song_t *s, u16 f)             { (void)s; (void)f; }
