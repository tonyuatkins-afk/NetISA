/*
 * search.h - Find on page for Cathode browser
 */

#ifndef SEARCH_H
#define SEARCH_H

#include "page.h"
#include "screen.h"

#define SEARCH_MAX 60
#define SEARCH_MAX_MATCHES 20

/* Search highlight attributes */
#define ATTR_SEARCH_HIT SCR_ATTR(SCR_BLACK, SCR_YELLOW)
#define ATTR_SEARCH_CUR SCR_ATTR(SCR_BLACK, SCR_WHITE)

typedef struct {
    int row;
    int col;
} search_pos_t;

typedef struct {
    char query[SEARCH_MAX + 1];
    int query_len;
    int active;          /* 1 = search mode active */
    int editing;         /* 1 = typing in search bar */
    int match_row;       /* current match position (-1 = none) */
    int match_col;
    int total_matches;
    int cursor;          /* cursor position within query */
    int current_idx;     /* index into matches[] for current match */
    search_pos_t matches[SEARCH_MAX_MATCHES];
} search_state_t;

void search_init(search_state_t *s);
void search_start(search_state_t *s);
void search_cancel(search_state_t *s);
int  search_handle_key(search_state_t *s, int key);  /* 1=search, -1=cancel, 0=cont */
void search_find_next(search_state_t *s, page_buffer_t *page, int direction);
int  search_is_match(search_state_t *s, page_buffer_t *page, int row, int col);

#endif /* SEARCH_H */
