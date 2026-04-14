/*
 * search_dc.c - Find-in-messages for Discord v2 DOS client
 *
 * Ctrl+F search across messages in the current channel.
 * Scans message structs in the near-heap pool, copies text
 * to a scratch buffer for fast case-insensitive matching
 * on 8088 (avoids far pointer arithmetic in tight loop).
 *
 * Adapted from Cathode's search pattern but searches
 * dc_message_t structs instead of page buffer cells.
 *
 * Target: 8088 real mode, OpenWatcom C, small memory model.
 */

#include "discord.h"
#include <string.h>

#if FEAT_SEARCH

/* ----------------------------------------------------------------
 * Case-insensitive character comparison
 * ---------------------------------------------------------------- */

static int ci_char_eq(char a, char b)
{
    if (a >= 'A' && a <= 'Z') a += 32;
    if (b >= 'A' && b <= 'Z') b += 32;
    return a == b;
}

/* ----------------------------------------------------------------
 * Case-insensitive substring search in near buffer.
 * Returns offset of first match, or -1 if not found.
 * start_pos: byte offset to begin searching from.
 * ---------------------------------------------------------------- */

static int ci_strstr(const char *haystack, int haylen,
                     const char *needle, int needlelen,
                     int start_pos)
{
    int i, j;

    if (needlelen == 0) return -1;
    if (start_pos < 0) start_pos = 0;

    for (i = start_pos; i <= haylen - needlelen; i++) {
        int match = 1;
        for (j = 0; j < needlelen; j++) {
            if (!ci_char_eq(haystack[i + j], needle[j])) {
                match = 0;
                break;
            }
        }
        if (match) return i;
    }
    return -1;
}

/* ----------------------------------------------------------------
 * dc_search_open  - activate search bar, begin editing
 * ---------------------------------------------------------------- */

void dc_search_open(dc_state_t *s)
{
    dc_search_t *sr = &s->search;
    sr->active = 1;
    sr->query[0] = '\0';
    sr->query_len = 0;
    sr->match_count = 0;
    sr->current_match = -1;
}

/* ----------------------------------------------------------------
 * dc_search_close - deactivate search, clear matches
 * ---------------------------------------------------------------- */

void dc_search_close(dc_state_t *s)
{
    dc_search_t *sr = &s->search;
    sr->active = 0;
    sr->query[0] = '\0';
    sr->query_len = 0;
    sr->match_count = 0;
    sr->current_match = -1;
    s->dirty = 1;
}

/* ----------------------------------------------------------------
 * dc_search_update - rescan all messages for current query
 *
 * Copies each message's text into a near scratch buffer and
 * runs case-insensitive substring search. Records all match
 * positions up to DC_SEARCH_MAX_MATCHES.
 * ---------------------------------------------------------------- */

void dc_search_update(dc_state_t *s)
{
    dc_search_t *sr = &s->search;
    char scratch[DC_MAX_MSG_LEN];
    int m, offset, tlen, qlen;

    sr->match_count = 0;
    sr->current_match = -1;

    qlen = sr->query_len;
    if (qlen == 0) return;

    for (m = 0; m < s->msg_count; m++) {
        /* Copy message text to near scratch buffer */
        memcpy(scratch, s->messages[m].text, DC_MAX_MSG_LEN);
        tlen = (int)strlen(scratch);

        /* Find all occurrences in this message */
        offset = 0;
        for (;;) {
            int pos = ci_strstr(scratch, tlen, sr->query, qlen, offset);
            if (pos < 0) break;

            if (sr->match_count < DC_SEARCH_MAX_MATCHES) {
                sr->matches[sr->match_count].msg_idx = m;
                sr->matches[sr->match_count].offset = pos;
                sr->match_count++;
            }

            offset = pos + 1;  /* advance past this match */
        }
    }

    /* Position on first match if any found */
    if (sr->match_count > 0)
        sr->current_match = 0;
}

/* ----------------------------------------------------------------
 * dc_search_next - advance to next match (with wrap)
 * ---------------------------------------------------------------- */

void dc_search_next(dc_state_t *s)
{
    dc_search_t *sr = &s->search;

    if (sr->match_count == 0) return;

    sr->current_match++;
    if (sr->current_match >= sr->match_count)
        sr->current_match = 0;

    /* Scroll message view to show the matched message */
    s->msg_scroll = sr->matches[sr->current_match].msg_idx;
    s->dirty = 1;
}

/* ----------------------------------------------------------------
 * dc_search_prev - go to previous match (with wrap)
 * ---------------------------------------------------------------- */

void dc_search_prev(dc_state_t *s)
{
    dc_search_t *sr = &s->search;

    if (sr->match_count == 0) return;

    sr->current_match--;
    if (sr->current_match < 0)
        sr->current_match = sr->match_count - 1;

    /* Scroll message view to show the matched message */
    s->msg_scroll = sr->matches[sr->current_match].msg_idx;
    s->dirty = 1;
}

/* ----------------------------------------------------------------
 * dc_search_handle_key - process keystrokes during search
 *
 * Returns:  1 = execute search (Enter pressed)
 *          -1 = search cancelled (Escape pressed)
 *           0 = continue editing / key consumed
 * ---------------------------------------------------------------- */

void dc_search_handle_key(dc_state_t *s, int key)
{
    dc_search_t *sr = &s->search;
    int ch = key & 0xFF;
    int i;

    /* Escape: cancel search */
    if (ch == KEY_ESC) {
        dc_search_close(s);
        return;
    }

    /* Enter: execute search with current query */
    if (ch == KEY_ENTER) {
        dc_search_update(s);
        if (sr->match_count > 0) {
            s->msg_scroll = sr->matches[sr->current_match].msg_idx;
        }
        s->dirty = 1;
        return;
    }

    /* Backspace: delete char before cursor */
    if (ch == KEY_BACKSPACE) {
        if (sr->query_len > 0) {
            sr->query_len--;
            sr->query[sr->query_len] = '\0';
            dc_search_update(s);
            s->dirty = 1;
        }
        return;
    }

    /* Extended keys (ASCII byte = 0, scan code in high byte) */
    if (ch == 0) {
        switch (key) {
        case KEY_DEL:
            /* Delete has no effect in this simple append model */
            break;
        case KEY_F1:
            /* Ignore function keys during search */
            break;
        default:
            break;
        }
        return;
    }

    /* Printable character: append to query */
    if (ch >= 0x20 && ch < 0x7F && sr->query_len < DC_SEARCH_MAX) {
        sr->query[sr->query_len] = (char)ch;
        sr->query_len++;
        sr->query[sr->query_len] = '\0';

        /* Live search: update matches as user types */
        dc_search_update(s);
        if (sr->match_count > 0) {
            s->msg_scroll = sr->matches[sr->current_match].msg_idx;
        }
        s->dirty = 1;
    }
}

#endif /* FEAT_SEARCH */
