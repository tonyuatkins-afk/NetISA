/*
 * ui/settings.c - Feature Unlock Matrix.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Renders every unlock entry under one of three categories: VISUALIZERS,
 * AUDIO, ARITHMETIC. Locked entries show their requirement on the right.
 * Unlocked entries show why (e.g. detected hardware) when available.
 */
#include "settings.h"
#include "screen.h"
#include "../unlock/unlock.h"
#include "../detect/fpu.h"
#include <stdio.h>
#include <string.h>

typedef struct { unlock_id_t id; const char *category; } category_map_t;

static const category_map_t cat_map[] = {
    /* Visualizers */
    { UL_FFT_256,     "VISUALIZERS" },
    { UL_BIPARTITE_FFT, "VISUALIZERS" },
    { UL_PLASMA,      "VISUALIZERS" },
    { UL_TUNNEL,      "VISUALIZERS" },
    { UL_PARTICLE,    "VISUALIZERS" },
    { UL_FIRE,        "VISUALIZERS" },
    { UL_WIREFRAME,   "VISUALIZERS" },
    { UL_STOCHASTIC,  "VISUALIZERS" },
    /* Audio */
    { UL_REALSOUND,   "AUDIO" },
    { UL_TANDY_PSG,   "AUDIO" },
    { UL_COVOX,       "AUDIO" },
    { UL_DISNEY,      "AUDIO" },
    { UL_OPL2_MIDI,   "AUDIO" },
    { UL_OPL3_MIDI,   "AUDIO" },
    { UL_CQM_FM,      "AUDIO" },
    { UL_SB_PCM,      "AUDIO" },
    { UL_SB16_PCM,    "AUDIO" },
    { UL_SB16_ASP,    "AUDIO" },
    { UL_GUS_HW_MIX,  "AUDIO" },
    { UL_GUS_MAX_LINEIN, "AUDIO" },
    { UL_AWE_SFONT,   "AUDIO" },
    { UL_MT32_MODE,   "AUDIO" },
    { UL_SC55_MODE,   "AUDIO" },
    { UL_XG_MODE,     "AUDIO" },
    { UL_AGOLD_DAC,   "AUDIO" },
    { UL_PAS16,       "AUDIO" },
    { UL_ESS_NATIVE,  "AUDIO" },
    { UL_TBEACH,      "AUDIO" },
    { UL_MPU401,      "AUDIO" },
    /* Arithmetic */
    { UL_ADAPTIVE_CORDIC, "ARITHMETIC" },
    { UL_EXACT_MIX,       "ARITHMETIC" },
    { UL_LOG_EFFECTS,     "ARITHMETIC" },
    { UL_SINC_RESAMPLE,   "ARITHMETIC" },
    { UL_KARAOKE,         "ARITHMETIC" },
    { UL_PARAM_EQ,        "ARITHMETIC" },
    { UL_CONV_REVERB,     "ARITHMETIC" },
    { UL_STEREO_WIDE,     "ARITHMETIC" },
    { UL_GAMMA_DITHER,    "ARITHMETIC" }
};

#define CAT_MAP_LEN (sizeof(cat_map) / sizeof(cat_map[0]))

static const char *categories[] = { "VISUALIZERS", "AUDIO", "ARITHMETIC" };

static void render_entry(u8 x, u8 y, u8 inner_w, const unlock_entry_t *e, const hw_profile_t *hw)
{
    const char *box = e->unlocked ? "[*]" : "[ ]";
    char line[80];
    u8 attr = e->unlocked ? ATTR_NORMAL : ATTR_DIM;
    sprintf(line, " %s %-*s", box, (int)(inner_w - 6), e->name);
    scr_puts(x, y, line, attr);
    if (e->unlocked) {
        if (hw->fpu_type != FPU_NONE && (e->id == UL_FFT_256 || e->id == UL_PLASMA ||
                                         e->id == UL_FIRE || e->id == UL_TUNNEL ||
                                         e->id == UL_ADAPTIVE_CORDIC ||
                                         e->id == UL_EXACT_MIX)) {
            char rh[40];
            sprintf(rh, "FPU: %s", hw->fpu_name);
            scr_puts((u8)(x + inner_w - (u8)strlen(rh) - 2), y, rh, ATTR_DIM);
        }
    } else if (e->requirement) {
        scr_puts((u8)(x + inner_w - (u8)strlen(e->requirement) - 2), y, e->requirement, ATTR_DIM);
    }
}

void settings_panel_show(const hw_profile_t *hw)
{
    u8 x = 2, y = 2;
    u8 w = (u8)(scr_cols() - 4);
    u8 h = (u8)(scr_rows() - 4);
    u8 row;
    u8 c, j;
    char buf[80];
    const unlock_entry_t *all = unlock_get_all();

    scr_clear(ATTR_NORMAL);
    scr_box(x, y, w, h, ATTR_BRIGHT);
    scr_puts((u8)(x + 2), y, " Settings: Feature Unlock Matrix ", ATTR_BRIGHT);

    row = (u8)(y + 2);
    for (c = 0; c < (u8)(sizeof(categories) / sizeof(categories[0])); c++) {
        scr_puts((u8)(x + 2), row++, categories[c], ATTR_CYAN);
        for (j = 0; j < CAT_MAP_LEN; j++) {
            if (strcmp(cat_map[j].category, categories[c]) != 0) continue;
            if (row >= y + h - 3) break;
            render_entry((u8)(x + 2), row++, (u8)(w - 4),
                         &all[cat_map[j].id], hw);
        }
        row++;
    }

    sprintf(buf, "  ---  %u of %u features unlocked  ---",
            unlock_count_enabled(), (unsigned)UL_COUNT);
    scr_puts((u8)(x + 2), (u8)(y + h - 3), buf, ATTR_BRIGHT);
    scr_puts((u8)(x + 2), (u8)(y + h - 2),
             " [*] = unlocked   [ ] = locked (requirement shown)   Esc to close ",
             ATTR_DIM);

    while (1) {
        u16 k = scr_getkey();
        if (k == KEY_ESC || k == KEY_F2) break;
    }
}
