/*
 * screen.c - Direct VGA text buffer rendering library
 *
 * All writes go to the far pointer 0xB800:0000 (VGA text buffer).
 * 80x25 text mode (mode 3) assumed.
 */

#include "screen.h"
#include <i86.h>
#include <conio.h>
#include <string.h>

/* VGA text buffer: segment 0xB800, offset 0x0000 */
#define VGA_SEG     0xB800
#define VGA_BASE    ((cell_t far *)0xB8000000L)

/* Saved video state for shutdown restore */
static unsigned char saved_mode = 0x03;
static unsigned short saved_cursor = 0;

static cell_t far *scr_cell(int x, int y)
{
    return VGA_BASE + y * SCR_WIDTH + x;
}

void scr_init(void)
{
    union REGS r;

    /* Save current video mode */
    r.h.ah = 0x0F;
    int86(0x10, &r, &r);
    saved_mode = r.h.al;

    /* Save cursor shape */
    r.h.ah = 0x03;
    r.h.bh = 0;
    int86(0x10, &r, &r);
    saved_cursor = r.w.cx;

    /* Set 80x25 text mode (mode 3) to ensure clean state */
    r.h.ah = 0x00;
    r.h.al = 0x03;
    int86(0x10, &r, &r);

    scr_cursor_hide();
    scr_clear(ATTR_NORMAL);
}

void scr_shutdown(void)
{
    union REGS r;

    /* Restore original video mode */
    r.h.ah = 0x00;
    r.h.al = saved_mode;
    int86(0x10, &r, &r);

    /* Restore cursor shape */
    r.h.ah = 0x01;
    r.w.cx = saved_cursor;
    int86(0x10, &r, &r);
}

void scr_clear(unsigned char attr)
{
    scr_fill(0, 0, SCR_WIDTH, SCR_HEIGHT, ' ', attr);
}

void scr_putc(int x, int y, char ch, unsigned char attr)
{
    cell_t far *p;
    if (x < 0 || x >= SCR_WIDTH || y < 0 || y >= SCR_HEIGHT)
        return;
    p = scr_cell(x, y);
    p->ch = (unsigned char)ch;
    p->attr = attr;
}

void scr_puts(int x, int y, const char *str, unsigned char attr)
{
    while (*str && x < SCR_WIDTH) {
        scr_putc(x, y, *str, attr);
        str++;
        x++;
    }
}

void scr_putsn(int x, int y, const char *str, int maxlen, unsigned char attr)
{
    int i = 0;
    while (*str && x < SCR_WIDTH && i < maxlen) {
        scr_putc(x, y, *str, attr);
        str++;
        x++;
        i++;
    }
}

void scr_hline(int x, int y, int len, char ch, unsigned char attr)
{
    int i;
    for (i = 0; i < len; i++)
        scr_putc(x + i, y, ch, attr);
}

void scr_vline(int x, int y, int len, char ch, unsigned char attr)
{
    int i;
    for (i = 0; i < len; i++)
        scr_putc(x, y + i, ch, attr);
}

void scr_box(int x, int y, int w, int h, unsigned char attr)
{
    int i;

    /* Corners */
    scr_putc(x, y, (char)BOX_TL, attr);
    scr_putc(x + w - 1, y, (char)BOX_TR, attr);
    scr_putc(x, y + h - 1, (char)BOX_BL, attr);
    scr_putc(x + w - 1, y + h - 1, (char)BOX_BR, attr);

    /* Top and bottom edges */
    for (i = 1; i < w - 1; i++) {
        scr_putc(x + i, y, (char)BOX_H, attr);
        scr_putc(x + i, y + h - 1, (char)BOX_H, attr);
    }

    /* Left and right edges */
    for (i = 1; i < h - 1; i++) {
        scr_putc(x, y + i, (char)BOX_V, attr);
        scr_putc(x + w - 1, y + i, (char)BOX_V, attr);
    }
}

void scr_fill(int x, int y, int w, int h, char ch, unsigned char attr)
{
    int row, col;
    for (row = 0; row < h; row++)
        for (col = 0; col < w; col++)
            scr_putc(x + col, y + row, ch, attr);
}

void scr_signal_bars(int x, int y, int strength_pct)
{
    /* 4-bar signal indicator using block characters */
    unsigned char bars[4];
    unsigned char attrs[4];
    int i;

    /* Determine which bars to fill */
    for (i = 0; i < 4; i++) {
        int threshold = (i + 1) * 25;
        if (strength_pct >= threshold) {
            bars[i] = (unsigned char)BOX_BLOCK;
            if (i == 0)      attrs[i] = ATTR_SIGNAL2;
            else if (i == 1) attrs[i] = ATTR_SIGNAL2;
            else if (i == 2) attrs[i] = ATTR_SIGNAL3;
            else             attrs[i] = ATTR_SIGNAL4;
        } else {
            bars[i] = (unsigned char)BOX_SHADE1;
            attrs[i] = ATTR_SIGNAL1;
        }
    }

    for (i = 0; i < 4; i++)
        scr_putc(x + i, y, (char)bars[i], attrs[i]);
}

int scr_getkey(void)
{
    union REGS r;
    r.h.ah = 0x00;
    int86(0x16, &r, &r);
    /* Return scancode in high byte, ASCII in low byte */
    return r.w.ax;
}

int scr_kbhit(void)
{
    unsigned char result = 0;
    /* INT 16h AH=01h: ZF=1 if no key, ZF=0 if key ready.
     * OpenWatcom's int86 cflag only captures CF, not ZF.
     * Use inline asm to check ZF directly. */
    _asm {
        mov ah, 01h
        int 16h
        jnz _has_key
        mov result, 0
        jmp _done
    _has_key:
        mov result, 1
    _done:
    }
    return result;
}

void scr_cursor_hide(void)
{
    union REGS r;
    r.h.ah = 0x01;
    r.w.cx = 0x2000;  /* start=32, end=0: invisible */
    int86(0x10, &r, &r);
}

void scr_cursor_show(void)
{
    union REGS r;
    r.h.ah = 0x01;
    r.w.cx = 0x0607;  /* normal underline cursor */
    int86(0x10, &r, &r);
}

void scr_cursor_pos(int x, int y)
{
    union REGS r;
    r.h.ah = 0x02;
    r.h.bh = 0;
    r.h.dh = (unsigned char)y;
    r.h.dl = (unsigned char)x;
    int86(0x10, &r, &r);
}

void scr_delay(int ms)
{
    unsigned long far *tick = (unsigned long far *)MK_FP(0x0040, 0x006C);
    unsigned long ticks, start;
    if (ms <= 0) return;
    ticks = (unsigned long)ms * 182UL / 10000UL;
    if (ticks == 0) ticks = 1;
    start = *tick;
    while ((*tick - start) < ticks) { /* spin */ }
}

/* VGA DAC palette fade - static buffer shared by fade_in/fade_out */
static unsigned char fade_pal[768];

static void scr_wait_vsync(void)
{
    while (inp(0x3DA) & 0x08) { }
    while (!(inp(0x3DA) & 0x08)) { }
}

void scr_fade_in(int steps, int step_delay_ms)
{
    int s, i;

    /* Read current palette (the target we're fading to) */
    outp(0x3C7, 0);
    for (i = 0; i < 768; i++)
        fade_pal[i] = (unsigned char)inp(0x3C9);

    /* Set all black */
    outp(0x3C8, 0);
    for (i = 0; i < 768; i++)
        outp(0x3C9, 0);

    /* Gradually restore */
    for (s = 1; s <= steps; s++) {
        scr_wait_vsync();
        outp(0x3C8, 0);
        for (i = 0; i < 768; i++) {
            unsigned char v = (unsigned char)(
                (unsigned int)fade_pal[i] * (unsigned int)s
                / (unsigned int)steps);
            outp(0x3C9, v);
        }
        scr_delay(step_delay_ms);
    }
}

void scr_fade_out(int steps, int step_delay_ms)
{
    int s, i;

    /* Read current palette */
    outp(0x3C7, 0);
    for (i = 0; i < 768; i++)
        fade_pal[i] = (unsigned char)inp(0x3C9);

    /* Gradually dim to black */
    for (s = steps - 1; s >= 0; s--) {
        scr_wait_vsync();
        outp(0x3C8, 0);
        for (i = 0; i < 768; i++) {
            unsigned char v = (unsigned char)(
                (unsigned int)fade_pal[i] * (unsigned int)s
                / (unsigned int)steps);
            outp(0x3C9, v);
        }
        scr_delay(step_delay_ms);
    }
}
