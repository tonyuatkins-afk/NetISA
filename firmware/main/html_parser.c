/**
 * html_parser.c - HTML to cell stream converter for Cathode browser
 *
 * Simple state machine parser. NOT a full HTML5 parser. Handles the
 * subset of HTML that matters for readable text content from real
 * websites: headings, paragraphs, links, lists, tables, preformatted.
 *
 * Output: stream of (char, attr, type, link_id) cells that the DOS
 * client displays directly into the VGA text buffer.
 */

#include "html_parser.h"
#include "esp_log.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

static const char *TAG = "html_parser";

/* CP437 character mappings */
#define CP437_BULLET    0xF9  /* middle dot */
#define CP437_HR        0xC4  /* horizontal bar */

void html_parser_init(html_parser_t *p, html_cell_t *cells, int max_cells,
                      html_link_t *links, int max_links)
{
    memset(p, 0, sizeof(*p));
    p->cells = cells;
    p->max_cells = max_cells;
    p->links = links;
    p->max_links = max_links;
    p->col = 0;
    p->row = 0;
    p->in_body = 0;
    p->heading_level = 0;
}

static void emit_cell(html_parser_t *p, char ch, uint8_t attr, uint8_t type,
                      uint16_t link_id)
{
    if (p->cell_count >= p->max_cells) return;

    p->cells[p->cell_count].ch = ch;
    p->cells[p->cell_count].attr = attr;
    p->cells[p->cell_count].type = type;
    p->cells[p->cell_count].link_id = link_id;
    p->cell_count++;
}

static void emit_newline(html_parser_t *p)
{
    /* Pad current line to end */
    uint8_t attr = ATTR_NORMAL;
    while (p->col < HTML_COLS) {
        emit_cell(p, ' ', attr, CELL_TEXT, 0);
        p->col++;
    }
    p->col = 0;
    p->row++;
}

static void emit_blank_line(html_parser_t *p)
{
    if (p->col > 0) emit_newline(p);
    emit_newline(p);
}

static uint8_t current_attr(html_parser_t *p)
{
    if (p->heading_level > 0) return ATTR_HEADING;
    if (p->in_link) return ATTR_LINK;
    if (p->bold) return ATTR_BOLD;
    if (p->in_pre) return ATTR_PRE;
    return ATTR_NORMAL;
}

static uint8_t current_type(html_parser_t *p)
{
    if (p->heading_level > 0) return CELL_HEADING;
    if (p->in_link) return CELL_LINK;
    if (p->in_pre) return CELL_PRE;
    return CELL_TEXT;
}

static uint16_t current_link(html_parser_t *p)
{
    if (p->in_link) return (uint16_t)p->current_link_id;
    return 0;
}

static void emit_char(html_parser_t *p, char ch)
{
    if (p->col >= HTML_COLS) {
        emit_newline(p);
    }
    emit_cell(p, ch, current_attr(p), current_type(p), current_link(p));
    p->col++;
}

static void emit_string(html_parser_t *p, const char *s)
{
    while (*s) {
        emit_char(p, *s);
        s++;
    }
}

/* Word-wrap aware text emission */
static void emit_text_char(html_parser_t *p, char ch)
{
    if (p->in_pre) {
        if (ch == '\n') {
            emit_newline(p);
        } else {
            emit_char(p, ch);
        }
        return;
    }

    /* Collapse whitespace */
    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
        /* Only emit space if not at start of line and last wasn't space */
        if (p->col > 0 && p->cell_count > 0 &&
            p->cells[p->cell_count - 1].ch != ' ') {
            emit_char(p, ' ');
        }
        return;
    }

    emit_char(p, ch);
}

/* Decode HTML entity, return decoded char or '?' */
static char decode_entity(const char *entity)
{
    if (strcmp(entity, "amp") == 0) return '&';
    if (strcmp(entity, "lt") == 0) return '<';
    if (strcmp(entity, "gt") == 0) return '>';
    if (strcmp(entity, "quot") == 0) return '"';
    if (strcmp(entity, "apos") == 0) return '\'';
    if (strcmp(entity, "nbsp") == 0) return ' ';
    if (strcmp(entity, "mdash") == 0) return '-';
    if (strcmp(entity, "ndash") == 0) return '-';
    if (strcmp(entity, "copy") == 0) return '(';  /* (c) approximation */
    if (strcmp(entity, "reg") == 0) return '(';   /* (R) approximation */

    /* Numeric entities &#NNN; or &#xHH; */
    if (entity[0] == '#') {
        int code;
        if (entity[1] == 'x' || entity[1] == 'X') {
            code = (int)strtol(entity + 2, NULL, 16);
        } else {
            code = atoi(entity + 1);
        }
        /* ASCII range */
        if (code >= 32 && code < 127) return (char)code;
        /* Common Unicode -> CP437 approximations */
        if (code == 8212 || code == 8211) return '-';   /* em/en dash */
        if (code == 8216 || code == 8217) return '\'';  /* smart quotes */
        if (code == 8220 || code == 8221) return '"';    /* smart double */
        if (code == 169) return '(';  /* copyright */
        if (code == 174) return '(';  /* registered */
        return '?';
    }

    return '?';
}

/* Case-insensitive tag name comparison */
static int tag_is(const char *tag_buf, const char *name)
{
    return strcasecmp(tag_buf, name) == 0;
}

/* Extract href attribute value from tag content: <a href="url" ...> */
static int extract_href(const char *tag_content, char *href, int max_len)
{
    const char *p = tag_content;
    const char *start;
    char quote;
    int len;

    /* Find href= (case insensitive) */
    while (*p) {
        if (strncasecmp(p, "href=", 5) == 0) {
            p += 5;
            /* Skip optional quote */
            if (*p == '"' || *p == '\'') {
                quote = *p;
                p++;
                start = p;
                while (*p && *p != quote) p++;
            } else {
                start = p;
                while (*p && *p != ' ' && *p != '>' && *p != '\t') p++;
            }
            len = (int)(p - start);
            if (len >= max_len) len = max_len - 1;
            memcpy(href, start, len);
            href[len] = '\0';
            return 1;
        }
        p++;
    }
    return 0;
}

/* Extract alt attribute for <img> tags */
static int extract_alt(const char *tag_content, char *alt, int max_len)
{
    const char *p = tag_content;
    const char *start;
    char quote;
    int len;

    while (*p) {
        if (strncasecmp(p, "alt=", 4) == 0) {
            p += 4;
            if (*p == '"' || *p == '\'') {
                quote = *p;
                p++;
                start = p;
                while (*p && *p != quote) p++;
            } else {
                start = p;
                while (*p && *p != ' ' && *p != '>' && *p != '\t') p++;
            }
            len = (int)(p - start);
            if (len >= max_len) len = max_len - 1;
            memcpy(alt, start, len);
            alt[len] = '\0';
            return 1;
        }
        p++;
    }
    return 0;
}

/* Parse tag name from tag buffer. Returns pointer past the name.
 * tag_buf contains everything between < and >, e.g. "a href=\"...\"" */
static const char *parse_tag_name(const char *tag_buf, char *name, int max_len)
{
    int i = 0;
    const char *p = tag_buf;

    /* Skip leading / for closing tags */
    if (*p == '/') p++;

    while (*p && *p != ' ' && *p != '\t' && *p != '>' && *p != '/' &&
           i < max_len - 1) {
        name[i++] = (char)tolower((unsigned char)*p);
        p++;
    }
    name[i] = '\0';
    return p;
}

static void process_tag(html_parser_t *p)
{
    char name[32];
    int is_closing;
    const char *rest;

    if (p->tag_len == 0) return;
    p->tag_buf[p->tag_len] = '\0';

    /* Skip comments */
    if (p->tag_len >= 3 && p->tag_buf[0] == '!' &&
        p->tag_buf[1] == '-' && p->tag_buf[2] == '-') {
        return;
    }

    /* Skip DOCTYPE */
    if (p->tag_buf[0] == '!') return;

    is_closing = (p->tag_buf[0] == '/');
    rest = parse_tag_name(p->tag_buf, name, sizeof(name));

    /* Structure tags */
    if (tag_is(name, "html") || tag_is(name, "head") || tag_is(name, "body")) {
        if (tag_is(name, "body")) {
            p->in_body = !is_closing;
        } else if (tag_is(name, "head")) {
            p->in_head = !is_closing;
        }
        return;
    }

    /* Title */
    if (tag_is(name, "title")) {
        p->in_title = !is_closing;
        if (is_closing) {
            p->title[p->title_len] = '\0';
        }
        return;
    }

    /* Skip script/style content */
    if (tag_is(name, "script")) {
        p->in_script = !is_closing;
        return;
    }
    if (tag_is(name, "style")) {
        p->in_style = !is_closing;
        return;
    }

    /* Only process visual tags inside body */
    if (!p->in_body && !is_closing) return;

    /* Headings */
    if (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && name[2] == '\0') {
        if (!is_closing) {
            emit_blank_line(p);
            p->heading_level = name[1] - '0';
        } else {
            p->heading_level = 0;
            emit_newline(p);
        }
        return;
    }

    /* Paragraph */
    if (tag_is(name, "p") || tag_is(name, "div")) {
        if (!is_closing) {
            if (p->col > 0 || p->row > 0) emit_blank_line(p);
        } else {
            if (p->col > 0) emit_newline(p);
        }
        return;
    }

    /* Line break */
    if (tag_is(name, "br")) {
        emit_newline(p);
        return;
    }

    /* Links */
    if (tag_is(name, "a")) {
        if (!is_closing) {
            char href[HTML_LINK_URL];
            if (extract_href(rest, href, sizeof(href)) &&
                p->link_count < p->max_links) {
                strncpy(p->links[p->link_count].url, href, HTML_LINK_URL - 1);
                p->links[p->link_count].url[HTML_LINK_URL - 1] = '\0';
                p->current_link_id = p->link_count;
                p->link_count++;
                p->in_link = 1;
            }
        } else {
            p->in_link = 0;
        }
        return;
    }

    /* Bold/strong */
    if (tag_is(name, "b") || tag_is(name, "strong")) {
        p->bold = !is_closing;
        return;
    }

    /* Italic/em (no visual change in text mode) */
    if (tag_is(name, "i") || tag_is(name, "em")) {
        p->italic = !is_closing;
        return;
    }

    /* Lists */
    if (tag_is(name, "ul") || tag_is(name, "ol")) {
        p->in_list = !is_closing;
        if (!is_closing && p->col > 0) emit_newline(p);
        return;
    }
    if (tag_is(name, "li")) {
        if (!is_closing) {
            if (p->col > 0) emit_newline(p);
            emit_string(p, "  ");
            emit_cell(p, CP437_BULLET, ATTR_BULLET, CELL_TEXT, 0);
            p->col++;
            emit_char(p, ' ');
        }
        return;
    }

    /* Tables */
    if (tag_is(name, "table")) {
        p->in_table = !is_closing;
        if (!is_closing && p->col > 0) emit_newline(p);
        return;
    }
    if (tag_is(name, "tr")) {
        if (!is_closing) {
            if (p->col > 0) emit_newline(p);
            p->in_td = 0;
        }
        return;
    }
    if (tag_is(name, "td") || tag_is(name, "th")) {
        if (!is_closing) {
            if (p->in_td) {
                emit_cell(p, ' ', ATTR_TABLE_BDR, CELL_TEXT, 0);
                p->col++;
                emit_cell(p, '|', ATTR_TABLE_BDR, CELL_TEXT, 0);
                p->col++;
                emit_cell(p, ' ', ATTR_TABLE_BDR, CELL_TEXT, 0);
                p->col++;
            }
            p->in_td = 1;
            if (tag_is(name, "th")) p->bold = 1;
        } else {
            if (tag_is(name, "th")) p->bold = 0;
        }
        return;
    }

    /* Preformatted */
    if (tag_is(name, "pre") || tag_is(name, "code")) {
        if (!is_closing) {
            if (tag_is(name, "pre") && p->col > 0) emit_newline(p);
            p->in_pre = !is_closing;
        } else {
            p->in_pre = 0;
        }
        return;
    }

    /* Horizontal rule */
    if (tag_is(name, "hr")) {
        if (p->col > 0) emit_newline(p);
        for (int i = 0; i < HTML_COLS && p->cell_count < p->max_cells; i++) {
            emit_cell(p, CP437_HR, ATTR_HR, CELL_TEXT, 0);
            p->col++;
        }
        emit_newline(p);
        return;
    }

    /* Images */
    if (tag_is(name, "img")) {
        char alt[64];
        emit_char(p, '[');
        if (extract_alt(rest, alt, sizeof(alt)) && strlen(alt) > 0) {
            emit_string(p, alt);
        } else {
            emit_string(p, "image");
        }
        emit_char(p, ']');
        return;
    }

    /* span, meta, link, etc: silently ignored */
}

int html_parse(html_parser_t *p, const char *html, int html_len)
{
    for (int i = 0; i < html_len && html[i]; i++) {
        char ch = html[i];

        /* Skip content inside <script> or <style> */
        if (p->in_script || p->in_style) {
            if (ch == '<') {
                p->in_tag = 1;
                p->tag_len = 0;
            } else if (p->in_tag) {
                if (ch == '>') {
                    p->in_tag = 0;
                    process_tag(p);
                } else if (p->tag_len < (int)sizeof(p->tag_buf) - 1) {
                    p->tag_buf[p->tag_len++] = ch;
                }
            }
            continue;
        }

        /* Entity handling */
        if (p->in_entity) {
            if (ch == ';') {
                p->in_entity = 0;
                p->entity_buf[p->entity_len] = '\0';
                char decoded = decode_entity(p->entity_buf);
                if (p->in_title) {
                    if (p->title_len < (int)sizeof(p->title) - 1) {
                        p->title[p->title_len++] = decoded;
                    }
                } else if (p->in_body) {
                    emit_text_char(p, decoded);
                }
            } else if (p->entity_len < (int)sizeof(p->entity_buf) - 1) {
                p->entity_buf[p->entity_len++] = ch;
            } else {
                /* Entity too long, emit as-is */
                p->in_entity = 0;
                if (p->in_body) {
                    emit_text_char(p, '&');
                    for (int j = 0; j < p->entity_len; j++) {
                        emit_text_char(p, p->entity_buf[j]);
                    }
                    emit_text_char(p, ch);
                }
            }
            continue;
        }

        /* Tag handling */
        if (p->in_tag) {
            if (ch == '>') {
                p->in_tag = 0;
                process_tag(p);
            } else if (p->tag_len < (int)sizeof(p->tag_buf) - 1) {
                p->tag_buf[p->tag_len++] = ch;
            }
            continue;
        }

        /* Start of tag */
        if (ch == '<') {
            p->in_tag = 1;
            p->tag_len = 0;
            continue;
        }

        /* Start of entity */
        if (ch == '&') {
            p->in_entity = 1;
            p->entity_len = 0;
            continue;
        }

        /* Title text */
        if (p->in_title) {
            if (p->title_len < (int)sizeof(p->title) - 1) {
                p->title[p->title_len++] = ch;
            }
            continue;
        }

        /* Body text */
        if (p->in_body) {
            emit_text_char(p, ch);
        }
    }

    /* Flush partial last line */
    if (p->col > 0) {
        while (p->col < HTML_COLS) {
            emit_cell(p, ' ', ATTR_NORMAL, CELL_TEXT, 0);
            p->col++;
        }
        p->row++;
    }

    ESP_LOGI(TAG, "Parsed: %d rows, %d cells, %d links, title: %s",
             p->row, p->cell_count, p->link_count, p->title);

    return p->row;
}
