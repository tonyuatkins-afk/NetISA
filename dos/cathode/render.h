/*
 * render.h - Renderer types for Cathode browser
 */

#ifndef RENDER_H
#define RENDER_H

#include "page.h"
#include "screen.h"

/* Cathode-specific attributes (extending screen.h design language) */
#define ATTR_LINK       SCR_ATTR(SCR_LIGHTCYAN, SCR_BLACK)
#define ATTR_LINK_SEL   SCR_ATTR(SCR_BLACK, SCR_CYAN)
#define ATTR_BOLD       SCR_ATTR(SCR_WHITE, SCR_BLACK)
#define ATTR_HEADING    SCR_ATTR(SCR_WHITE, SCR_BLACK)
#define ATTR_TABLE      SCR_ATTR(SCR_GREEN, SCR_BLACK)
#define ATTR_HRULE      SCR_ATTR(SCR_DARKGRAY, SCR_BLACK)
#define ATTR_BULLET     SCR_ATTR(SCR_GREEN, SCR_BLACK)

/* Screen layout constants */
#define LAYOUT_TITLE_ROW    0
#define LAYOUT_URL_ROW      1
#define LAYOUT_SEP1_ROW     2
#define LAYOUT_CONTENT_TOP  3
#define LAYOUT_CONTENT_BOT  22
#define LAYOUT_SEP2_ROW     23
#define LAYOUT_STATUS_ROW   24

void render_page(page_buffer_t *page);
void render_titlebar(const char *title);
void render_urlbar(const char *url, int editing, int cursor_pos);
void render_statusbar(page_buffer_t *page, const char *status_msg);
void render_chrome(void);
void render_all(page_buffer_t *page, const char *url,
                int url_editing, int url_cursor, const char *status_msg);

#endif /* RENDER_H */
