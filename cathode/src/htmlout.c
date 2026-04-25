/*
 * htmlout.c - HTML layout emitter
 *
 * Handles text emission with word-boundary wrapping, whitespace
 * collapsing, style stack, link tracking, and tag dispatch for
 * block and inline elements.
 */

#include "htmlout.h"
#include "render.h"
#include "screen.h"
#include "url.h"
#include <string.h>

/* Write a single cell to the page buffer */
static void put_cell(html_parser_t *p, int row, int col,
                     char ch, unsigned char attr,
                     unsigned char type, unsigned short link_id)
{
    if (row >= PAGE_MAX_ROWS - 1) {
        p->truncated = 1;
        return;
    }
    page_set_cell(p->page, row, col, ch, attr, type, link_id);
}

/* Flush the word buffer to the page */
void html_flush_word(html_parser_t *p)
{
    int i;
    unsigned char type;
    unsigned short lid;

    if (p->word_len == 0) return;

    /* If word doesn't fit on current line, wrap first */
    if (p->col + p->word_len > 78 && p->col > p->indent && !p->in_pre) {
        p->row++;
        p->col = p->indent;
    }

    type = p->word_is_link ? CELL_LINK : CELL_TEXT;
    lid = p->word_is_link ? p->word_link_id : 0;

    for (i = 0; i < p->word_len; i++) {
        if (p->col >= 78) {
            if (p->in_pre) {
                /* Truncate with indicator */
                put_cell(p, p->row, 78, (char)0xAF, ATTR_DIM, CELL_TEXT, 0);
                break;
            }
            p->row++;
            p->col = p->indent;
        }
        put_cell(p, p->row, p->col, p->word_buf[i],
                 p->word_attr, type, lid);
        p->col++;
        p->line_has_content = 1;
    }

    p->word_len = 0;
}

void html_emit_char(html_parser_t *p, char ch)
{
    if (p->truncated) return;

    /* Whitespace handling */
    if (!p->in_pre && (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')) {
        if (!p->line_has_content && p->word_len == 0) return;

        /* Flush current word, then emit a space */
        html_flush_word(p);

        /* Collapse consecutive whitespace */
        if (p->col > p->indent) {
            page_cell_t prev = page_get_cell(p->page, p->row, p->col - 1);
            if (prev.ch == ' ') return;
        }
        /* Emit the space directly (not buffered) */
        put_cell(p, p->row, p->col, ' ', p->current_attr,
                 p->in_link ? CELL_LINK : CELL_TEXT,
                 p->in_link ? (unsigned short)p->page->link_count : 0);
        p->col++;
        p->line_has_content = 1;
        return;
    }

    /* In <pre> mode, handle newlines */
    if (p->in_pre && (ch == '\n' || ch == '\r')) {
        html_flush_word(p);
        if (ch == '\n') html_emit_newline(p);
        return;
    }

    /* Flush if style changed mid-word */
    if (p->word_len > 0 && p->current_attr != p->word_attr) {
        html_flush_word(p);
    }

    /* Buffer the character for word-boundary wrapping */
    if (p->word_len < 78) {
        p->word_buf[p->word_len++] = ch;
        p->word_attr = p->current_attr;
        p->word_is_link = p->in_link;
        p->word_link_id = p->in_link ? (unsigned short)p->page->link_count : 0;
    }
    /* If word exceeds line width, force flush */
    if (p->word_len >= 78 - p->indent) {
        html_flush_word(p);
    }
}

void html_emit_newline(html_parser_t *p)
{
    html_flush_word(p);
    if (p->truncated) return;
    p->row++;
    p->col = p->indent;
    p->line_has_content = 0;
}

void html_emit_blank_line(html_parser_t *p)
{
    html_flush_word(p);
    if (p->truncated) return;
    if (p->line_has_content) {
        p->row++;
        p->col = p->indent;
    }
    p->row++;
    p->col = p->indent;
    p->line_has_content = 0;
}

/* === Style stack === */

void html_push_style(html_parser_t *p, unsigned char style_type)
{
    if (p->style_depth >= HTML_STYLE_DEPTH) return;  /* silent ignore */
    p->style_stack[p->style_depth++] = style_type;
    html_recompute_attr(p);
}

void html_pop_style(html_parser_t *p)
{
    if (p->style_depth <= 0) return;
    p->style_depth--;
    html_recompute_attr(p);
}

void html_recompute_attr(html_parser_t *p)
{
    int i;
    p->current_attr = ATTR_NORMAL;
    for (i = 0; i < p->style_depth; i++) {
        switch (p->style_stack[i]) {
        case STYLE_BOLD:
            p->current_attr = ATTR_HIGHLIGHT;
            break;
        case STYLE_ITALIC:
            p->current_attr = ATTR_DIM;
            break;
        case STYLE_CODE:
            p->current_attr = SCR_ATTR(SCR_GREEN, SCR_BLACK);
            break;
        case STYLE_LINK:
            p->current_attr = ATTR_LINK;
            break;
        case STYLE_HEADING:
            p->current_attr = ATTR_HEADER;
            break;
        case STYLE_UNDERLINE:
            p->current_attr = SCR_ATTR(SCR_LIGHTCYAN, SCR_BLACK);
            break;
        }
    }
}

/* === Helper: case-insensitive tag name comparison === */

static int tag_eq(const char *tag, const char *name)
{
    return (strcmp(tag, name) == 0);
}

static int tag_is_heading(const char *tag)
{
    return (tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6' && tag[2] == '\0');
}

/* === Helper: check saved attribute by name === */

static const char *get_saved_attr(html_parser_t *p, const char *name)
{
    if (strcmp(name, "href") == 0) return p->saved_href;
    if (strcmp(name, "alt") == 0) return p->saved_alt;
    if (strcmp(name, "src") == 0) return p->saved_src;
    if (strcmp(name, "type") == 0) return p->saved_type;
    if (strcmp(name, "name") == 0) return p->saved_name;
    if (strcmp(name, "value") == 0) return p->saved_value;
    if (strcmp(name, "action") == 0) return p->saved_action;
    if (strcmp(name, "method") == 0) return p->saved_method;
    if (strcmp(name, "size") == 0) return p->saved_size;
    return "";
}

static int has_attr(html_parser_t *p, const char *name)
{
    return (get_saved_attr(p, name)[0] != '\0');
}

/* === Tag dispatch === */

void html_dispatch_tag(html_parser_t *p)
{
    const char *tag = p->tag_name;
    int is_close = p->tag_is_close;

    /* ---- Suppression tags ---- */
    if (tag_eq(tag, "script")) {
        p->in_script = is_close ? 0 : 1;
        return;
    }
    if (tag_eq(tag, "style")) {
        p->in_style = is_close ? 0 : 1;
        return;
    }
    if (tag_eq(tag, "head")) {
        p->in_head = is_close ? 0 : 1;
        return;
    }
    if (tag_eq(tag, "title")) {
        if (!is_close) {
            p->in_title = 1;
            p->page->title[0] = '\0';
        } else {
            p->in_title = 0;
        }
        return;
    }

    /* <body> implicitly closes <head> (handles missing </head>) */
    if (tag_eq(tag, "body")) {
        p->in_head = 0;
        return;
    }

    /* Skip everything in <head> or <script>/<style> */
    if (p->in_script || p->in_style || (p->in_head && !p->in_title)) return;

    /* ---- Block elements ---- */
    if (tag_is_heading(tag)) {
        if (!is_close) {
            html_emit_blank_line(p);
            html_push_style(p, STYLE_HEADING);
        } else {
            html_pop_style(p);
            html_emit_blank_line(p);
        }
        return;
    }

    if (tag_eq(tag, "p") || tag_eq(tag, "div")) {
        if (!is_close) {
            html_emit_blank_line(p);
        } else {
            html_emit_blank_line(p);
        }
        return;
    }

    if (tag_eq(tag, "br")) {
        html_emit_newline(p);
        return;
    }

    if (tag_eq(tag, "hr")) {
        int c;
        html_emit_newline(p);
        for (c = p->indent; c < 78; c++)
            put_cell(p, p->row, c, (char)0xC4, ATTR_HRULE, CELL_TEXT, 0);
        p->row++;
        p->col = p->indent;
        p->line_has_content = 0;
        return;
    }

    if (tag_eq(tag, "pre")) {
        if (!is_close) {
            html_emit_newline(p);
            p->in_pre = 1;
            html_push_style(p, STYLE_CODE);
        } else {
            p->in_pre = 0;
            html_pop_style(p);
            html_emit_newline(p);
        }
        return;
    }

    if (tag_eq(tag, "blockquote")) {
        if (!is_close) {
            html_emit_newline(p);
            p->indent += 4;
            if (p->indent > 60) p->indent = 60;
            p->col = p->indent;
        } else {
            p->indent -= 4;
            if (p->indent < 0) p->indent = 0;
            html_emit_newline(p);
        }
        return;
    }

    /* Lists */
    if (tag_eq(tag, "ul")) {
        if (!is_close) {
            html_emit_newline(p);
            if (p->list_depth < 4) {
                p->list_ordered[p->list_depth] = 0;
                p->list_counter[p->list_depth] = 0;
                p->list_depth++;
            }
            p->indent += 2;
            if (p->indent > 60) p->indent = 60;
            p->col = p->indent;
        } else {
            if (p->list_depth > 0) p->list_depth--;
            p->indent -= 2;
            if (p->indent < 0) p->indent = 0;
            html_emit_newline(p);
        }
        return;
    }

    if (tag_eq(tag, "ol")) {
        if (!is_close) {
            html_emit_newline(p);
            if (p->list_depth < 4) {
                p->list_ordered[p->list_depth] = 1;
                p->list_counter[p->list_depth] = 0;
                p->list_depth++;
            }
            p->indent += 3;
            if (p->indent > 60) p->indent = 60;
            p->col = p->indent;
        } else {
            if (p->list_depth > 0) p->list_depth--;
            p->indent -= 3;
            if (p->indent < 0) p->indent = 0;
            html_emit_newline(p);
        }
        return;
    }

    if (tag_eq(tag, "li")) {
        if (!is_close) {
            html_emit_newline(p);
            if (p->list_depth > 0) {
                int d = p->list_depth - 1;
                if (p->list_ordered[d]) {
                    int cnt;
                    p->list_counter[d]++;
                    cnt = p->list_counter[d];
                    /* Cap display at 99 to avoid non-digit overflow */
                    if (cnt > 99) cnt = 99;
                    if (cnt >= 10 && p->col >= 3) {
                        put_cell(p, p->row, p->col - 3,
                                 (char)('0' + (cnt / 10)),
                                 ATTR_BULLET, CELL_TEXT, 0);
                        put_cell(p, p->row, p->col - 2,
                                 (char)('0' + (cnt % 10)),
                                 ATTR_BULLET, CELL_TEXT, 0);
                        put_cell(p, p->row, p->col - 1, '.',
                                 ATTR_BULLET, CELL_TEXT, 0);
                    } else if (cnt < 10 && p->col >= 2) {
                        put_cell(p, p->row, p->col - 2,
                                 (char)('0' + cnt),
                                 ATTR_BULLET, CELL_TEXT, 0);
                        put_cell(p, p->row, p->col - 1, '.',
                                 ATTR_BULLET, CELL_TEXT, 0);
                    }
                } else {
                    if (p->col >= 2) {
                        put_cell(p, p->row, p->col - 2, (char)0x07,
                                 ATTR_BULLET, CELL_TEXT, 0);
                    }
                }
            }
        }
        return;
    }

    /* ---- Inline elements ---- */
    if (tag_eq(tag, "a")) {
        if (!is_close) {
            /* If at link capacity, render as plain text */
            if (p->page->link_count >= MAX_LINKS) {
                return;
            }
            /* Save href attribute if present */
            if (has_attr(p, "href")) {
                strncpy(p->link_href, p->saved_href, HTML_MAX_ATTR - 1);
                p->link_href[HTML_MAX_ATTR - 1] = '\0';
            } else {
                p->link_href[0] = '\0';
            }
            html_flush_word(p);
            p->link_start_row = p->row;
            p->link_start_col = p->col;
            p->in_link = 1;
            html_push_style(p, STYLE_LINK);
        } else {
            if (!p->in_link) return;  /* close without open (at capacity) */
            html_flush_word(p);
            html_pop_style(p);
            p->in_link = 0;

            /* Register the link */
            if (p->link_href[0]) {
                /* Resolve relative URL */
                char resolved[URL_MAX + 1];
                url_resolve(p->page->url, p->link_href,
                            resolved, URL_MAX + 1);

                if (p->page->link_count < MAX_LINKS) {
                    page_add_link(p->page, resolved,
                                  p->link_start_row, p->link_start_col,
                                  p->row, p->col - 1);
                } else {
                    /* MAX_LINKS exceeded: walk back and reset cells */
                    int r = p->link_start_row;
                    int c = p->link_start_col;
                    while (r < p->row || (r == p->row && c < p->col)) {
                        if (r < PAGE_MAX_ROWS && c < PAGE_COLS)
                            p->page->meta[PAGE_IDX(r, c)] = CELL_TEXT;
                        c++;
                        if (c >= PAGE_COLS) { c = 0; r++; }
                    }
                }
            }
        }
        return;
    }

    if (tag_eq(tag, "b") || tag_eq(tag, "strong")) {
        if (!is_close) html_push_style(p, STYLE_BOLD);
        else html_pop_style(p);
        return;
    }

    if (tag_eq(tag, "i") || tag_eq(tag, "em")) {
        if (!is_close) html_push_style(p, STYLE_ITALIC);
        else html_pop_style(p);
        return;
    }

    if (tag_eq(tag, "u")) {
        if (!is_close) html_push_style(p, STYLE_UNDERLINE);
        else html_pop_style(p);
        return;
    }

    if (tag_eq(tag, "code")) {
        if (!is_close) html_push_style(p, STYLE_CODE);
        else html_pop_style(p);
        return;
    }

    /* ---- Image tag ---- */
    if (tag_eq(tag, "img") && !is_close) {
        /* Show alt text as [IMG: alt] */
        const char *prefix = "[IMG: ";
        int i;
        for (i = 0; prefix[i]; i++)
            html_emit_char(p, prefix[i]);
        if (has_attr(p, "alt")) {
            const char *alt = p->saved_alt;
            for (i = 0; alt[i] && i < 60; i++)
                html_emit_char(p, alt[i]);
        } else {
            html_emit_char(p, '?');
        }
        html_emit_char(p, ']');
        return;
    }

    /* ---- Table tags (Phase 3, stubbed) ---- */
#if FEAT_TABLES
    /* TODO: buffered table rendering */
#endif
    if (tag_eq(tag, "table") || tag_eq(tag, "tbody") ||
        tag_eq(tag, "thead") || tag_eq(tag, "tfoot")) {
        if (!is_close) html_emit_newline(p);
        return;
    }
    if (tag_eq(tag, "tr")) {
        if (!is_close) html_emit_newline(p);
        return;
    }
    if (tag_eq(tag, "td") || tag_eq(tag, "th")) {
        if (!is_close) {
            /* Separate columns with pipe */
            if (p->line_has_content)
                html_emit_char(p, '|');
            html_emit_char(p, ' ');
            if (tag_eq(tag, "th"))
                html_push_style(p, STYLE_BOLD);
        } else {
            if (tag_eq(tag, "th"))
                html_pop_style(p);
            html_emit_char(p, ' ');
        }
        return;
    }

    /* ---- Form tags (Phase 3, stubbed) ---- */
#if FEAT_FORMS
    /* TODO: form field creation */
#endif
    if (tag_eq(tag, "form")) return;
    if (tag_eq(tag, "input") && !is_close) {
        html_emit_char(p, '[');
        if (has_attr(p, "value")) {
            int i;
            const char *val = p->saved_value;
            for (i = 0; val[i] && i < 20; i++)
                html_emit_char(p, val[i]);
        } else {
            int i;
            for (i = 0; i < 10; i++)
                html_emit_char(p, '_');
        }
        html_emit_char(p, ']');
        return;
    }
    if (tag_eq(tag, "textarea") || tag_eq(tag, "select") ||
        tag_eq(tag, "button") || tag_eq(tag, "option") ||
        tag_eq(tag, "label")) {
        /* Pass through content as text */
        return;
    }

    /* Unknown tags: silently ignore */
}
