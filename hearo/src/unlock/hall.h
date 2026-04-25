/*
 * unlock/hall.h - Hall of Recognition.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_HALL_H
#define HEARO_HALL_H

#include "../hearo.h"

void hall_load(const char *path);
void hall_update(const hw_profile_t *hw);
void hall_save(const char *path);

u16  hall_boot_count(void);
const char *hall_first_date(void);
hbool hall_has_changes(void);
const char *hall_changes_summary(void);

/* Iterators for the hallview UI. */
typedef struct {
    char date[11];
    char category[16];
    char text[64];
} hall_event_t;

u16 hall_event_count(void);
const hall_event_t *hall_event(u16 i);

/* Lifetime stats */
typedef struct {
    u32 hours_played_x10;   /* tenths of an hour */
    u32 tracks_played;
    u32 unique_tracks;
    u32 boot_count;
    u32 features_unlocked;  /* peak observed */
    u32 hardware_events;
} hall_stats_t;

const hall_stats_t *hall_stats(void);
void hall_record_play(u32 track_seconds, hbool was_new);

#endif
