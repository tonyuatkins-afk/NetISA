/*
 * splash.c - Animated splash screen for Claude for DOS
 *
 * Fake neural POST sequence, CRT static burst, block-art CLAUDE logo
 * with column-sweep reveal, and a gloriously nerdy tagline.
 * Press any key at any time to skip straight to chat.
 */

#include "claude.h"
#include <string.h>
#include <dos.h>

/* ── Simple PRNG for CRT static effect ─────────────────────── */

static unsigned int rng_state;

static unsigned int fast_rand(void)
{
    rng_state = rng_state * 25173u + 13849u;
    return rng_state;
}

static void seed_rng(void)
{
    unsigned long t = 0;
    _asm {
        xor ax, ax
        int 1Ah
        mov word ptr t, dx
        mov word ptr t+2, cx
    }
    rng_state = (unsigned int)(t & 0xFFFFu);
    if (rng_state == 0) rng_state = 54321u;
}

/* ── Timing ────────────────────────────────────────────────── */

/* Wait N BIOS ticks (~55ms each). Returns 1 if key pressed (skip). */
static int wait(unsigned int ticks)
{
    unsigned long start = 0, now = 0;
    _asm {
        xor ax, ax
        int 1Ah
        mov word ptr start, dx
        mov word ptr start+2, cx
    }
    for (;;) {
        if (scr_kbhit()) { scr_getkey(); return 1; }
        _asm {
            xor ax, ax
            int 1Ah
            mov word ptr now, dx
            mov word ptr now+2, cx
        }
        if ((now - start) >= (unsigned long)ticks) break;
        _asm { int 28h }
    }
    return 0;
}

/* ���─ Color palette ─────────────────────────────────────────── */

#define A_BIOS    SCR_ATTR(SCR_WHITE, SCR_BLACK)
#define A_COPY    SCR_ATTR(SCR_DARKGRAY, SCR_BLACK)
#define A_LABEL   SCR_ATTR(SCR_GREEN, SCR_BLACK)
#define A_DOTS    SCR_ATTR(SCR_DARKGRAY, SCR_BLACK)
#define A_OK      SCR_ATTR(SCR_LIGHTGREEN, SCR_BLACK)
#define A_FAIL    SCR_ATTR(SCR_LIGHTRED, SCR_BLACK)
#define A_VALUE   SCR_ATTR(SCR_LIGHTGRAY, SCR_BLACK)
#define A_WARN    SCR_ATTR(SCR_YELLOW, SCR_BLACK)
#define A_LOGO_DK SCR_ATTR(SCR_GREEN, SCR_BLACK)
#define A_LOGO    SCR_ATTR(SCR_LIGHTGREEN, SCR_BLACK)
#define A_FLASH   SCR_ATTR(SCR_WHITE, SCR_BLACK)
#define A_SUB     SCR_ATTR(SCR_DARKGRAY, SCR_BLACK)
#define A_TAG     SCR_ATTR(SCR_LIGHTCYAN, SCR_BLACK)
#define A_VER     SCR_ATTR(SCR_DARKGRAY, SCR_BLACK)
#define A_KEY     SCR_ATTR(SCR_YELLOW, SCR_BLACK)

/* ── Boot sequence ─────────────────────────────────────────── */

/* Print label + animated dots + result on one line */
static int post_line(int y, const char *label, const char *result,
                     unsigned char rattr)
{
    int x, i, dot_count;

    scr_puts(1, y, label, A_LABEL);
    x = 1 + (int)strlen(label);

    /* Dots from current position to column 40 */
    dot_count = 40 - x;
    if (dot_count < 3) dot_count = 3;
    for (i = 0; i < dot_count; i++) {
        scr_putc(x + i, y, '.', A_DOTS);
    }
    x += dot_count + 1;

    if (wait(2)) return 1;

    if (result)
        scr_puts(x, y, result, rattr);

    return 0;
}

static int run_boot_sequence(void)
{
    int y = 0;

    scr_clear(SCR_ATTR(SCR_BLACK, SCR_BLACK));

    /* BIOS title */
    scr_puts(1, y, "ANTHROPIC NEURAL BIOS v1.0", A_BIOS);
    y++;
    scr_puts(1, y, "(C) 2026 Anthropic, PBC. All rights reserved.", A_COPY);
    y += 2;

    if (wait(5)) return 1;

    /* POST lines */
    if (post_line(y++, "Detecting ISA bus",
                  "8/16-bit [OK]", A_OK)) return 1;
    if (post_line(y++, "Neural coprocessor",
                  "ESP32-S3 @ 240 MHz", A_VALUE)) return 1;
    if (post_line(y++, "Conventional memory",
                  "640K (should be enough)", A_VALUE)) return 1;
    if (post_line(y++, "Extended intelligence",
                  "Anthropic Claude", A_VALUE)) return 1;
    if (post_line(y++, "TLS 1.3 cipher suite",
                  "AES-256-GCM [OK]", A_OK)) return 1;
    if (post_line(y++, "Attention heads",
                  "all present and accounted for", A_VALUE)) return 1;

    /* The gag: briefly flash "FAI" in red before correcting to PASSED */
    {
        int lx, dx;
        scr_puts(1, y, "Constitutional AI check", A_LABEL);
        lx = 24;
        for (dx = 0; dx < 16; dx++)
            scr_putc(lx + dx, y, '.', A_DOTS);
        if (wait(2)) return 1;
        scr_puts(42, y, "FAI", A_FAIL);
        if (wait(4)) return 1;
        scr_puts(42, y, "PASSED   ", A_OK);
        y++;
    }

    if (post_line(y++, "Tokenizer",
                  "100,000 tokens loaded", A_VALUE)) return 1;

    y++;
    scr_puts(1, y, "Initializing neural interface", A_LABEL);
    {
        int bx = 30;
        int i;
        for (i = 0; i < 8; i++) {
            scr_putc(bx + i, y, '.', A_LABEL);
            if (wait(3)) return 1;
        }
    }

    if (wait(3)) return 1;
    return 0;
}

/* ── CRT static burst ──────────────────────────────────────── */

static int crt_static(void)
{
    int x, y, frame;

    for (frame = 0; frame < 3; frame++) {
        for (y = 0; y < 25; y++) {
            for (x = 0; x < 80; x++) {
                unsigned int r = fast_rand();
                scr_putc(x, y, (char)(r % 94 + 33),
                         (unsigned char)((r >> 8) & 0x0F));
            }
        }
        if (wait(2)) return 1;
    }

    /* Brief black pause (CRT warming up) */
    scr_clear(SCR_ATTR(SCR_BLACK, SCR_BLACK));
    if (wait(6)) return 1;

    return 0;
}

/* ── Block-art logo: CLAUDE ────────────────────────────────── */

/*
 * 5x7 pixel font for each letter. Each byte has 5 significant bits
 * (bit 4 = leftmost pixel, bit 0 = rightmost). Each pixel renders
 * as 2 full-block characters on screen (10 columns per letter).
 */

#define GLYPH_W     5
#define GLYPH_H     7
#define NUM_GLYPHS  6
#define CELL_W      2
#define LETTER_SCR  (GLYPH_W * CELL_W)          /* 10 screen cols/letter */
#define GAP_SCR     2                            /* gap between letters */
#define LOGO_TOT_W  (NUM_GLYPHS * LETTER_SCR + (NUM_GLYPHS - 1) * GAP_SCR)
#define LOGO_X      ((80 - LOGO_TOT_W) / 2)     /* 5 */
#define LOGO_Y      6

/* Pixel columns (including 1-px gaps between glyphs) */
#define TOTAL_PCOLS (NUM_GLYPHS * GLYPH_W + (NUM_GLYPHS - 1))  /* 35 */

static const unsigned char glyphs[NUM_GLYPHS][GLYPH_H] = {
    /* C */ { 0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0F },
    /* L */ { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F },
    /* A */ { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },
    /* U */ { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },
    /* D */ { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E },
    /* E */ { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }
};

/* Draw the entire logo with a given character and attribute */
static void draw_logo_full(char ch, unsigned char attr)
{
    int g, row, col, sx;

    for (g = 0; g < NUM_GLYPHS; g++) {
        int base_x = LOGO_X + g * (LETTER_SCR + GAP_SCR);
        for (row = 0; row < GLYPH_H; row++) {
            unsigned char bits = glyphs[g][row];
            for (col = 0; col < GLYPH_W; col++) {
                if (bits & (0x10 >> col)) {
                    sx = base_x + col * CELL_W;
                    scr_putc(sx,     LOGO_Y + row, ch, attr);
                    scr_putc(sx + 1, LOGO_Y + row, ch, attr);
                }
            }
        }
    }
}

/* Draw a single pixel-column of the logo (for sweep animation).
 * pcol ranges 0..TOTAL_PCOLS-1, mapping across all glyphs+gaps. */
static void draw_logo_pcol(int pcol, char ch, unsigned char attr)
{
    int g, accum, cig, base_x, sx, row;

    accum = 0;
    for (g = 0; g < NUM_GLYPHS; g++) {
        if (pcol >= accum && pcol < accum + GLYPH_W) {
            cig = pcol - accum;   /* column-in-glyph */
            base_x = LOGO_X + g * (LETTER_SCR + GAP_SCR);
            sx = base_x + cig * CELL_W;
            for (row = 0; row < GLYPH_H; row++) {
                if (glyphs[g][row] & (0x10 >> cig)) {
                    scr_putc(sx,     LOGO_Y + row, ch, attr);
                    scr_putc(sx + 1, LOGO_Y + row, ch, attr);
                }
            }
            return;
        }
        accum += GLYPH_W;
        if (g < NUM_GLYPHS - 1) accum++;  /* 1-pixel gap */
    }
}

static int animate_logo(void)
{
    int pcol;

    /* Column-by-column sweep in dark green */
    for (pcol = 0; pcol < TOTAL_PCOLS; pcol++) {
        draw_logo_pcol(pcol, (char)0xDB, A_LOGO_DK);
        if (wait(1)) return 1;
    }

    /* Flash to white */
    draw_logo_full((char)0xDB, A_FLASH);
    if (wait(3)) return 1;

    /* Settle to bright green */
    draw_logo_full((char)0xDB, A_LOGO);

    return 0;
}

/* ── Text elements below the logo ──────────────────────────── */

static void center_text(int y, const char *text, unsigned char attr)
{
    int len = (int)strlen(text);
    int x = (80 - len) / 2;
    if (x < 0) x = 0;
    scr_puts(x, y, text, attr);
}

/* Type text centered with cadence — pauses at punctuation */
static int type_centered(int y, const char *text, unsigned char attr)
{
    int len = (int)strlen(text);
    int x = (80 - len) / 2;
    int i, tick;

    if (x < 0) x = 0;
    tick = 0;

    for (i = 0; text[i]; i++) {
        scr_putc(x + i, y, text[i], attr);
        tick++;

        /* Pause at sentence-ending punctuation */
        if (text[i] == '.' && text[i + 1] == ' ') {
            if (wait(5)) return 1;
            tick = 0;
        } else if (tick >= 3) {
            if (wait(1)) return 1;
            tick = 0;
        }
    }
    return 0;
}

static int show_text(void)
{
    int vis, blinks;

    /* Subtitle: decorative line under logo */
    if (wait(3)) return 1;
    center_text(14, "\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4 for DOS \xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4", A_SUB);
    if (wait(8)) return 1;

    /* The tagline: typed out with dramatic pauses at periods */
    if (type_centered(17,
        "640K of RAM.  Billions of parameters.  One ISA slot.",
        A_TAG)) return 1;
    if (wait(8)) return 1;

    /* Version string */
    center_text(20, "CLAUDE.EXE v0.1  \xB3  NetISA", A_VER);
    if (wait(5)) return 1;

    /* Blinking "Press any key" */
    vis = 1;
    for (blinks = 0; blinks < 20; blinks++) {
        if (vis)
            center_text(22, "Press any key to begin...", A_KEY);
        else
            scr_fill(27, 22, 26, 1, ' ', SCR_ATTR(SCR_BLACK, SCR_BLACK));
        vis = !vis;
        if (wait(9)) return 1;  /* ~0.5s per blink */
    }

    return 0;
}

/* ── Public entry point ────────────────────────────────────── */

void cl_splash(void)
{
    /* Drain leftover keypresses */
    while (scr_kbhit()) scr_getkey();

    seed_rng();
    scr_cursor_hide();

    /* Phase 1: Neural POST sequence */
    if (run_boot_sequence()) goto done;

    /* Phase 2: CRT static burst */
    if (crt_static()) goto done;

    /* Phase 3: Logo materializes */
    if (animate_logo()) goto done;

    /* Phase 4: Subtitle, tagline, press-any-key */
    if (show_text()) goto done;

done:
    while (scr_kbhit()) scr_getkey();
    scr_clear(SCR_ATTR(SCR_BLACK, SCR_BLACK));
}
