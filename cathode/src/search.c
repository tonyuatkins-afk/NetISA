/*
 * search.c - Find on page for Cathode browser
 *
 * Linear scan of page cells with case-insensitive matching.
 * Highlights matches during render, cycles with N/Shift+N.
 */

#include "search.h"
#include "screen.h"
#include <string.h>

static char to_lower(char c)
{
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

void search_init(search_state_t *s)
{
    memset(s, 0, sizeof(search_state_t));
    s->match_row = -1;
    s->match_col = -1;
}

void search_start(search_state_t *s)
{
    s->active = 1;
    s->editing = 1;
    s->query[0] = '\0';
    s->query_len = 0;
    s->cursor = 0;
    s->match_row = -1;
    s->total_matches = 0;
}

void search_cancel(search_state_t *s)
{
    s->active = 0;
    s->editing = 0;
    s->match_row = -1;
    s->total_matches = 0;
}

int search_handle_key(search_state_t *s, int key)
{
    int ch = key & 0xFF;

    /* Enter: execute search */
    if (ch == 0x0D) {
        s->editing = 0;
        return 1;
    }

    /* Escape: cancel search */
    if (ch == 0x1B) {
        search_cancel(s);
        return -1;
    }

    /* Backspace */
    if (ch == 0x08) {
        if (s->cursor > 0) {
            int i;
            s->cursor--;
            for (i = s->cursor; i < s->query_len - 1; i++)
                s->query[i] = s->query[i + 1];
            s->query_len--;
            s->query[s->query_len] = '\0';
        }
        return 0;
    }

    /* Printable character */
    if (ch >= 0x20 && ch < 0x7F && s->query_len < SEARCH_MAX) {
        int i;
        for (i = s->query_len; i > s->cursor; i--)
            s->query[i] = s->query[i - 1];
        s->query[s->cursor] = (char)ch;
        s->cursor++;
        s->query_len++;
        s->query[s->query_len] = '\0';
    }

    return 0;
}

/* Scan all matches and store positions (up to SEARCH_MAX_MATCHES) */
static void search_scan_all(search_state_t *s, page_buffer_t *page)
{
    int r, c, qi;
    int qlen = s->query_len;

    s->total_matches = 0;
    if (qlen == 0) return;

    for (r = 0; r < page->total_rows; r++) {
        for (c = 0; c <= PAGE_COLS - qlen; c++) {
            int match = 1;
            for (qi = 0; qi < qlen; qi++) {
                page_cell_t cell = page_get_cell(page, r, c + qi);
                if (to_lower(cell.ch) != to_lower(s->query[qi])) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                if (s->total_matches < SEARCH_MAX_MATCHES) {
                    s->matches[s->total_matches].row = r;
                    s->matches[s->total_matches].col = c;
                }
                s->total_matches++;
            }
        }
    }
}

void search_find_next(search_state_t *s, page_buffer_t *page, int direction)
{
    int cap, i, best;

    if (s->query_len == 0) return;

    /* Scan all matches for highlight support */
    search_scan_all(s, page);

    cap = s->total_matches;
    if (cap > SEARCH_MAX_MATCHES) cap = SEARCH_MAX_MATCHES;

    if (cap == 0) {
        s->match_row = -1;
        s->match_col = -1;
        s->current_idx = -1;
        return;
    }

    /* Find next match in matches[] array from current position */
    best = -1;
    if (direction >= 0) {
        /* Forward: find first match after current position */
        for (i = 0; i < cap; i++) {
            if (s->match_row < 0 ||
                s->matches[i].row > s->match_row ||
                (s->matches[i].row == s->match_row &&
                 s->matches[i].col > s->match_col)) {
                best = i;
                break;
            }
        }
        /* Wrap to first match */
        if (best < 0) best = 0;
    } else {
        /* Backward: find last match before current position */
        for (i = cap - 1; i >= 0; i--) {
            if (s->match_row < 0 ||
                s->matches[i].row < s->match_row ||
                (s->matches[i].row == s->match_row &&
                 s->matches[i].col < s->match_col)) {
                best = i;
                break;
            }
        }
        /* Wrap to last match */
        if (best < 0) best = cap - 1;
    }

    s->match_row = s->matches[best].row;
    s->match_col = s->matches[best].col;
    s->current_idx = best;
}

int search_is_match(search_state_t *s, page_buffer_t *page, int row, int col)
{
    int i, cap, qlen;

    (void)page;

    if (!s->active || s->query_len == 0) return 0;
    qlen = s->query_len;

    cap = s->total_matches;
    if (cap > SEARCH_MAX_MATCHES) cap = SEARCH_MAX_MATCHES;

    /* Check if (row, col) falls within any stored match */
    for (i = 0; i < cap; i++) {
        int mr = s->matches[i].row;
        int mc = s->matches[i].col;
        if (row == mr && col >= mc && col < mc + qlen)
            return 1;
    }

    return 0;
}
