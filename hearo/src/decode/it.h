/*
 * decode/it.h - Impulse Tracker loader (Phase 3 stub).
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_DECODE_IT_H
#define HEARO_DECODE_IT_H
#include "../hearo.h"
typedef struct { char title[27]; u8 loaded; } it_song_t;
hbool it_load(const char *filename, it_song_t *song);
void  it_free(it_song_t *song);
void  it_play_init(it_song_t *song, u32 r);
void  it_advance(it_song_t *song, u16 f);
#endif
