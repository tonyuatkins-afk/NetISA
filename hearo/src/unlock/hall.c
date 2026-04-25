/*
 * unlock/hall.c - Hall of Recognition.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * The Hall is a plain text file (HEARO.HAL). One event per line:
 *   YYYY-MM-DD CATEGORY: text
 * Plus a small set of stat lines after a "[stats]" section. We choose text
 * over binary so the user can grep, edit, and back up the file.
 *
 * On boot we load the file, snapshot the previous fingerprint, run detection,
 * compare, and append "new since last boot" events. The boot count is
 * incremented and the file is saved at exit.
 */
#include "hall.h"
#include "unlock.h"
#include "../detect/cpu.h"
#include "../detect/fpu.h"
#include "../detect/video.h"
#include "../detect/audio.h"
#include <stdio.h>
#include <string.h>

#define HALL_MAX_EVENTS 256

static hall_event_t events[HALL_MAX_EVENTS];
static u16 event_count = 0;
static hall_stats_t stats;
static u32 prev_fingerprint = 0;
static char first_date[11] = "";
static u16 changes_added = 0;
static char changes_buf[160];

static void add_event(const char *date, const char *category, const char *text)
{
    if (event_count >= HALL_MAX_EVENTS) return;
    strncpy(events[event_count].date, date, 10);
    events[event_count].date[10] = '\0';
    strncpy(events[event_count].category, category, sizeof(events[0].category) - 1);
    events[event_count].category[sizeof(events[0].category) - 1] = '\0';
    strncpy(events[event_count].text, text, sizeof(events[0].text) - 1);
    events[event_count].text[sizeof(events[0].text) - 1] = '\0';
    event_count++;
}

void hall_load(const char *path)
{
    FILE *f;
    char line[128];
    char section[16] = "events";
    event_count = 0;
    memset(&stats, 0, sizeof(stats));
    prev_fingerprint = 0;
    first_date[0] = '\0';

    f = fopen(path, "r");
    if (!f) return;

    while (fgets(line, sizeof(line), f)) {
        u16 n = (u16)strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;
        if (line[0] == '[') {
            if      (strstr(line, "events")) strcpy(section, "events");
            else if (strstr(line, "stats"))  strcpy(section, "stats");
            else if (strstr(line, "header")) strcpy(section, "header");
            continue;
        }

        if (strcmp(section, "header") == 0) {
            if (strncmp(line, "first=", 6) == 0)        strncpy(first_date, line + 6, 10);
            else if (strncmp(line, "fingerprint=", 12) == 0) {
                u32 v = 0;
                sscanf(line + 12, "%lx", &v);
                prev_fingerprint = v;
            }
        } else if (strcmp(section, "events") == 0) {
            if (event_count < HALL_MAX_EVENTS) {
                hall_event_t *e = &events[event_count];
                /* Format: YYYY-MM-DD CATEGORY: text */
                if (n > 12 && line[10] == ' ') {
                    char *colon;
                    strncpy(e->date, line, 10);
                    e->date[10] = '\0';
                    colon = strchr(line + 11, ':');
                    if (colon) {
                        u16 catlen = (u16)(colon - (line + 11));
                        if (catlen >= sizeof(e->category)) catlen = sizeof(e->category) - 1;
                        memcpy(e->category, line + 11, catlen);
                        e->category[catlen] = '\0';
                        strncpy(e->text, colon + 2, sizeof(e->text) - 1);
                        e->text[sizeof(e->text) - 1] = '\0';
                        event_count++;
                    }
                }
            }
        } else if (strcmp(section, "stats") == 0) {
            u32 v;
            if (sscanf(line, "boot_count=%lu", &v) == 1) stats.boot_count = v;
            else if (sscanf(line, "tracks_played=%lu", &v) == 1) stats.tracks_played = v;
            else if (sscanf(line, "unique_tracks=%lu", &v) == 1) stats.unique_tracks = v;
            else if (sscanf(line, "hours_played_x10=%lu", &v) == 1) stats.hours_played_x10 = v;
            else if (sscanf(line, "features_unlocked=%lu", &v) == 1) stats.features_unlocked = v;
            else if (sscanf(line, "hardware_events=%lu", &v) == 1) stats.hardware_events = v;
        }
    }
    fclose(f);
}

static hbool category_seen(const char *cat, const char *text)
{
    u16 i;
    for (i = 0; i < event_count; i++) {
        if (strcmp(events[i].category, cat) == 0 &&
            strcmp(events[i].text, text) == 0) return HTRUE;
    }
    return HFALSE;
}

static void append_change(const char *fmt)
{
    if (changes_added == 0) {
        strcpy(changes_buf, fmt);
    } else {
        strncat(changes_buf, ", ", sizeof(changes_buf) - strlen(changes_buf) - 1);
        strncat(changes_buf, fmt, sizeof(changes_buf) - strlen(changes_buf) - 1);
    }
    changes_added++;
}

void hall_update(const hw_profile_t *hw)
{
    char text[64];
    changes_added = 0;
    changes_buf[0] = '\0';

    if (first_date[0] == '\0') strcpy(first_date, hw->detect_date);

    /* CPU */
    sprintf(text, "%s @ %u MHz", cpu_name(hw->cpu_class), hw->cpu_mhz);
    if (!category_seen("CPU", text)) { add_event(hw->detect_date, "CPU", text); append_change("CPU"); stats.hardware_events++; }

    /* FPU */
    if (hw->fpu_type != FPU_NONE) {
        sprintf(text, "%s", hw->fpu_name);
        if (!category_seen("FPU", text)) { add_event(hw->detect_date, "FPU", text); append_change("FPU"); stats.hardware_events++; }
    }

    /* Memory */
    sprintf(text, "%uK conv + %luK XMS", hw->mem_conv_kb, hw->mem_xms_kb);
    if (!category_seen("MEMORY", text)) { add_event(hw->detect_date, "MEMORY", text); append_change("memory"); stats.hardware_events++; }

    /* Video */
    if (hw->vid_name[0]) {
        if (!category_seen("VIDEO", hw->vid_name)) { add_event(hw->detect_date, "VIDEO", hw->vid_name); append_change("video"); stats.hardware_events++; }
    }

    /* Audio cards */
    {
        u8 i;
        for (i = 0; i < hw->aud_card_count; i++) {
            if (!category_seen("AUDIO", hw->aud_cards[i])) {
                add_event(hw->detect_date, "AUDIO", hw->aud_cards[i]);
                append_change("audio");
                stats.hardware_events++;
            }
        }
    }

    /* Input */
    if (hw->has_mouse && !category_seen("INPUT", "Mouse"))      { add_event(hw->detect_date, "INPUT", "Mouse");      stats.hardware_events++; }
    if (hw->has_joystick && !category_seen("INPUT", "Joystick")) { add_event(hw->detect_date, "INPUT", "Joystick");   stats.hardware_events++; }

    /* NetISA */
    if (hw->nisa_status == NISA_LINK_UP) {
        sprintf(text, "NetISA at %03Xh fw %s", hw->nisa_base, hw->nisa_fw);
        if (!category_seen("NETISA", text)) { add_event(hw->detect_date, "NETISA", text); append_change("NetISA"); stats.hardware_events++; }
    }

    if (unlock_count_enabled() > stats.features_unlocked) {
        stats.features_unlocked = unlock_count_enabled();
    }

    stats.boot_count++;
    prev_fingerprint = hw->fingerprint;
}

void hall_save(const char *path)
{
    FILE *f = fopen(path, "w");
    u16 i;
    if (!f) return;
    fprintf(f, "[header]\n");
    fprintf(f, "first=%s\n", first_date);
    fprintf(f, "fingerprint=%08lX\n\n", (unsigned long)prev_fingerprint);
    fprintf(f, "[events]\n");
    for (i = 0; i < event_count; i++) {
        fprintf(f, "%s %s: %s\n", events[i].date, events[i].category, events[i].text);
    }
    fprintf(f, "\n[stats]\n");
    fprintf(f, "boot_count=%lu\n", (unsigned long)stats.boot_count);
    fprintf(f, "tracks_played=%lu\n", (unsigned long)stats.tracks_played);
    fprintf(f, "unique_tracks=%lu\n", (unsigned long)stats.unique_tracks);
    fprintf(f, "hours_played_x10=%lu\n", (unsigned long)stats.hours_played_x10);
    fprintf(f, "features_unlocked=%lu\n", (unsigned long)stats.features_unlocked);
    fprintf(f, "hardware_events=%lu\n", (unsigned long)stats.hardware_events);
    fclose(f);
}

u16 hall_boot_count(void)        { return (u16)stats.boot_count; }
const char *hall_first_date(void) { return first_date[0] ? first_date : "today"; }
hbool hall_has_changes(void)     { return (changes_added > 0) ? HTRUE : HFALSE; }
const char *hall_changes_summary(void) { return changes_buf; }

u16 hall_event_count(void) { return event_count; }
const hall_event_t *hall_event(u16 i) { return (i < event_count) ? &events[i] : 0; }
const hall_stats_t *hall_stats(void)  { return &stats; }

void hall_record_play(u32 track_seconds, hbool was_new)
{
    stats.tracks_played++;
    if (was_new) stats.unique_tracks++;
    /* Convert seconds to tenths of an hour: seconds * 10 / 3600. */
    stats.hours_played_x10 += track_seconds / 360UL;
}
