/*
 * ui/boot.c - HEARO boot screen.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Layout:
 *   Rows 1..6  HEARO logo (block characters).
 *   Row  7     "NetISA Music Player vN.N    MIT License"
 *   Rows 9..   Hardware report. Each detected component renders as:
 *                <category>  <name>           detected
 *                            <unlock list>     ENABLED (right justified col 72)
 *   Last row   "Your machine has unlocked NN features. Press any key to begin."
 */
#include "boot.h"
#include "screen.h"
#include "../detect/cpu.h"
#include "../detect/fpu.h"
#include "../detect/video.h"
#include "../detect/audio.h"
#include "../unlock/unlock.h"
#include "../unlock/hall.h"
#include <stdio.h>
#include <string.h>

#define LOGO_X 1
#define COL_NAME    11
#define COL_STATUS  72

/* The logo lives in CP437 box-drawing characters when rendered in colour, with
 * a plain ASCII fallback for MDA/Hercules. Bytes used (CP437):
 *   0xDB full block, 0xC9 box-double-down-right, 0xBB down-left,
 *   0xC8 up-right, 0xBC up-left, 0xBA double vertical, 0xCD double horizontal.
 * Six rows for colour, padded to six rows on mono so the rest of the boot
 * screen layout stays at fixed Y offsets across adapters.
 */
static const char *logo_lines[] = {
    "  ##  ##  ###  ##   ##  ###",
    "  ##  ##  #    ##   ##/  ##",
    "  ######  ##   ##   ##   ##",
    "  ##  ##  #    ## # ##/  ##",
    "  ##  ##  ###  ##/##/   ###",
    ""
};

static const char *logo_lines_color[] = {
    " \xDB\xDB\xBB  \xDB\xDB\xBB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xBB \xDB\xDB\xDB\xDB\xDB\xBB \xDB\xDB\xDB\xDB\xDB\xDB\xBB  \xDB\xDB\xDB\xDB\xDB\xDB\xBB",
    " \xDB\xDB\xBA  \xDB\xDB\xBA\xDB\xDB\xC9\xCD\xCD\xCD\xCD\xBC\xDB\xDB\xC9\xCD\xCD\xDB\xDB\xBB\xDB\xDB\xC9\xCD\xCD\xDB\xDB\xBB\xDB\xDB\xC9\xCD\xCD\xCD\xDB\xDB\xBB",
    " \xDB\xDB\xDB\xDB\xDB\xDB\xDB\xBA\xDB\xDB\xDB\xDB\xDB\xBB  \xDB\xDB\xDB\xDB\xDB\xDB\xDB\xBA\xDB\xDB\xDB\xDB\xDB\xDB\xC9\xBC\xDB\xDB\xBA   \xDB\xDB\xBA",
    " \xDB\xDB\xC9\xCD\xCD\xDB\xDB\xBA\xDB\xDB\xC9\xCD\xCD\xBC  \xDB\xDB\xC9\xCD\xCD\xDB\xDB\xBA\xDB\xDB\xC9\xCD\xCD\xDB\xDB\xBB\xDB\xDB\xBA   \xDB\xDB\xBA",
    " \xDB\xDB\xBA  \xDB\xDB\xBA\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xBB\xDB\xDB\xBA  \xDB\xDB\xBA\xDB\xDB\xBA  \xDB\xDB\xBA\xC8\xDB\xDB\xDB\xDB\xDB\xDB\xC9\xBC",
    " \xC8\xCD\xBC  \xC8\xCD\xBC\xC8\xCD\xCD\xCD\xCD\xCD\xCD\xBC\xC8\xCD\xBC  \xC8\xCD\xBC\xC8\xCD\xBC  \xC8\xCD\xBC \xC8\xCD\xCD\xCD\xCD\xCD\xBC"
};

#define LOGO_ROWS 6

/* Compares two ISO dates "YYYY-MM-DD". Returns the number of years between
 * `today` and `first` when their MM-DD parts match (i.e. it is the anniversary
 * day), zero otherwise. We deliberately accept Feb 29 as a same-day match: a
 * machine first detected on a leap day gets celebrated annually on Feb 29
 * even when that calendar slot does not exist (a small mercy). */
static u16 anniversary_years(const char *first_date, const char *today_date)
{
    int fy, ty, diff;
    if (!first_date || !today_date) return 0;
    if (strlen(first_date) < 10 || strlen(today_date) < 10) return 0;
    if (strcmp(first_date + 5, today_date + 5) != 0) return 0;
    fy = (first_date[0] - '0') * 1000 + (first_date[1] - '0') * 100
       + (first_date[2] - '0') * 10  + (first_date[3] - '0');
    ty = (today_date[0] - '0') * 1000 + (today_date[1] - '0') * 100
       + (today_date[2] - '0') * 10  + (today_date[3] - '0');
    diff = ty - fy;
    return (diff > 0 && diff < 100) ? (u16)diff : 0;
}

static void put_status(u8 y, const char *status, u8 attr)
{
    u8 len = (u8)strlen(status);
    u8 x;
    if (len > scr_cols()) len = scr_cols();
    x = (u8)(scr_cols() - len - 2);
    scr_puts(x, y, status, attr);
}

static u8 render_unlock_list(u8 x, u8 y_start, u8 max_cols, unlock_id_t *ids, u8 n)
{
    char line[80];
    u8 cur_y = y_start;
    u8 i;
    line[0] = '\0';
    for (i = 0; i < n; i++) {
        const unlock_entry_t *e = unlock_get(ids[i]);
        u8 namelen;
        if (!e || !e->unlocked) continue;
        namelen = (u8)(strlen(e->name) + 2);
        if (strlen(line) + namelen >= max_cols) {
            scr_puts(x, cur_y, line, ATTR_GREEN);
            cur_y++;
            line[0] = '\0';
        }
        if (line[0] != '\0') strcat(line, ", ");
        strcat(line, e->name);
    }
    if (line[0] != '\0') {
        scr_puts(x, cur_y, line, ATTR_GREEN);
        cur_y++;
    }
    return cur_y;
}

void boot_screen_render(const hw_profile_t *hw)
{
    u8 row = 0;
    u8 i;
    char buf[80];

    scr_init();
    scr_cursor(HFALSE);
    scr_clear(ATTR_NORMAL);

    /* Logo */
    {
        const char **lines = scr_is_mono() ? logo_lines : logo_lines_color;
        for (i = 0; i < LOGO_ROWS; i++) {
            scr_puts(LOGO_X, (u8)(1 + i), lines[i], ATTR_BRIGHT);
        }
    }

    sprintf(buf, "NetISA Music Player v%s            MIT License", HEARO_VER_STRING);
    scr_puts(LOGO_X, (u8)(1 + LOGO_ROWS + 1), buf, ATTR_NORMAL);
    row = (u8)(1 + LOGO_ROWS + 3);

    /* Anniversary */
    {
        u16 years = anniversary_years(hall_first_date(), hw->detect_date);
        if (years == 1) {
            sprintf(buf, "One year ago today, you ran HEARO on this machine for the first time.");
            scr_puts(LOGO_X, row, buf, ATTR_YELLOW);
            row += 2;
        } else if (years > 1) {
            sprintf(buf, "%u years ago today, you first ran HEARO on this machine.", years);
            scr_puts(LOGO_X, row, buf, ATTR_YELLOW);
            row += 2;
        }
    }

    /* New since last boot */
    if (hall_has_changes() && hall_boot_count() > 1) {
        sprintf(buf, "New since last boot: %s", hall_changes_summary());
        scr_puts(LOGO_X, row, buf, ATTR_YELLOW);
        row++;
        row++;
    }

    /* CPU */
    {
        sprintf(buf, "%s @ %u MHz%s", cpu_name(hw->cpu_class), hw->cpu_mhz,
                hw->cpu_overclock ? " (oc)" : "");
        scr_puts(LOGO_X, row, "CPU", ATTR_CYAN);
        scr_puts(COL_NAME, row, buf, ATTR_NORMAL);
        put_status(row, "detected", ATTR_NORMAL);
        row++;
    }

    /* FPU */
    if (hw->fpu_type != FPU_NONE) {
        scr_puts(COL_NAME, row, hw->fpu_name, ATTR_NORMAL);
        put_status(row, "ENABLED", ATTR_ENABLED);
        row++;
        {
            unlock_id_t fpu_ids[] = {
                UL_FFT_256, UL_SINC_RESAMPLE, UL_EXACT_MIX,
                UL_PLASMA, UL_TUNNEL, UL_PARTICLE, UL_FIRE, UL_WIREFRAME,
                UL_KARAOKE, UL_PARAM_EQ, UL_CONV_REVERB, UL_STEREO_WIDE,
                UL_GAMMA_DITHER, UL_ADAPTIVE_CORDIC, UL_LOG_EFFECTS,
                UL_16CH_MIX
            };
            u8 prefix_x = COL_NAME + 2;
            u8 max = (u8)(COL_STATUS - prefix_x);
            scr_puts(prefix_x, row, "Unlocked: ", ATTR_DIM);
            row = render_unlock_list((u8)(prefix_x + 10), row,
                                     (u8)(max - 10),
                                     fpu_ids, sizeof(fpu_ids) / sizeof(fpu_ids[0]));
        }
    } else {
        unlock_id_t no_fpu_ids[] = { UL_STOCHASTIC, UL_BIPARTITE_FFT };
        scr_puts(COL_NAME, row, "Math coprocessor not present", ATTR_DIM);
        put_status(row, "skipped", ATTR_DIM);
        row++;
        scr_puts(COL_NAME + 2, row, "FPU-less features: ", ATTR_DIM);
        row = render_unlock_list(COL_NAME + 21, row, (u8)(COL_STATUS - COL_NAME - 21),
                                 no_fpu_ids, 2);
    }
    row++;

    /* Memory */
    sprintf(buf, "%uK conventional + %luK extended", hw->mem_conv_kb, hw->mem_xms_kb);
    scr_puts(LOGO_X, row, "Memory", ATTR_CYAN);
    scr_puts(COL_NAME, row, buf, ATTR_NORMAL);
    put_status(row, "detected", ATTR_NORMAL);
    row++;
    {
        unlock_id_t mem_ids[] = { UL_FULL_LIBRARY, UL_VIS_PRELOAD };
        scr_puts(COL_NAME + 2, row, "Unlocked: ", ATTR_DIM);
        row = render_unlock_list(COL_NAME + 12, row, COL_STATUS - COL_NAME - 12, mem_ids, 2);
    }
    row++;

    /* Video */
    scr_puts(LOGO_X, row, "Video", ATTR_CYAN);
    scr_puts(COL_NAME, row, hw->vid_name, ATTR_NORMAL);
    put_status(row, "detected", ATTR_NORMAL);
    row++;
    if (hw->vid_class == VID_SVGA) {
        sprintf(buf, "VESA %u.%u @ %ux%ux256",
                hw->vesa.ver_major, hw->vesa.ver_minor,
                hw->vesa.max_w, hw->vesa.max_h);
        scr_puts(COL_NAME, row, buf, ATTR_NORMAL);
        put_status(row, "ENABLED", ATTR_ENABLED);
        row++;
    }
    row++;

    /* Audio */
    scr_puts(LOGO_X, row, "Audio", ATTR_CYAN);
    for (i = 0; i < hw->aud_card_count; i++) {
        if (i == 0) {
            scr_puts(COL_NAME, row, hw->aud_cards[i], ATTR_NORMAL);
        } else {
            scr_puts(COL_NAME, row, hw->aud_cards[i], ATTR_NORMAL);
        }
        put_status(row, "detected", ATTR_NORMAL);
        row++;
    }
    row++;

    /* NetISA */
    scr_puts(LOGO_X, row, "NetISA", ATTR_CYAN);
    if (hw->nisa_status == NISA_LINK_UP) {
        sprintf(buf, "card present, fw %s", hw->nisa_fw);
        scr_puts(COL_NAME, row, buf, ATTR_NORMAL);
        put_status(row, "detected", ATTR_NORMAL);
    } else {
        scr_puts(COL_NAME, row, "not found", ATTR_DIM);
    }
    row++;
    row++;

    /* Footer rule + closing line */
    if (row < scr_rows() - 2) {
        u8 ruley = (u8)(scr_rows() - 3);
        scr_hline(LOGO_X, ruley,
                  (u8)(scr_cols() - LOGO_X - 1),
                  scr_is_mono() ? '-' : (char)0xC4, ATTR_DIM);
    }

    sprintf(buf, "Your machine has unlocked %u features. Press any key to begin.",
            unlock_count_enabled());
    scr_puts(LOGO_X, (u8)(scr_rows() - 2), buf, ATTR_BRIGHT);

    scr_getkey();
    scr_clear(ATTR_NORMAL);
}
