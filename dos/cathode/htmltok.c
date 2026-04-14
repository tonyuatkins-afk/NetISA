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

/* Entity resolution: returns CP437 char, or 0 if unknown */
static unsigned char resolve_entity(const char *name, int len)
{
    if (len == 3 && name[0] == 'a' && name[1] == 'm' && name[2] == 'p')
        return '&';
    if (len == 2 && name[0] == 'l' && name[1] == 't')
        return '<';
    if (len == 2 && name[0] == 'g' && name[1] == 't')
        return '>';
    if (len == 4 && name[0] == 'q' && name[1] == 'u' &&
        name[2] == 'o' && name[3] == 't')
        return '"';
    if (len == 4 && name[0] == 'n' && name[1] == 'b' &&
        name[2] == 's' && name[3] == 'p')
        return ' ';
    if (len == 4 && name[0] == 'c' && name[1] == 'o' &&
        name[2] == 'p' && name[3] == 'y')
        return 'c';
    if (len == 4 && name[0] == 'a' && name[1] == 'p' &&
        name[2] == 'o' && name[3] == 's')
        return '\'';

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
        if (cp > 0 && cp < 128) return (unsigned char)cp;
        if (cp >= 128) {
            /* Feed through UTF-8 decoder mapping */
            utf8_decoder_t tmp;
            utf8_init(&tmp);
            if (cp < 0x100) {
                /* Direct Latin-1 mapping */
                unsigned char result = utf8_feed(&tmp, 0xC0 | (unsigned char)(cp >> 6));
                if (result == 0)
                    result = utf8_feed(&tmp, 0x80 | (unsigned char)(cp & 0x3F));
                return result ? result : '?';
            }
            return '?';  /* Higher codepoints: best effort */
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
