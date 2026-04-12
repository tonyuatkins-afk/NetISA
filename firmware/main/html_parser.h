/**
 * html_parser.h - HTML to cell stream converter for Cathode browser
 *
 * Simple state machine parser that converts HTML to a stream of
 * (char, attr, type, link_id) cells for direct VGA text buffer display.
 */

#ifndef HTML_PARSER_H
#define HTML_PARSER_H

#include <stdint.h>

/* Cell types */
#define CELL_TEXT       0
#define CELL_LINK       1
#define CELL_HEADING    2
#define CELL_PRE        3

/* VGA text mode attributes (match DOS screen.h) */
#define ATTR_NORMAL     0x07    /* Light gray on black */
#define ATTR_HEADING    0x0F    /* White on black */
#define ATTR_LINK       0x0B    /* Light cyan on black */
#define ATTR_BOLD       0x0F    /* White on black */
#define ATTR_PRE        0x0A    /* Light green on black */
#define ATTR_TABLE_BDR  0x02    /* Green on black */
#define ATTR_HR         0x08    /* Dark gray on black */
#define ATTR_BULLET     0x02    /* Green on black */

#define HTML_COLS       80
#define HTML_MAX_LINKS  128
#define HTML_LINK_URL   256

typedef struct {
    char ch;
    uint8_t attr;
    uint8_t type;
    uint16_t link_id;
} html_cell_t;

typedef struct {
    char url[HTML_LINK_URL];
} html_link_t;

typedef struct {
    html_cell_t *cells;
    int cell_count;
    int max_cells;
    html_link_t *links;
    int link_count;
    int max_links;
    int col;
    int row;
    char title[80];

    /* Parser state */
    int in_tag;
    int in_body;
    int in_head;
    int in_title;
    int in_script;
    int in_style;
    int in_pre;
    int bold;
    int italic;
    int in_link;
    int current_link_id;
    int heading_level;
    int in_list;
    int in_table;
    int in_td;

    /* Tag accumulation buffer */
    char tag_buf[512];
    int tag_len;

    /* Entity accumulation */
    char entity_buf[16];
    int entity_len;
    int in_entity;

    /* Title accumulation */
    int title_len;
} html_parser_t;

void html_parser_init(html_parser_t *p, html_cell_t *cells, int max_cells,
                      html_link_t *links, int max_links);
int  html_parse(html_parser_t *p, const char *html, int html_len);

#endif /* HTML_PARSER_H */
