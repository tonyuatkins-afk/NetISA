/*
 * ui/ui.h - Main interactive UI loop.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_UI_H
#define HEARO_UI_H

#include "../hearo.h"

void ui_run(const hw_profile_t *hw);

/* Focus zones */
typedef enum { UI_FOCUS_BROWSER=0, UI_FOCUS_PLAYLIST, UI_FOCUS_NOWPLAY, UI_FOCUS_COUNT } ui_focus_t;
ui_focus_t ui_focus(void);
void ui_set_focus(ui_focus_t f);

/* Request a redraw of a region (used after panel state change). */
void ui_request_redraw(void);

#endif
