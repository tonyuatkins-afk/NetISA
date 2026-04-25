/*
 * screen.h - Direct VGA text buffer rendering library
 *
 * Shared rendering engine for NETISA.EXE and all NetISA DOS applications.
 * Writes directly to the VGA text buffer at 0xB800:0000.
 * Assumes 80x25 text mode (mode 3).
 */

#ifndef SCREEN_H
#define SCREEN_H

/* Screen dimensions */
#define SCR_WIDTH   80
#define SCR_HEIGHT  25

/* Cell structure: character + attribute byte */
typedef struct {
    unsigned char ch;
    unsigned char attr;
} cell_t;

/* CGA/VGA color constants (foreground, bits 0-3) */
#define SCR_BLACK           0x00
#define SCR_BLUE            0x01
#define SCR_GREEN           0x02
#define SCR_CYAN            0x03
#define SCR_RED             0x04
#define SCR_MAGENTA         0x05
#define SCR_BROWN           0x06
#define SCR_LIGHTGRAY       0x07
#define SCR_DARKGRAY        0x08
#define SCR_LIGHTBLUE       0x09
#define SCR_LIGHTGREEN      0x0A
#define SCR_LIGHTCYAN       0x0B
#define SCR_LIGHTRED        0x0C
#define SCR_LIGHTMAGENTA    0x0D
#define SCR_YELLOW          0x0E
#define SCR_WHITE           0x0F

/* Build attribute byte: fg (0-15), bg (0-7) */
#define SCR_ATTR(fg, bg)    ((unsigned char)(((bg) << 4) | (fg)))

/* NetISA design language attributes */
#define ATTR_NORMAL     SCR_ATTR(SCR_LIGHTGRAY, SCR_BLACK)
#define ATTR_HIGHLIGHT  SCR_ATTR(SCR_WHITE, SCR_BLACK)
#define ATTR_SELECTED   SCR_ATTR(SCR_LIGHTGREEN, SCR_BLACK)
#define ATTR_HEADER     SCR_ATTR(SCR_WHITE, SCR_BLACK)
#define ATTR_BORDER     SCR_ATTR(SCR_GREEN, SCR_BLACK)
#define ATTR_STATUS     SCR_ATTR(SCR_BLACK, SCR_GREEN)
#define ATTR_ERROR      SCR_ATTR(SCR_LIGHTRED, SCR_BLACK)
#define ATTR_INPUT      SCR_ATTR(SCR_YELLOW, SCR_BLACK)
#define ATTR_DIM        SCR_ATTR(SCR_DARKGRAY, SCR_BLACK)

/* Signal strength bar colors (increasing brightness) */
#define ATTR_SIGNAL1    SCR_ATTR(SCR_DARKGRAY, SCR_BLACK)
#define ATTR_SIGNAL2    SCR_ATTR(SCR_GREEN, SCR_BLACK)
#define ATTR_SIGNAL3    SCR_ATTR(SCR_LIGHTGREEN, SCR_BLACK)
#define ATTR_SIGNAL4    SCR_ATTR(SCR_WHITE, SCR_BLACK)

/* CP437 box drawing characters */
#define BOX_TL      0xC9  /* top-left double corner */
#define BOX_TR      0xBB  /* top-right double corner */
#define BOX_BL      0xC8  /* bottom-left double corner */
#define BOX_BR      0xBC  /* bottom-right double corner */
#define BOX_H       0xCD  /* horizontal double line */
#define BOX_V       0xBA  /* vertical double line */
#define BOX_BLOCK   0xDB  /* full block */
#define BOX_SHADE1  0xB0  /* light shade */
#define BOX_SHADE2  0xB1  /* medium shade */
#define BOX_SHADE3  0xB2  /* dark shade */
#define BOX_ARROW   0x10  /* right-pointing triangle */
#define BOX_BULLET  0xFE  /* small square bullet */

/* Keyboard scan codes (high byte from INT 16h) */
#define KEY_UP      0x4800
#define KEY_DOWN    0x5000
#define KEY_LEFT    0x4B00
#define KEY_RIGHT   0x4D00
#define KEY_ENTER   0x000D
#define KEY_ESC     0x001B
#define KEY_TAB     0x0F09
#define KEY_BKSP    0x0E08

/* Function prototypes */
void scr_init(void);
void scr_shutdown(void);
void scr_clear(unsigned char attr);
void scr_putc(int x, int y, char ch, unsigned char attr);
void scr_puts(int x, int y, const char *str, unsigned char attr);
void scr_putsn(int x, int y, const char *str, int maxlen, unsigned char attr);
void scr_hline(int x, int y, int len, char ch, unsigned char attr);
void scr_vline(int x, int y, int len, char ch, unsigned char attr);
void scr_box(int x, int y, int w, int h, unsigned char attr);
void scr_fill(int x, int y, int w, int h, char ch, unsigned char attr);
void scr_signal_bars(int x, int y, int strength_pct);
int  scr_getkey(void);
int  scr_kbhit(void);
void scr_cursor_hide(void);
void scr_cursor_show(void);
void scr_cursor_pos(int x, int y);
void scr_delay(int ms);
void scr_fade_in(int steps, int step_delay_ms);
void scr_fade_out(int steps, int step_delay_ms);

/* Attribute read/write for mouse cursor support */
unsigned char scr_getattr(int x, int y);
void scr_setattr(int x, int y, unsigned char attr);

#endif /* SCREEN_H */
