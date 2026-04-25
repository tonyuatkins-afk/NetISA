/*
 * ui/menu.c - Menu bar and pull-downs.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * The menu bar is fixed at row 1. Each menu is opened via Alt+letter or by
 * arrow keys when the bar has focus. Drop-down boxes overdraw the four-pane
 * layout temporarily and restore on close.
 */
#include "menu.h"
#include "screen.h"
#include <string.h>

typedef struct {
    const char *label;       /* with hotkey letter capitalised */
    char hot;                /* hotkey ascii */
    menu_action_t action;
} menu_item_t;

typedef struct {
    const char *label;
    char hot;
    const menu_item_t *items;
    u8 item_count;
} menu_def_t;

static const menu_item_t file_items[] = {
    { "Open file...",     'O', MA_OPEN_FILE },
    { "Open directory",   'D', MA_OPEN_DIR },
    { "----",             0,   MA_NONE },
    { "Exit",             'x', MA_EXIT }
};

static const menu_item_t playback_items[] = {
    { "Play / Pause",     'P', MA_PLAY_PAUSE },
    { "Stop",             'S', MA_STOP },
    { "Next track",       'N', MA_NEXT },
    { "Prev track",       'V', MA_PREV },
    { "----",             0,   MA_NONE },
    { "Shuffle",          'h', MA_SHUFFLE },
    { "Repeat",           'R', MA_REPEAT },
    { "----",             0,   MA_NONE },
    { "Volume up",        'U', MA_VOL_UP },
    { "Volume down",      'D', MA_VOL_DOWN }
};

static const menu_item_t view_items[] = {
    { "File browser",     'F', MA_VIEW_BROWSER },
    { "Playlist",         'P', MA_VIEW_PLAYLIST },
    { "Now playing",      'N', MA_VIEW_NOWPLAY },
    { "Spectrum",         'S', MA_VIEW_SPECTRUM },
    { "----",             0,   MA_NONE },
    { "Full spectrum",    'F', MA_VIEW_FULL_SPECTRUM },
    { "----",             0,   MA_NONE },
    { "50-line mode",     '5', MA_VIEW_50LINE }
};

static const menu_item_t settings_items[] = {
    { "Audio device...",  'A', MA_SETT_AUDIO },
    { "Video mode...",    'V', MA_SETT_VIDEO },
    { "Unlock matrix",    'U', MA_SETT_UNLOCKS },
    { "Recognition...",   'R', MA_SETT_RECOG },
    { "----",             0,   MA_NONE },
    { "Theme...",         'T', MA_SETT_THEME }
};

static const menu_item_t help_items[] = {
    { "About HEARO",          'A', MA_HELP_ABOUT },
    { "Keyboard shortcuts",   'K', MA_HELP_KEYS },
    { "Hall of Recognition",  'H', MA_HELP_HALL },
    { "----",                 0,   MA_NONE },
    { "Version info",         'V', MA_HELP_VERSION }
};

static const menu_def_t menus[MENU_COUNT] = {
    { "File",     'F', file_items,     sizeof(file_items) / sizeof(file_items[0]) },
    { "Playback", 'P', playback_items, sizeof(playback_items) / sizeof(playback_items[0]) },
    { "View",     'V', view_items,     sizeof(view_items) / sizeof(view_items[0]) },
    { "Settings", 'S', settings_items, sizeof(settings_items) / sizeof(settings_items[0]) },
    { "Help",     'H', help_items,     sizeof(help_items) / sizeof(help_items[0]) }
};

void menu_bar_render(void)
{
    u8 x = 1;
    u8 i;
    scr_hline(0, 0, scr_cols(), ' ', ATTR_MENU_ITEM);
    for (i = 0; i < MENU_COUNT; i++) {
        scr_putch(x, 0, ' ', ATTR_MENU_ITEM);
        scr_putch((u8)(x + 1), 0, menus[i].label[0], ATTR_MENU_HOT);
        scr_puts((u8)(x + 2), 0, menus[i].label + 1, ATTR_MENU_ITEM);
        scr_putch((u8)(x + 1 + (u8)strlen(menus[i].label)), 0, ' ', ATTR_MENU_ITEM);
        x = (u8)(x + (u8)strlen(menus[i].label) + 3);
    }
}

const char *menu_action_label(menu_action_t a)
{
    u8 m, i;
    for (m = 0; m < MENU_COUNT; m++) {
        for (i = 0; i < menus[m].item_count; i++) {
            if (menus[m].items[i].action == a) return menus[m].items[i].label;
        }
    }
    return "";
}

static u8 menu_x(menu_id_t m)
{
    u8 x = 1, i;
    for (i = 0; i < (u8)m; i++) x = (u8)(x + (u8)strlen(menus[i].label) + 3);
    return x;
}

menu_action_t menu_dispatch(menu_id_t m)
{
    u8 sel = 0;
    u8 mx = menu_x(m);
    u8 my = 1;
    u8 width = 0;
    u8 i;
    const menu_def_t *md;
    if ((u8)m >= MENU_COUNT) return MA_NONE;
    md = &menus[m];
    for (i = 0; i < md->item_count; i++) {
        u8 w = (u8)strlen(md->items[i].label);
        if (w > width) width = w;
    }
    width = (u8)(width + 4);
    if (mx + width > scr_cols()) mx = (u8)(scr_cols() - width - 1);

    /* Highlight bar position for the open menu */
    {
        u8 bx = (u8)(mx - 1);
        scr_putch(bx, 0, ' ', ATTR_SELECTED);
        scr_putch((u8)(bx + 1), 0, md->label[0], ATTR_SELECTED);
        scr_puts((u8)(bx + 2), 0, md->label + 1, ATTR_SELECTED);
        scr_putch((u8)(bx + 1 + (u8)strlen(md->label)), 0, ' ', ATTR_SELECTED);
    }
    scr_box(mx, my, width, (u8)(md->item_count + 2), ATTR_MENU_ITEM);
    while (1) {
        u16 key;
        for (i = 0; i < md->item_count; i++) {
            u8 attr = (i == sel) ? ATTR_SELECTED : ATTR_MENU_ITEM;
            const char *lab = md->items[i].label;
            if (strcmp(lab, "----") == 0) {
                scr_hline((u8)(mx + 1), (u8)(my + 1 + i), (u8)(width - 2),
                          scr_is_mono() ? '-' : (char)0xC4, ATTR_MENU_ITEM);
            } else {
                scr_fill((u8)(mx + 1), (u8)(my + 1 + i), (u8)(width - 2), 1, ' ', attr);
                scr_puts((u8)(mx + 2), (u8)(my + 1 + i), lab, attr);
            }
        }
        key = scr_getkey();
        switch (key) {
            case KEY_ESC: return MA_NONE;
            case KEY_UP:
                do { if (sel == 0) sel = (u8)(md->item_count - 1); else sel--; }
                while (strcmp(md->items[sel].label, "----") == 0);
                break;
            case KEY_DOWN:
                do { sel = (u8)((sel + 1) % md->item_count); }
                while (strcmp(md->items[sel].label, "----") == 0);
                break;
            case KEY_ENTER:
                return md->items[sel].action;
            default:
                /* Hotkey */
                {
                    u8 ascii = (u8)(key & 0xFF);
                    if (ascii) {
                        u8 j;
                        u8 lo = (u8)((ascii >= 'A' && ascii <= 'Z') ? ascii + 32 : ascii);
                        for (j = 0; j < md->item_count; j++) {
                            if (md->items[j].hot != 0) {
                                u8 h = (u8)md->items[j].hot;
                                u8 hl = (u8)((h >= 'A' && h <= 'Z') ? h + 32 : h);
                                if (hl == lo) return md->items[j].action;
                            }
                        }
                    }
                }
                break;
        }
    }
}

menu_action_t menu_from_key(u16 key)
{
    switch (key) {
        case KEY_ALT_F: return (menu_action_t)(MENU_FILE + 1000);     /* sentinel: open File menu */
        case KEY_ALT_P: return (menu_action_t)(MENU_PLAYBACK + 1000);
        case KEY_ALT_V: return (menu_action_t)(MENU_VIEW + 1000);
        case KEY_ALT_S: return (menu_action_t)(MENU_SETTINGS + 1000);
        case KEY_ALT_H: return (menu_action_t)(MENU_HELP + 1000);
        case KEY_ALT_X: return MA_EXIT;
        case KEY_F2:    return MA_SETT_UNLOCKS;
        case KEY_F10:   return MA_HELP_HALL;
        default:        return MA_NONE;
    }
}
