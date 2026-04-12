/*
 * browser.h - Browser state structures for Cathode
 */

#ifndef BROWSER_H
#define BROWSER_H

#include "page.h"
#include "urlbar.h"

#define HISTORY_MAX 20

typedef struct {
    page_buffer_t far *current_page;
    char history[HISTORY_MAX][256];
    int history_pos;
    int history_count;
    int running;
    urlbar_t urlbar;
    int menu_open;
    char status_msg[80];
} browser_state_t;

void browser_init(browser_state_t *b);
void browser_shutdown(browser_state_t *b);
void browser_navigate(browser_state_t *b, const char *url);
void browser_back(browser_state_t *b);
void browser_forward(browser_state_t *b);
void browser_reload(browser_state_t *b);
void browser_scroll(browser_state_t *b, int delta);
void browser_scroll_to(browser_state_t *b, int pos);
void browser_select_link(browser_state_t *b, int delta);
void browser_follow_link(browser_state_t *b);

#endif /* BROWSER_H */
