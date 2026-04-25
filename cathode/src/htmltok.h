/*
 * htmltok.h - HTML tokenizer and parser interface for Cathode browser
 *
 * Streaming character-by-character state machine. Drives htmlout.c
 * for layout emission. Shared parser state struct used by all html* modules.
 */

#ifndef HTMLTOK_H
#define HTMLTOK_H

#include "page.h"
#include "utf8.h"
#include "cathode_cfg.h"

/* Parser state machine states */
#define PS_TEXT         0
#define PS_TAG_OPEN     1
#define PS_TAG_NAME     2
#define PS_TAG_ATTRS    3
#define PS_TAG_DONE     4
#define PS_ENTITY       5
#define PS_COMMENT      6

/* Attribute parsing sub-states */
#define AS_SPACE        0
#define AS_NAME         1
#define AS_EQ           2
#define AS_VAL_START    3
#define AS_VAL_QUOTED   4
#define AS_VAL_UNQUOTED 5

/* Buffer sizes */
#define HTML_MAX_TAG     16
#define HTML_MAX_ATTR    128
#define HTML_STYLE_DEPTH 8

/* Style types for the stack */
#define STYLE_NONE      0
#define STYLE_BOLD      1
#define STYLE_ITALIC    2
#define STYLE_CODE      3
#define STYLE_LINK      4
#define STYLE_HEADING   5
#define STYLE_UNDERLINE 6

/* The shared parser state structure (~1150 bytes, must be static) */
typedef struct {
    /* State machine */
    unsigned char state;
    unsigned char in_head;
    unsigned char in_pre;
    unsigned char in_script;
    unsigned char in_style;
    unsigned char in_title;

    /* Tag accumulation */
    char tag_name[HTML_MAX_TAG];
    int tag_len;
    int tag_is_close;

    /* Attribute accumulation */
    char attr_name[HTML_MAX_TAG];
    char attr_val[HTML_MAX_ATTR];
    int attr_name_len;
    int attr_val_len;
    unsigned char attr_state;
    char attr_quote;

    /* Saved attributes — captured as each attribute completes */
    char saved_href[HTML_MAX_ATTR];
    char saved_alt[64];
    char saved_src[HTML_MAX_ATTR];
    char saved_type[HTML_MAX_TAG];
    char saved_name[HTML_MAX_TAG];
    char saved_value[HTML_MAX_ATTR];
    char saved_action[HTML_MAX_ATTR];
    char saved_method[HTML_MAX_TAG];
    char saved_size[HTML_MAX_TAG];

    /* Output cursor */
    int row, col;
    int indent;
    int line_has_content;

    /* Style stack */
    unsigned char style_stack[HTML_STYLE_DEPTH];
    int style_depth;
    unsigned char current_attr;

    /* Link state */
    char link_href[HTML_MAX_ATTR];
    int link_start_row;
    int link_start_col;
    int in_link;

    /* Form state */
    char form_action[HTML_MAX_ATTR];
    int form_id;

    /* List state */
    int list_depth;
    int list_ordered[4];
    int list_counter[4];

    /* Entity accumulation */
    char entity_buf[12];
    int entity_len;

    /* Comment tracking */
    int comment_dashes;

    /* UTF-8 decoder */
    utf8_decoder_t utf8;

    /* Target page buffer */
    page_buffer_t *page;

    /* Truncation flag */
    int truncated;

    /* Word wrap buffer for word-boundary wrapping */
    char word_buf[80];
    unsigned char word_attr;
    int word_len;
    int word_is_link;
    unsigned short word_link_id;
} html_parser_t;

/* Public interface */
void html_init(html_parser_t *p, page_buffer_t *page);
void html_parse_chunk(html_parser_t *p, const char far *data, int len);
void html_finish(html_parser_t *p);

#endif /* HTMLTOK_H */
