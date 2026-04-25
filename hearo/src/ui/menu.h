/*
 * ui/menu.h - Menu bar and pull-down system.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_UI_MENU_H
#define HEARO_UI_MENU_H

#include "../hearo.h"

typedef enum {
    MENU_FILE=0, MENU_PLAYBACK, MENU_VIEW, MENU_SETTINGS, MENU_HELP,
    MENU_COUNT
} menu_id_t;

typedef enum {
    MA_NONE=0, MA_OPEN_FILE, MA_OPEN_DIR, MA_EXIT,
    MA_PLAY_PAUSE, MA_STOP, MA_NEXT, MA_PREV, MA_SHUFFLE, MA_REPEAT, MA_VOL_UP, MA_VOL_DOWN,
    MA_VIEW_BROWSER, MA_VIEW_PLAYLIST, MA_VIEW_NOWPLAY, MA_VIEW_SPECTRUM, MA_VIEW_FULL_SPECTRUM, MA_VIEW_50LINE,
    MA_SETT_AUDIO, MA_SETT_VIDEO, MA_SETT_UNLOCKS, MA_SETT_RECOG, MA_SETT_THEME,
    MA_HELP_ABOUT, MA_HELP_KEYS, MA_HELP_HALL, MA_HELP_VERSION
} menu_action_t;

void menu_bar_render(void);
menu_action_t menu_dispatch(menu_id_t m);    /* returns the chosen action */
menu_action_t menu_from_key(u16 key);        /* keyboard shortcut to action */
const char *menu_action_label(menu_action_t a);

#endif
