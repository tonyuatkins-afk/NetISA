/*
 * ui/ui.c - Main interactive loop.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Four pane layout:
 *   Browser  (left top)
 *   Playlist (left bottom)
 *   Now playing (right top)
 *   Spectrum (right bottom)
 *
 * Plus title row, menu bar, and bottom status row. Tab cycles focus among
 * the panes that accept it. Alt+letter opens menus. Function keys are
 * shortcuts (F2 = settings, F10 = hall, F12 = exit).
 */
#include "ui.h"
#include "screen.h"
#include "menu.h"
#include "browser.h"
#include "nowplay.h"
#include "spectrum.h"
#include "settings.h"
#include "hallview.h"
#include "../platform/timer.h"
#include "../detect/audio.h"
#include "../detect/fpu.h"
#include "../unlock/whisper.h"
#include "../unlock/unlock.h"
#include <stdio.h>
#include <string.h>

static ui_focus_t focus = UI_FOCUS_BROWSER;
static hbool need_redraw = HTRUE;
static u32  last_spectrum_step_ms = 0;
static const hw_profile_t *cur_hw = 0;

ui_focus_t ui_focus(void) { return focus; }
void ui_set_focus(ui_focus_t f) { focus = f; need_redraw = HTRUE; }
void ui_request_redraw(void) { need_redraw = HTRUE; }

static const char *output_label_for(const hw_profile_t *hw)
{
    if (hw->aud_devices & AUD_GUS_MAX)  return "GUS MAX";
    if (hw->aud_devices & AUD_GUS)      return "GUS";
    if (hw->aud_devices & AUD_SB_AWE32) return "AWE32";
    if (hw->aud_devices & AUD_SB_AWE64) return "AWE64";
    if (hw->aud_devices & AUD_SB_16)    return "SB16";
    if (hw->aud_devices & AUD_SB_PRO2)  return "SB Pro 2";
    if (hw->aud_devices & AUD_SB_PRO)   return "SB Pro";
    if (hw->aud_devices & AUD_SB_20)    return "SB 2.0";
    if (hw->aud_devices & AUD_ADLIB)    return "AdLib";
    if (hw->aud_devices & AUD_DISNEY)   return "Disney";
    if (hw->aud_devices & AUD_COVOX)    return "Covox";
    return "PC Speaker";
}

static void render_title(void)
{
    char buf[80];
    sprintf(buf, " HEARO v%s ", HEARO_VER_STRING);
    scr_hline(0, 0, scr_cols(), ' ', ATTR_TITLE_BAR);
    scr_puts(0, 0, buf, ATTR_TITLE_BAR);
    /* Right side: FPU/SVGA badges. */
    if (cur_hw) {
        char rh[40] = "";
        if (cur_hw->fpu_type != FPU_NONE) strcat(rh, "FPU ");
        if (cur_hw->vid_class == VID_SVGA) strcat(rh, "SVGA ");
        if (cur_hw->aud_devices & (AUD_GUS | AUD_GUS_MAX)) strcat(rh, "GUS ");
        if (rh[0]) scr_puts((u8)(scr_cols() - (u8)strlen(rh) - 1), 0, rh, ATTR_TITLE_BAR);
    }
}

static void render_status_bar(void)
{
    const char *help = " Space:Play/Pause  N:Next  P:Prev  Tab:Focus  F2:Settings  F10:Hall  F12:Exit ";
    u8 y = (u8)(scr_rows() - 1);
    scr_hline(0, y, scr_cols(), ' ', ATTR_STATUS_BAR);
    scr_puts(0, y, help, ATTR_STATUS_BAR);
    if (whisper_pending()) {
        const char *m = whisper_message();
        u8 x = (u8)(scr_cols() - (u8)strlen(m) - 2);
        scr_puts(x, y, m, ATTR_STATUS_BAR);
    }
}

static void render_playlist(u8 x, u8 y, u8 w, u8 h, hbool focused)
{
    static const char *items[] = {
        "1. AXELF.MOD",
        "2. CATSTEP.S3M",
        "3. CREAM.XM",
        "4. ENIGMA.MOD",
        "5. POPCORN.MOD"
    };
    u8 i;
    u8 box_attr = focused ? ATTR_BRIGHT : ATTR_DIM;
    scr_box(x, y, w, h, box_attr);
    scr_puts((u8)(x + 2), y, " Playlist ", focused ? ATTR_BRIGHT : ATTR_NORMAL);
    for (i = 0; i < 5 && (1 + i) < h - 1; i++) {
        u8 ry = (u8)(y + 1 + i);
        u8 attr = (i == 3 && focused) ? ATTR_SELECTED : ATTR_NORMAL;
        scr_fill((u8)(x + 1), ry, (u8)(w - 2), 1, ' ', attr);
        scr_putch((u8)(x + 2), ry, i == 3 ? '>' : ' ', attr);
        scr_puts((u8)(x + 4), ry, items[i], attr);
        if (i == 3) {
            scr_puts((u8)(x + w - 8), ry, "<PLAY>", attr);
        }
    }
}

static void redraw_all(void)
{
    u8 col_split;
    u8 row_split;
    u8 cols = scr_cols();
    u8 rows = scr_rows();

    scr_clear(ATTR_NORMAL);
    render_title();
    menu_bar_render();

    col_split = (u8)(cols / 2);
    row_split = (u8)(rows / 2 + 1);

    /* Left top: browser */
    browser_render(0, 1, col_split, (u8)(row_split - 1), focus == UI_FOCUS_BROWSER);
    /* Left bottom: playlist */
    render_playlist(0, row_split, col_split, (u8)(rows - row_split - 1), focus == UI_FOCUS_PLAYLIST);
    /* Right top: now playing */
    nowplay_render(col_split, 1, (u8)(cols - col_split), (u8)(row_split - 1));
    /* Right bottom: spectrum */
    spectrum_render(col_split, row_split, (u8)(cols - col_split), (u8)(rows - row_split - 1));

    render_status_bar();
}

static void open_menu(menu_id_t which)
{
    menu_action_t a = menu_dispatch(which);
    switch (a) {
        case MA_EXIT:
            /* Caller checks this state via a flag; for v1.0 we exit by sending KEY_ESC */
            break;
        case MA_SETT_UNLOCKS:
            settings_panel_show(cur_hw);
            need_redraw = HTRUE;
            break;
        case MA_HELP_HALL:
            hallview_show(cur_hw);
            need_redraw = HTRUE;
            break;
        case MA_PLAY_PAUSE:
            nowplay_set_state(nowplay_state() == NP_PLAYING ? NP_PAUSED : NP_PLAYING);
            need_redraw = HTRUE;
            break;
        case MA_STOP:
            nowplay_set_state(NP_STOPPED);
            need_redraw = HTRUE;
            break;
        default:
            need_redraw = HTRUE;
            break;
    }
}

static menu_id_t menu_id_from_action(menu_action_t a)
{
    return (menu_id_t)((int)a - 1000);
}

void ui_run(const hw_profile_t *hw)
{
    hbool quit = HFALSE;

    cur_hw = hw;

    scr_init();
    scr_cursor(HFALSE);
    browser_init(0);
    spectrum_init(hw->fpu_type != FPU_NONE);
    nowplay_set_output(output_label_for(hw));
    whisper_check(unlock_get_all());

    while (!quit) {
        u32 now;
        if (need_redraw) {
            redraw_all();
            need_redraw = HFALSE;
        }
        now = timer_ms();
        if (now - last_spectrum_step_ms > 100UL) {
            spectrum_step();
            spectrum_render((u8)(scr_cols() / 2), (u8)(scr_rows() / 2 + 1),
                            (u8)(scr_cols() - scr_cols() / 2),
                            (u8)(scr_rows() - (scr_rows() / 2 + 1) - 1));
            last_spectrum_step_ms = now;
        }
        if (!scr_keypending()) continue;

        {
            u16 key = scr_getkey();
            menu_action_t a;
            if (key == KEY_F12 || key == KEY_ALT_X) { quit = HTRUE; break; }
            if (key == KEY_TAB) {
                ui_set_focus((ui_focus_t)(((int)focus + 1) % UI_FOCUS_COUNT));
                continue;
            }
            if (key == KEY_F2)  { settings_panel_show(hw); need_redraw = HTRUE; continue; }
            if (key == KEY_F10) { hallview_show(hw);       need_redraw = HTRUE; continue; }
            if (key == KEY_SPACE) {
                nowplay_set_state(nowplay_state() == NP_PLAYING ? NP_PAUSED : NP_PLAYING);
                need_redraw = HTRUE; continue;
            }
            a = menu_from_key(key);
            if ((int)a >= 1000) { open_menu(menu_id_from_action(a)); continue; }
            if (a == MA_EXIT) { quit = HTRUE; break; }

            if (focus == UI_FOCUS_BROWSER) {
                if (browser_handle_key(key)) need_redraw = HTRUE;
            }
            if (key == KEY_ESC) { quit = HTRUE; break; }
        }
    }

    scr_cursor(HTRUE);
    scr_clear(ATTR_NORMAL);
}
