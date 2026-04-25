/*
 * ui/screen.c - Low-level text-mode rendering.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Direct video memory writes to B800:0000 (color) or B000:0000 (MDA).
 * Watcom-only; uses __far / MK_FP / int86 from <i86.h> and <conio.h>.
 */
#include "screen.h"
#include <string.h>
#include <conio.h>
#include <i86.h>

static u8 cols = 80;
static u8 rows = 25;
static hbool is_mono = HFALSE;
static u8 __far *vram = 0;

void scr_init(void)
{
    /* Probe BIOS data area at 40h:49h for current video mode. */
    u8 mode = *(u8 __far *)MK_FP(0x0040, 0x0049);
    if (mode == 0x07) {
        is_mono = HTRUE;
        vram = (u8 __far *)MK_FP(0xB000, 0x0000);
    } else {
        is_mono = HFALSE;
        vram = (u8 __far *)MK_FP(0xB800, 0x0000);
    }
    cols = *(u8 __far *)MK_FP(0x0040, 0x004A);
    if (cols == 0) cols = 80;
    rows = (u8)(*(u8 __far *)MK_FP(0x0040, 0x0084) + 1);
    if (rows < 25) rows = 25;
}

u8 scr_cols(void) { return cols; }
u8 scr_rows(void) { return rows; }
hbool scr_is_mono(void) { return is_mono; }

u8 scr_attr(u8 colour_attr)
{
    if (!is_mono) return colour_attr;
    /* Bright means high intensity. Normal background, white foreground. */
    if (colour_attr == 0x00) return 0x00;
    if ((colour_attr & 0x80) || colour_attr == ATTR_TITLE_BAR) return 0x70; /* reverse */
    if ((colour_attr & 0x08) || colour_attr == ATTR_BRIGHT)    return 0x0F; /* bright */
    if (colour_attr == ATTR_DIM || colour_attr == ATTR_LOCKED) return 0x07;
    return 0x07;
}

void scr_clear(u8 attr)
{
    u16 i;
    u16 cell = ((u16)scr_attr(attr) << 8) | 0x20;
    u16 n = (u16)cols * (u16)rows;
    for (i = 0; i < n; i++) {
        vram[i * 2]     = (u8)(cell & 0xFF);
        vram[i * 2 + 1] = (u8)(cell >> 8);
    }
}

void scr_putch(u8 x, u8 y, char ch, u8 attr)
{
    u16 off;
    if (x >= cols || y >= rows) return;
    off = ((u16)y * (u16)cols + (u16)x) * 2;
    vram[off]     = (u8)ch;
    vram[off + 1] = scr_attr(attr);
}

void scr_puts(u8 x, u8 y, const char *s, u8 attr)
{
    u8 cx = x;
    if (!s) return;
    while (*s && cx < cols) {
        scr_putch(cx++, y, *s++, attr);
    }
}

void scr_hline(u8 x, u8 y, u8 len, char ch, u8 attr)
{
    u8 i;
    for (i = 0; i < len && (x + i) < cols; i++) scr_putch((u8)(x + i), y, ch, attr);
}

void scr_vline(u8 x, u8 y, u8 len, char ch, u8 attr)
{
    u8 i;
    for (i = 0; i < len && (y + i) < rows; i++) scr_putch(x, (u8)(y + i), ch, attr);
}

void scr_fill(u8 x, u8 y, u8 w, u8 h, char ch, u8 attr)
{
    u8 i;
    for (i = 0; i < h; i++) scr_hline(x, (u8)(y + i), w, ch, attr);
}

void scr_box(u8 x, u8 y, u8 w, u8 h, u8 attr)
{
    if (w < 2 || h < 2) return;
    if (is_mono) {
        scr_putch(x, y, '+', attr);
        scr_putch((u8)(x + w - 1), y, '+', attr);
        scr_putch(x, (u8)(y + h - 1), '+', attr);
        scr_putch((u8)(x + w - 1), (u8)(y + h - 1), '+', attr);
        scr_hline((u8)(x + 1), y, (u8)(w - 2), '-', attr);
        scr_hline((u8)(x + 1), (u8)(y + h - 1), (u8)(w - 2), '-', attr);
        scr_vline(x, (u8)(y + 1), (u8)(h - 2), '|', attr);
        scr_vline((u8)(x + w - 1), (u8)(y + 1), (u8)(h - 2), '|', attr);
    } else {
        scr_putch(x, y, (char)0xDA, attr);
        scr_putch((u8)(x + w - 1), y, (char)0xBF, attr);
        scr_putch(x, (u8)(y + h - 1), (char)0xC0, attr);
        scr_putch((u8)(x + w - 1), (u8)(y + h - 1), (char)0xD9, attr);
        scr_hline((u8)(x + 1), y, (u8)(w - 2), (char)0xC4, attr);
        scr_hline((u8)(x + 1), (u8)(y + h - 1), (u8)(w - 2), (char)0xC4, attr);
        scr_vline(x, (u8)(y + 1), (u8)(h - 2), (char)0xB3, attr);
        scr_vline((u8)(x + w - 1), (u8)(y + 1), (u8)(h - 2), (char)0xB3, attr);
    }
}

void scr_cursor(hbool visible)
{
    union REGS r;
    r.h.ah = 0x01;
    if (visible) { r.h.ch = 6; r.h.cl = 7; }
    else         { r.h.ch = 0x20; r.h.cl = 0; }
    int86(0x10, &r, &r);
}

u16 scr_getkey(void)
{
    union REGS r;
    r.h.ah = 0x00;
    int86(0x16, &r, &r);
    return r.x.ax;
}

hbool scr_keypending(void)
{
    union REGS r;
    r.h.ah = 0x01;
    int86(0x16, &r, &r);
    return ((r.x.cflag & 0x40) == 0) ? HTRUE : HFALSE;
}
