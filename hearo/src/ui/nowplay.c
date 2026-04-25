/*
 * ui/nowplay.c - Now playing pane.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#include "nowplay.h"
#include "screen.h"
#include <stdio.h>
#include <string.h>

static char title[40]    = "Axel F";
static char artist[40]   = "Harold Faltermeyer";
static char album[40]    = "Beverly Hills Cop OST";
static char output[16]   = "GUS MAX";
static np_state_t state  = NP_PLAYING;
static u32 position_s    = 0;
static u32 duration_s    = 6 * 60 + 1;
static u8  volume        = 8;

void nowplay_set_track(const char *t, const char *a, const char *al, u32 dur)
{
    if (t)  { strncpy(title,  t,  sizeof(title) - 1);  title[sizeof(title) - 1] = '\0'; }
    if (a)  { strncpy(artist, a,  sizeof(artist) - 1); artist[sizeof(artist) - 1] = '\0'; }
    if (al) { strncpy(album,  al, sizeof(album) - 1);  album[sizeof(album) - 1] = '\0'; }
    duration_s = dur;
    position_s = 0;
}

void nowplay_set_output(const char *device_label)
{
    if (!device_label) return;
    strncpy(output, device_label, sizeof(output) - 1);
    output[sizeof(output) - 1] = '\0';
}

void nowplay_set_state(np_state_t s) { state = s; }

void nowplay_advance(u32 seconds)
{
    if (state != NP_PLAYING) return;
    position_s += seconds;
    if (position_s > duration_s) position_s = duration_s;
}

np_state_t nowplay_state(void)    { return state; }
u32 nowplay_position(void)        { return position_s; }
u32 nowplay_duration(void)        { return duration_s; }

void nowplay_render(u8 x, u8 y, u8 w, u8 h)
{
    char buf[80];
    u8 inner_w = (u8)(w - 4);
    u8 row = (u8)(y + 2);
    u8 i;
    scr_box(x, y, w, h, ATTR_DIM);
    scr_puts((u8)(x + 2), y, " Now Playing ", ATTR_BRIGHT);

    sprintf(buf, "%-*.*s", inner_w, inner_w, title);
    scr_puts((u8)(x + 2), row++, buf, ATTR_BRIGHT);
    sprintf(buf, "%-*.*s", inner_w, inner_w, artist);
    scr_puts((u8)(x + 2), row++, buf, ATTR_NORMAL);
    sprintf(buf, "%-*.*s", inner_w, inner_w, album);
    scr_puts((u8)(x + 2), row++, buf, ATTR_DIM);
    row++;

    {
        const char *st = state == NP_PLAYING ? "Playing" :
                         state == NP_PAUSED  ? "Paused"  : "Stopped";
        u32 mm_p = position_s / 60, ss_p = position_s % 60;
        u32 mm_d = duration_s / 60, ss_d = duration_s % 60;
        sprintf(buf, "%-9s  %02lu:%02lu / %02lu:%02lu",
                st, (unsigned long)mm_p, (unsigned long)ss_p,
                (unsigned long)mm_d, (unsigned long)ss_d);
        scr_puts((u8)(x + 2), row++, buf, ATTR_NORMAL);
    }

    /* Progress bar */
    {
        u8 bar_w = (u8)(inner_w);
        u8 filled = duration_s ? (u8)((position_s * (u32)bar_w) / duration_s) : 0;
        if (filled > bar_w) filled = bar_w;
        for (i = 0; i < bar_w; i++) {
            char ch = (i < filled) ? (scr_is_mono() ? '=' : (char)0xDB) : (scr_is_mono() ? '-' : (char)0xB0);
            scr_putch((u8)(x + 2 + i), row, ch, i < filled ? ATTR_GREEN : ATTR_DIM);
        }
        row++;
    }

    /* Volume bar + output */
    {
        u8 bar_w = 10;
        u8 vx = (u8)(x + 2);
        scr_puts(vx, row, "Vol: ", ATTR_NORMAL);
        for (i = 0; i < bar_w; i++) {
            char ch = (i < volume) ? (scr_is_mono() ? '#' : (char)0xDB) : (scr_is_mono() ? '.' : (char)0xB0);
            scr_putch((u8)(vx + 5 + i), row, ch, i < volume ? ATTR_GREEN : ATTR_DIM);
        }
        sprintf(buf, " [%s]", output);
        scr_puts((u8)(vx + 5 + bar_w + 1), row, buf, ATTR_CYAN);
    }
}
