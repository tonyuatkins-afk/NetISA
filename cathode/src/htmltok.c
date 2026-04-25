/*
 * htmltok.c - HTML tokenizer state machine
 *
 * Character-by-character parser. Accumulates tag names and attributes,
 * decodes entities, feeds text through UTF-8 decoder, dispatches to
 * htmlout.c for layout.
 */

#include "htmltok.h"
#include "htmlout.h"
#include "screen.h"
#include <string.h>

void html_init(html_parser_t *p, page_buffer_t *page)
{
    memset(p, 0, sizeof(html_parser_t));
    p->page = page;
    p->current_attr = ATTR_NORMAL;
    utf8_init(&p->utf8);
}

/* Named entity → Unicode codepoint table.
 * Sorted by first character for fast rejection. */
typedef struct { const char *name; unsigned long cp; } entity_map_t;
static const entity_map_t named_entities[] = {
    /* Basic */
    {"amp",     '&'},     {"apos",    '\''},    {"gt",      '>'},
    {"lt",      '<'},     {"nbsp",    ' '},     {"quot",    '"'},
    /* Accented lowercase */
    {"aacute",  0xE1},    {"acirc",   0xE2},    {"agrave",  0xE0},
    {"aring",   0xE5},    {"atilde",  0xE3},    {"auml",    0xE4},
    {"ccedil",  0xE7},    {"eacute",  0xE9},    {"ecirc",   0xEA},
    {"egrave",  0xE8},    {"euml",    0xEB},    {"iacute",  0xED},
    {"icirc",   0xEE},    {"igrave",  0xEC},    {"iuml",    0xEF},
    {"ntilde",  0xF1},    {"oacute",  0xF3},    {"ocirc",   0xF4},
    {"ograve",  0xF2},    {"oslash",  0xF8},    {"otilde",  0xF5},
    {"ouml",    0xF6},    {"szlig",   0xDF},    {"uacute",  0xFA},
    {"ucirc",   0xFB},    {"ugrave",  0xF9},    {"uuml",    0xFC},
    {"yacute",  0xFD},    {"yuml",    0xFF},
    /* Accented uppercase */
    {"Aacute",  0xC1},    {"Acirc",   0xC2},    {"Agrave",  0xC0},
    {"Aring",   0xC5},    {"Atilde",  0xC3},    {"Auml",    0xC4},
    {"Ccedil",  0xC7},    {"Eacute",  0xC9},    {"Ecirc",   0xCA},
    {"Egrave",  0xC8},    {"Euml",    0xCB},    {"Iacute",  0xCD},
    {"Icirc",   0xCE},    {"Igrave",  0xCC},    {"Iuml",    0xCF},
    {"Ntilde",  0xD1},    {"Oacute",  0xD3},    {"Ocirc",   0xD4},
    {"Ograve",  0xD2},    {"Oslash",  0xD8},    {"Otilde",  0xD5},
    {"Ouml",    0xD6},    {"Uacute",  0xDA},    {"Ucirc",   0xDB},
    {"Ugrave",  0xD9},    {"Uuml",    0xDC},    {"Yacute",  0xDD},
    /* Punctuation */
    {"bull",    0x2022},  {"hellip",  0x2026},
    {"ldquo",   0x201C},  {"rdquo",   0x201D},
    {"lsquo",   0x2018},  {"rsquo",   0x2019},
    {"mdash",   0x2014},  {"ndash",   0x2013},
    {"laquo",   0xAB},    {"raquo",   0xBB},
    /* Currency & symbols */
    {"cent",    0xA2},    {"copy",    0xA9},    {"deg",     0xB0},
    {"euro",    0x20AC},  {"pound",   0xA3},    {"reg",     0xAE},
    {"sect",    0xA7},    {"trade",   0x2122},  {"yen",     0xA5},
    /* Math */
    {"divide",  0xF7},    {"frac12",  0xBD},    {"frac14",  0xBC},
    {"frac34",  0xBE},    {"minus",   0x2212},  {"plusmn",  0xB1},
    {"times",   0xD7},
    /* Arrows */
    {"darr",    0x2193},  {"larr",    0x2190},  {"rarr",    0x2192},
    {"uarr",    0x2191},
    /* Misc */
    {"iexcl",   0xA1},    {"iquest",  0xBF},    {"middot",  0xB7},
    {"micro",   0xB5},    {"para",    0xB6},    {"shy",     0xAD},
    {"sup1",    0xB9},    {"sup2",    0xB2},    {"sup3",    0xB3},
    {"ordf",    0xAA},    {"ordm",    0xBA},
    {NULL, 0}
};

/* Entity resolution: returns CP437 char, or 0 if unknown */
static unsigned char resolve_entity(const char *name, int len)
{
    /* Numeric entity: &#NNN; or &#xHH; */
    if (len > 1 && name[0] == '#') {
        unsigned long cp = 0;
        int i;
        if (name[1] == 'x' || name[1] == 'X') {
            for (i = 2; i < len; i++) {
                char c = name[i];
                if (c >= '0' && c <= '9') cp = cp * 16 + (c - '0');
                else if (c >= 'a' && c <= 'f') cp = cp * 16 + (c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') cp = cp * 16 + (c - 'A' + 10);
            }
        } else {
            for (i = 1; i < len; i++) {
                char c = name[i];
                if (c >= '0' && c <= '9') cp = cp * 10 + (c - '0');
            }
        }
        if (cp > 0)
            return utf8_cp_to_cp437(cp);
        return 0;
    }

    /* Named entity: linear search (table is ~90 entries, fast on 8088) */
    {
        const entity_map_t *e;
        for (e = named_entities; e->name != NULL; e++) {
            if ((int)strlen(e->name) == len &&
                memcmp(e->name, name, len) == 0)
                return utf8_cp_to_cp437(e->cp);
        }
    }
    return 0;  /* unknown entity */
}

/* Save a completed attribute to the appropriate saved_* field */
static void save_attribute(html_parser_t *p)
{
    const char *n = p->attr_name;
    const char *v = p->attr_val;
    int nlen = p->attr_name_len;

    if (nlen == 0) return;

    if (nlen == 4 && n[0] == 'h' && n[1] == 'r' && n[2] == 'e' && n[3] == 'f') {
        strncpy(p->saved_href, v, HTML_MAX_ATTR - 1);
        p->saved_href[HTML_MAX_ATTR - 1] = '\0';
    } else if (nlen == 3 && n[0] == 'a' && n[1] == 'l' && n[2] == 't') {
        strncpy(p->saved_alt, v, 63);
        p->saved_alt[63] = '\0';
    } else if (nlen == 3 && n[0] == 's' && n[1] == 'r' && n[2] == 'c') {
        strncpy(p->saved_src, v, HTML_MAX_ATTR - 1);
        p->saved_src[HTML_MAX_ATTR - 1] = '\0';
    } else if (nlen == 4 && n[0] == 't' && n[1] == 'y' && n[2] == 'p' && n[3] == 'e') {
        strncpy(p->saved_type, v, HTML_MAX_TAG - 1);
        p->saved_type[HTML_MAX_TAG - 1] = '\0';
    } else if (nlen == 4 && n[0] == 'n' && n[1] == 'a' && n[2] == 'm' && n[3] == 'e') {
        strncpy(p->saved_name, v, HTML_MAX_TAG - 1);
        p->saved_name[HTML_MAX_TAG - 1] = '\0';
    } else if (nlen == 5 && n[0] == 'v' && n[1] == 'a' && n[2] == 'l' && n[3] == 'u' && n[4] == 'e') {
        strncpy(p->saved_value, v, HTML_MAX_ATTR - 1);
        p->saved_value[HTML_MAX_ATTR - 1] = '\0';
    } else if (nlen == 6 && n[0] == 'a' && n[1] == 'c' && n[2] == 't' && n[3] == 'i' && n[4] == 'o' && n[5] == 'n') {
        strncpy(p->saved_action, v, HTML_MAX_ATTR - 1);
        p->saved_action[HTML_MAX_ATTR - 1] = '\0';
    } else if (nlen == 6 && n[0] == 'm' && n[1] == 'e' && n[2] == 't' && n[3] == 'h' && n[4] == 'o' && n[5] == 'd') {
        strncpy(p->saved_method, v, HTML_MAX_TAG - 1);
        p->saved_method[HTML_MAX_TAG - 1] = '\0';
    } else if (nlen == 4 && n[0] == 's' && n[1] == 'i' && n[2] == 'z' && n[3] == 'e') {
        strncpy(p->saved_size, v, HTML_MAX_TAG - 1);
        p->saved_size[HTML_MAX_TAG - 1] = '\0';
    }
}

/* Clear all saved attribute fields — called at start of new tag */
static void clear_saved_attrs(html_parser_t *p)
{
    p->saved_href[0] = '\0';
    p->saved_alt[0] = '\0';
    p->saved_src[0] = '\0';
    p->saved_type[0] = '\0';
    p->saved_name[0] = '\0';
    p->saved_value[0] = '\0';
    p->saved_action[0] = '\0';
    p->saved_method[0] = '\0';
    p->saved_size[0] = '\0';
}

/* Convert tag name to lowercase in-place */
static void lowercase_tag(char *s, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        if (s[i] >= 'A' && s[i] <= 'Z')
            s[i] += 32;
    }
}

/* Process one byte of input */
static void process_byte(html_parser_t *p, unsigned char byte)
{
    switch (p->state) {

    case PS_TEXT:
        if (byte == '<') {
            html_flush_word(p);
            p->state = PS_TAG_OPEN;
            p->tag_len = 0;
            p->tag_is_close = 0;
            p->attr_name_len = 0;
            p->attr_val_len = 0;
            p->attr_state = AS_SPACE;
            clear_saved_attrs(p);
            return;
        }
        /* Suppress output in <head>, <script>, <style>.
         * Note: '<' check above must stay before suppression (needed to
         * find </script> and </style>). But '&' must be suppressed. */
        if (p->in_script || p->in_style) return;
        if (p->in_head && !p->in_title) return;

        if (byte == '&') {
            html_flush_word(p);
            p->state = PS_ENTITY;
            p->entity_len = 0;
            return;
        }

        /* Feed through UTF-8 decoder */
        {
            unsigned char ch = utf8_feed(&p->utf8, byte);
            if (ch != 0) {
                if (p->in_title && p->in_head) {
                    /* Accumulate title text */
                    int tlen = (int)strlen(p->page->title);
                    if (tlen < 79) {
                        p->page->title[tlen] = (char)ch;
                        p->page->title[tlen + 1] = '\0';
                    }
                } else {
                    html_emit_char(p, (char)ch);
                }
            }
        }
        return;

    case PS_TAG_OPEN:
        if (byte == '/') {
            p->tag_is_close = 1;
            p->state = PS_TAG_NAME;
            return;
        }
        if (byte == '!') {
            /* Could be comment <!-- or doctype <!DOCTYPE */
            p->state = PS_COMMENT;
            p->comment_dashes = 0;
            return;
        }
        /* Start of tag name */
        p->state = PS_TAG_NAME;
        if (p->tag_len < HTML_MAX_TAG - 1)
            p->tag_name[p->tag_len++] = (char)byte;
        return;

    case PS_TAG_NAME:
        if (byte == '>' || byte == '/' || byte == ' ' ||
            byte == '\t' || byte == '\n' || byte == '\r') {
            p->tag_name[p->tag_len] = '\0';
            lowercase_tag(p->tag_name, p->tag_len);
            if (byte == '>') {
                p->state = PS_TEXT;
                html_dispatch_tag(p);
            } else if (byte == '/') {
                /* Self-closing like <br/> — wait for > */
                p->state = PS_TAG_ATTRS;
            } else {
                p->state = PS_TAG_ATTRS;
                p->attr_state = AS_SPACE;
            }
            return;
        }
        if (p->tag_len < HTML_MAX_TAG - 1)
            p->tag_name[p->tag_len++] = (char)byte;
        /* else: tag name too long, skip excess chars */
        return;

    case PS_TAG_ATTRS:
        if (byte == '>') {
            /* Finalize any pending attribute */
            if (p->attr_name_len > 0) {
                p->attr_name[p->attr_name_len] = '\0';
                p->attr_val[p->attr_val_len] = '\0';
                lowercase_tag(p->attr_name, p->attr_name_len);
                save_attribute(p);
            }
            p->state = PS_TEXT;
            html_dispatch_tag(p);
            return;
        }

        switch (p->attr_state) {
        case AS_SPACE:
            if (byte == '/') return;  /* self-closing slash */
            if (byte != ' ' && byte != '\t' && byte != '\n' && byte != '\r') {
                p->attr_name_len = 0;
                p->attr_val_len = 0;
                p->attr_state = AS_NAME;
                if (p->attr_name_len < HTML_MAX_TAG - 1)
                    p->attr_name[p->attr_name_len++] = (char)byte;
            }
            return;

        case AS_NAME:
            if (byte == '=') {
                p->attr_name[p->attr_name_len] = '\0';
                lowercase_tag(p->attr_name, p->attr_name_len);
                p->attr_state = AS_VAL_START;
            } else if (byte == ' ' || byte == '\t' || byte == '\n') {
                p->attr_name[p->attr_name_len] = '\0';
                lowercase_tag(p->attr_name, p->attr_name_len);
                p->attr_state = AS_EQ;
            } else {
                if (p->attr_name_len < HTML_MAX_TAG - 1)
                    p->attr_name[p->attr_name_len++] = (char)byte;
            }
            return;

        case AS_EQ:
            if (byte == '=') {
                p->attr_state = AS_VAL_START;
            } else if (byte != ' ' && byte != '\t') {
                /* No value — attribute is boolean. Save it first. */
                p->attr_val[0] = '\0';
                p->attr_val_len = 0;
                save_attribute(p);
                /* Start new attr. */
                p->attr_name_len = 0;
                p->attr_val_len = 0;
                p->attr_state = AS_NAME;
                if (p->attr_name_len < HTML_MAX_TAG - 1)
                    p->attr_name[p->attr_name_len++] = (char)byte;
            }
            return;

        case AS_VAL_START:
            if (byte == '"' || byte == '\'') {
                p->attr_quote = (char)byte;
                p->attr_state = AS_VAL_QUOTED;
                p->attr_val_len = 0;
            } else if (byte != ' ' && byte != '\t') {
                p->attr_state = AS_VAL_UNQUOTED;
                p->attr_val_len = 0;
                if (p->attr_val_len < HTML_MAX_ATTR - 1)
                    p->attr_val[p->attr_val_len++] = (char)byte;
            }
            return;

        case AS_VAL_QUOTED:
            if (byte == (unsigned char)p->attr_quote) {
                p->attr_val[p->attr_val_len] = '\0';
                /* Attribute complete — save and reset for next */
                save_attribute(p);
                p->attr_state = AS_SPACE;
            } else {
                if (p->attr_val_len < HTML_MAX_ATTR - 1)
                    p->attr_val[p->attr_val_len++] = (char)byte;
            }
            return;

        case AS_VAL_UNQUOTED:
            if (byte == ' ' || byte == '\t' || byte == '>') {
                p->attr_val[p->attr_val_len] = '\0';
                save_attribute(p);
                p->attr_state = AS_SPACE;
                if (byte == '>') {
                    p->state = PS_TEXT;
                    html_dispatch_tag(p);
                }
            } else {
                if (p->attr_val_len < HTML_MAX_ATTR - 1)
                    p->attr_val[p->attr_val_len++] = (char)byte;
            }
            return;
        }
        return;

    case PS_ENTITY:
        if (byte == ';' || p->entity_len >= 10 ||
            (byte == ' ' || byte == '<' || byte == '\n')) {
            unsigned char ch;
            p->entity_buf[p->entity_len] = '\0';
            ch = resolve_entity(p->entity_buf, p->entity_len);
            if (ch)
                html_emit_char(p, (char)ch);
            else {
                /* Unknown entity: emit raw &text; */
                int i;
                html_emit_char(p, '&');
                for (i = 0; i < p->entity_len; i++)
                    html_emit_char(p, p->entity_buf[i]);
                if (byte == ';')
                    html_emit_char(p, ';');
            }
            p->state = PS_TEXT;
            /* Reprocess the trigger byte unless it was ';' (consumed) */
            if (byte != ';')
                process_byte(p, byte);
            return;
        }
        p->entity_buf[p->entity_len++] = (char)byte;
        return;

    case PS_COMMENT:
        /* Handles <!-- comments --> and <!DOCTYPE ...> and <![CDATA[...]]>
         *
         * comment_dashes encoding:
         *   0-1: initial dashes after '<!' (deciding if it's a comment)
         *   10+: confirmed real comment (saw '<!--'), tracking closing dashes
         *         10 = inside comment, 11 = saw one '-', 12 = saw '--'
         *
         * For <!DOCTYPE> (no '--' at start), any '>' exits immediately.
         * For <!-- comment -->, need '-->' to exit. */
        if (p->comment_dashes < 10) {
            /* Still deciding: is this <!-- or <!DOCTYPE? */
            if (byte == '-') {
                p->comment_dashes++;
                if (p->comment_dashes == 2) {
                    /* Saw '<!-' then '-' = real comment start */
                    p->comment_dashes = 10;
                }
            } else if (byte == '>') {
                /* <!...> with no '--' = DOCTYPE, done */
                p->state = PS_TEXT;
                p->comment_dashes = 0;
            }
            /* else: non-dash in <!DOCTYPE..., keep waiting for '>' */
        } else {
            /* In a real comment. Track trailing dashes for '-->' */
            if (byte == '-') {
                p->comment_dashes++;
            } else if (byte == '>' && p->comment_dashes >= 12) {
                /* saw '-->' */
                p->state = PS_TEXT;
                p->comment_dashes = 0;
            } else {
                p->comment_dashes = 10;  /* reset dash count, stay in comment */
            }
        }
        return;
    }
}

void html_parse_chunk(html_parser_t *p, const char far *data, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        process_byte(p, (unsigned char)data[i]);
    }
}

void html_finish(html_parser_t *p)
{
    /* Flush any pending word */
    html_flush_word(p);

    /* If page was truncated, write footer on last row */
    if (p->truncated) {
        int r = PAGE_MAX_ROWS - 1;
        int c;
        const char *msg = "[Page truncated at 200 rows]";
        for (c = 0; msg[c] && c < PAGE_COLS; c++)
            page_set_cell(p->page, r, c, msg[c], ATTR_DIM, CELL_TEXT, 0);
        p->page->total_rows = PAGE_MAX_ROWS;
    }
}
