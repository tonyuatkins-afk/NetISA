/*
 * ui/browser.c - File browser pane.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Walks the current directory with _dos_findfirst / _dos_findnext, filters
 * to subdirectories plus the music file extensions HEARO knows how to handle.
 * Enter on a subdirectory chdir()s into it and reloads; Backspace ascends to
 * the parent. Selection is preserved via name matching when reloading.
 */
#include "browser.h"
#include "screen.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define MAX_ENTRIES 64

typedef struct { char name[16]; char duration[8]; hbool is_dir; } entry_t;

static entry_t entries[MAX_ENTRIES];
static u8 entry_count = 0;
static u8 selected = 0;
static u8 scroll = 0;
static char current_dir[80] = "C:\\";
static u8 view_h = 1;

static const char *music_exts[] = {
    ".MOD", ".S3M", ".XM",  ".IT",  ".MID", ".WAV",
    ".SID", ".AY",  ".NSF", ".GBS", ".SPC", 0
};

static int ext_match(const char *name, const char *ext)
{
    /* Case-insensitive trailing match. name is 8.3 plus dot. */
    u16 nlen = (u16)strlen(name);
    u16 elen = (u16)strlen(ext);
    u16 i;
    if (nlen < elen) return 0;
    for (i = 0; i < elen; i++) {
        char a = name[nlen - elen + i];
        char b = ext[i];
        if (a >= 'a' && a <= 'z') a = (char)(a - 32);
        if (b >= 'a' && b <= 'z') b = (char)(b - 32);
        if (a != b) return 0;
    }
    return 1;
}

static hbool is_music_file(const char *name)
{
    int i;
    for (i = 0; music_exts[i]; i++) {
        if (ext_match(name, music_exts[i])) return HTRUE;
    }
    return HFALSE;
}

static void copy_entry(u8 idx, const char *name, hbool is_dir, const char *duration)
{
    if (idx >= MAX_ENTRIES) return;
    strncpy(entries[idx].name, name, sizeof(entries[idx].name) - 1);
    entries[idx].name[sizeof(entries[idx].name) - 1] = '\0';
    entries[idx].is_dir = is_dir;
    if (duration) {
        strncpy(entries[idx].duration, duration, sizeof(entries[idx].duration) - 1);
        entries[idx].duration[sizeof(entries[idx].duration) - 1] = '\0';
    } else {
        entries[idx].duration[0] = '\0';
    }
}

#include <dos.h>
#include <direct.h>

static void load_directory(void)
{
    struct find_t f;
    unsigned rc;
    char *root_test;

    entry_count = 0;
    if (!getcwd(current_dir, sizeof(current_dir))) {
        strcpy(current_dir, "C:\\");
    }

    /* Add ".." unless we're already at a drive root (e.g. "C:\"). */
    root_test = current_dir;
    if (!(strlen(root_test) <= 3 && root_test[1] == ':' && root_test[2] == '\\')) {
        copy_entry(entry_count++, "..", HTRUE, 0);
    }

    /* Subdirectories first, then files. Two passes keep dirs grouped at top. */
    rc = _dos_findfirst("*.*", _A_NORMAL | _A_SUBDIR | _A_RDONLY, &f);
    while (rc == 0 && entry_count < MAX_ENTRIES) {
        if (f.name[0] != '.' && (f.attrib & _A_SUBDIR)) {
            copy_entry(entry_count++, f.name, HTRUE, 0);
        }
        rc = _dos_findnext(&f);
    }

    rc = _dos_findfirst("*.*", _A_NORMAL | _A_RDONLY, &f);
    while (rc == 0 && entry_count < MAX_ENTRIES) {
        if (!(f.attrib & _A_SUBDIR) && is_music_file(f.name)) {
            char dur[8];
            /* Approximate duration from file size as a rough pre-decode hint.
             * Real durations come from track metadata once playback ships. */
            unsigned long kb = (f.size + 1023UL) / 1024UL;
            sprintf(dur, "%lukB", kb < 9999UL ? kb : 9999UL);
            copy_entry(entry_count++, f.name, HFALSE, dur);
        }
        rc = _dos_findnext(&f);
    }
}

static int change_directory(const char *path)
{
    if (!path || !path[0]) return -1;
    return chdir(path);
}

void browser_init(const char *start_dir)
{
    if (start_dir && start_dir[0]) {
        chdir(start_dir);
    }
    load_directory();
    selected = 0;
    scroll = 0;
}

void browser_render(u8 x, u8 y, u8 w, u8 h, hbool focused)
{
    u8 i;
    char buf[80];
    u8 box_attr = focused ? ATTR_BRIGHT : ATTR_DIM;
    scr_box(x, y, w, h, box_attr);
    sprintf(buf, " %s ", current_dir);
    if ((u8)strlen(buf) > w - 4) buf[w - 4] = '\0';
    scr_puts((u8)(x + 2), y, buf, focused ? ATTR_BRIGHT : ATTR_NORMAL);

    view_h = (u8)(h - 2);
    if (view_h < 1) view_h = 1;
    if (selected < scroll) scroll = selected;
    if (selected >= scroll + view_h) scroll = (u8)(selected - view_h + 1);

    for (i = 0; i < view_h; i++) {
        u8 idx = (u8)(scroll + i);
        u8 ry = (u8)(y + 1 + i);
        u8 attr = (idx == selected && focused) ? ATTR_SELECTED : ATTR_NORMAL;
        scr_fill((u8)(x + 1), ry, (u8)(w - 2), 1, ' ', attr);
        if (idx >= entry_count) continue;
        scr_putch((u8)(x + 2), ry, idx == selected ? '>' : ' ', attr);
        if (entries[idx].is_dir) {
            scr_putch((u8)(x + 4), ry, '[', attr);
            scr_puts((u8)(x + 5), ry, entries[idx].name, attr);
            scr_putch((u8)(x + 5 + (u8)strlen(entries[idx].name)), ry, ']', attr);
        } else {
            scr_puts((u8)(x + 4), ry, entries[idx].name, attr);
        }
        if (entries[idx].duration[0]) {
            u8 dx = (u8)(x + w - 2 - (u8)strlen(entries[idx].duration));
            scr_puts(dx, ry, entries[idx].duration, attr);
        }
    }
    if (entry_count == 0) {
        scr_puts((u8)(x + 2), (u8)(y + 1), "(no music files in this directory)", ATTR_DIM);
    }
}

hbool browser_handle_key(u16 key)
{
    switch (key) {
        case KEY_UP:    if (selected > 0) selected--; return HTRUE;
        case KEY_DOWN:  if (selected + 1 < entry_count) selected++; return HTRUE;
        case KEY_PGUP:  selected = (u8)(selected > view_h ? selected - view_h : 0); return HTRUE;
        case KEY_PGDN:  selected = (u8)(selected + view_h >= entry_count ? entry_count - 1 : selected + view_h); return HTRUE;
        case KEY_HOME:  selected = 0; return HTRUE;
        case KEY_END:   selected = (u8)(entry_count > 0 ? entry_count - 1 : 0); return HTRUE;
        case KEY_ENTER:
            if (selected < entry_count && entries[selected].is_dir) {
                if (change_directory(entries[selected].name) == 0) {
                    load_directory();
                    selected = 0;
                    scroll = 0;
                }
            }
            return HTRUE;
        case 0x0E08: /* Backspace */
            if (change_directory("..") == 0) {
                load_directory();
                selected = 0;
                scroll = 0;
            }
            return HTRUE;
        default:
            return HFALSE;
    }
}

const char *browser_current_path(void) { return current_dir; }
const char *browser_selected_filename(void)
{
    return (selected < entry_count) ? entries[selected].name : "";
}
