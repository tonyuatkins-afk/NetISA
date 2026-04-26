/*
 * ui/browser.h - File browser pane.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_UI_BROWSER_H
#define HEARO_UI_BROWSER_H

#include "../hearo.h"

void browser_init(const char *start_dir);
void browser_render(u8 x, u8 y, u8 w, u8 h, hbool focused);
hbool browser_handle_key(u16 key);   /* HTRUE if consumed */
const char *browser_current_path(void);
const char *browser_selected_filename(void);
hbool       browser_selected_is_dir(void);

#endif
