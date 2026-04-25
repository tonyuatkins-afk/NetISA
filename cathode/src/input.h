/*
 * input.h - Key constants and input types for Cathode v0.2
 */

#ifndef INPUT_H
#define INPUT_H

#include "browser.h"

/* Extended key codes (scan code in high byte) */
#define KEY_PGUP        0x4900
#define KEY_PGDN        0x5100
#define KEY_HOME        0x4700
#define KEY_END         0x4F00
#define KEY_DELETE      0x5300
#define KEY_F5          0x3F00
#define KEY_F6          0x4000
#define KEY_SHIFT_TAB   0x0F00

/* Ctrl keys (ASCII values) */
#define KEY_CTRL_L      0x000C  /* Ctrl+L = focus URL bar */
#define KEY_CTRL_F      0x0006  /* Ctrl+F = find */
#define KEY_CTRL_D      0x0004  /* Ctrl+D = bookmark */
#define KEY_CTRL_B      0x0002  /* Ctrl+B = view bookmarks */

void input_handle_key(browser_state_t *b, int key);

#endif /* INPUT_H */
