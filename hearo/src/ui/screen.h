/*
 * ui/screen.h - Low-level text-mode rendering.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_UI_SCREEN_H
#define HEARO_UI_SCREEN_H

#include "../hearo.h"

void scr_init(void);
void scr_clear(u8 attr);
void scr_putch(u8 x, u8 y, char ch, u8 attr);
void scr_puts(u8 x, u8 y, const char *s, u8 attr);
void scr_hline(u8 x, u8 y, u8 len, char ch, u8 attr);
void scr_vline(u8 x, u8 y, u8 len, char ch, u8 attr);
void scr_box(u8 x, u8 y, u8 w, u8 h, u8 attr);
void scr_fill(u8 x, u8 y, u8 w, u8 h, char ch, u8 attr);
void scr_cursor(hbool visible);

u16   scr_getkey(void);          /* INT 16h AH=00h, returns scan<<8|ascii */
hbool scr_keypending(void);      /* INT 16h AH=01h */

u8    scr_cols(void);
u8    scr_rows(void);
hbool scr_is_mono(void);

/* Translate an attribute to its mono equivalent when running on MDA/Hercules. */
u8    scr_attr(u8 colour_attr);

#endif
