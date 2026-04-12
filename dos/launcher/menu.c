/*
 * menu.c - Main menu logic for NETISA.EXE launcher
 */

#include "screen.h"
#include "netisa.h"
#include "menu.h"
#include <stdlib.h>

/* Forward declarations for panels */
extern void panel_wifi(void);
extern void panel_status(void);

#define MENU_ITEMS  4

static const char *menu_labels[MENU_ITEMS] = {
    "WiFi Setup",
    "Card Status",
    "Claude",
    "About"
};

static void draw_frame(void)
{
    /* Clear screen */
    scr_clear(ATTR_NORMAL);

    /* Outer border */
    scr_box(0, 0, 80, 25, ATTR_BORDER);

    /* Title bar */
    scr_fill(1, 1, 78, 1, ' ', ATTR_STATUS);
    scr_puts(2, 1, " NetISA Control Panel ", ATTR_STATUS);

    /* Status bar */
    scr_fill(1, 23, 78, 1, ' ', ATTR_STATUS);
    scr_puts(2, 23, " \x18\x19 Navigate  Enter Select  Q Quit ", ATTR_STATUS);
}

static void draw_about(void)
{
    int y = 4;

    scr_fill(1, 3, 78, 20, ' ', ATTR_NORMAL);

    scr_puts(4, y, "NetISA v1.0", ATTR_HIGHLIGHT);
    y += 2;
    scr_puts(4, y, "Open-source TLS 1.3 networking for vintage PCs", ATTR_NORMAL);
    y += 2;
    scr_puts(4, y, "Hardware:", ATTR_HEADER);
    scr_puts(14, y, "ATF1508AS CPLD + ESP32-S3", ATTR_NORMAL);
    y += 1;
    scr_puts(4, y, "License:", ATTR_HEADER);
    scr_puts(14, y, "MIT (software) / CERN-OHL-P (hardware)", ATTR_NORMAL);
    y += 2;

    scr_putc(4, y, (char)BOX_BULLET, ATTR_BORDER);
    scr_puts(6, y, "https://barelybooting.com", ATTR_INPUT);
    y += 1;
    scr_putc(4, y, (char)BOX_BULLET, ATTR_BORDER);
    scr_puts(6, y, "https://github.com/tonyuatkins-afk/NetISA", ATTR_INPUT);
    y += 2;

    scr_puts(4, y, "\"What the 8087 was to floating-point math,", ATTR_DIM);
    y += 1;
    scr_puts(5, y, "NetISA is to modern cryptography.\"", ATTR_DIM);

    y += 3;
    scr_puts(4, y, "Press ESC to return.", ATTR_DIM);

    for (;;) {
        int key = scr_getkey();
        if ((key & 0xFF) == 0x1B)
            return;
    }
}

static void draw_menu(int sel)
{
    int i;
    int menu_x = 4;
    int menu_y = 5;

    /* Clear content area */
    scr_fill(1, 3, 78, 20, ' ', ATTR_NORMAL);

    /* Section header */
    scr_putc(4, 3, (char)BOX_BULLET, ATTR_BORDER);
    scr_puts(6, 3, "Main Menu", ATTR_HEADER);

    for (i = 0; i < MENU_ITEMS; i++) {
        unsigned char attr;
        if (i == sel) {
            attr = ATTR_SELECTED;
            scr_putc(menu_x, menu_y + i, (char)BOX_ARROW, ATTR_SELECTED);
        } else {
            attr = ATTR_NORMAL;
            scr_putc(menu_x, menu_y + i, ' ', ATTR_NORMAL);
        }
        scr_puts(menu_x + 2, menu_y + i, menu_labels[i], attr);
    }
}

void menu_run(void)
{
    int sel = 0;
    int running = 1;

    draw_frame();

    while (running) {
        int key;

        draw_menu(sel);
        key = scr_getkey();

        switch (key & 0xFF) {
        case 0x1B:  /* ESC */
        case 'q':
        case 'Q':
            running = 0;
            break;
        case 0x0D:  /* Enter */
            switch (sel) {
            case 0:
                panel_wifi();
                draw_frame();
                break;
            case 1:
                panel_status();
                draw_frame();
                break;
            case 2:
                scr_shutdown();
                system("claude.exe");
                scr_init();
                draw_frame();
                break;
            case 3:
                draw_about();
                draw_frame();
                break;
            }
            break;
        default:
            /* Check extended key (scancode in high byte) */
            if ((key & 0xFF) == 0) {
                switch (key) {
                case KEY_UP:
                    if (sel > 0) sel--;
                    break;
                case KEY_DOWN:
                    if (sel < MENU_ITEMS - 1) sel++;
                    break;
                }
            }
            break;
        }
    }
}
