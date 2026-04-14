/*
 * htmlout.h - HTML layout emitter for Cathode browser
 *
 * Handles text emission, word wrapping, style stack, link tracking,
 * and tag dispatch for block/inline elements.
 */

#ifndef HTMLOUT_H
#define HTMLOUT_H

#include "htmltok.h"

/* Called by htmltok.c when text content is ready */
void html_emit_char(html_parser_t *p, char ch);
void html_emit_newline(html_parser_t *p);
void html_emit_blank_line(html_parser_t *p);
void html_flush_word(html_parser_t *p);

/* Tag dispatch — called by htmltok.c on tag open/close */
void html_dispatch_tag(html_parser_t *p);

/* Style stack */
void html_push_style(html_parser_t *p, unsigned char style_type);
void html_pop_style(html_parser_t *p);
void html_recompute_attr(html_parser_t *p);

#endif /* HTMLOUT_H */
