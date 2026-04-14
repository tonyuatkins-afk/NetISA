/*
 * stub_pages.h - Stub page provider for Cathode browser
 *
 * Returns HTML strings for about: URLs.
 * Also provides the procedural CATHODE block-art logo.
 */

#ifndef STUB_PAGES_H
#define STUB_PAGES_H

#include "page.h"

/* Returns far pointer to HTML string for the given about: URL,
   or NULL if URL is not recognized. */
const char far *stub_get_html(const char *url);

/* Write the CP437 block-art CATHODE logo to page rows 1-6.
   Must be called before the parser begins emitting content. */
void build_home_logo(page_buffer_t *page);

/* Legacy API — kept for compatibility during migration */
int stub_fetch_page(const char *url, page_buffer_t *page);

#endif /* STUB_PAGES_H */
