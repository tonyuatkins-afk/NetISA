/*
 * ui/hallview.c - Hall of Recognition viewer.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#include "hallview.h"
#include "screen.h"
#include "../unlock/hall.h"
#include "../unlock/unlock.h"
#include <stdio.h>
#include <string.h>

void hallview_show(const hw_profile_t *hw)
{
    u8 x = 2, y = 2;
    u8 w = (u8)(scr_cols() - 4);
    u8 h = (u8)(scr_rows() - 4);
    u16 scroll = 0;
    u16 view_h;
    char buf[80];
    const hall_stats_t *st;

    scr_clear(ATTR_NORMAL);
    while (1) {
        u16 i;
        u8 row = (u8)(y + 1);
        u16 ev_n = hall_event_count();
        scr_box(x, y, w, h, ATTR_BRIGHT);
        scr_puts((u8)(x + 2), y, " Hall of Recognition ", ATTR_BRIGHT);

        sprintf(buf, "Machine fingerprint: %08lX", (unsigned long)hw->fingerprint);
        scr_puts((u8)(x + 2), row++, buf, ATTR_CYAN);
        sprintf(buf, "First boot: %s", hall_first_date());
        scr_puts((u8)(x + 2), row++, buf, ATTR_NORMAL);
        sprintf(buf, "Boot count: %u", hall_boot_count());
        scr_puts((u8)(x + 2), row++, buf, ATTR_NORMAL);
        row++;

        scr_puts((u8)(x + 2), row++, "HARDWARE TIMELINE", ATTR_CYAN);
        scr_hline((u8)(x + 2), row++, 17, scr_is_mono() ? '-' : (char)0xC4, ATTR_DIM);

        view_h = (u16)(h - (row - y) - 9);
        if (scroll > ev_n) scroll = 0;
        for (i = 0; i < view_h && (scroll + i) < ev_n; i++) {
            const hall_event_t *e = hall_event((u16)(scroll + i));
            if (!e) break;
            sprintf(buf, "%s  %-7s %s", e->date, e->category, e->text);
            scr_puts((u8)(x + 2), row++, buf, ATTR_NORMAL);
        }
        row++;

        st = hall_stats();
        scr_puts((u8)(x + 2), row++, "LIFETIME STATISTICS", ATTR_CYAN);
        scr_hline((u8)(x + 2), row++, 19, scr_is_mono() ? '-' : (char)0xC4, ATTR_DIM);
        sprintf(buf, "Hours played:    %5lu     Tracks played: %5lu",
                (unsigned long)(st->hours_played_x10 / 10),
                (unsigned long)st->tracks_played);
        scr_puts((u8)(x + 2), row++, buf, ATTR_NORMAL);
        sprintf(buf, "Unique tracks:   %5lu     Boot count:    %5lu",
                (unsigned long)st->unique_tracks,
                (unsigned long)st->boot_count);
        scr_puts((u8)(x + 2), row++, buf, ATTR_NORMAL);
        sprintf(buf, "Features unlocked: %3u    Hardware events: %3lu",
                unlock_count_enabled(), (unsigned long)st->hardware_events);
        scr_puts((u8)(x + 2), row++, buf, ATTR_NORMAL);

        scr_puts((u8)(x + 2), (u8)(y + h - 2),
                 "Up/Down to scroll   Esc to close", ATTR_DIM);

        {
            u16 k = scr_getkey();
            if (k == KEY_ESC || k == KEY_F10) break;
            if (k == KEY_UP   && scroll > 0) scroll--;
            if (k == KEY_DOWN && (u16)(scroll + 1) < ev_n) scroll++;
            if (k == KEY_PGUP)               scroll = (scroll > 5) ? scroll - 5 : 0;
            if (k == KEY_PGDN)               scroll = ((u16)(scroll + 5) < ev_n) ? scroll + 5 : ev_n;
        }
    }
}
