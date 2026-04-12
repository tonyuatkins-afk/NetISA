/*
 * stub_pages.h - Test page declarations for DOSBox-X testing
 */

#ifndef STUB_PAGES_H
#define STUB_PAGES_H

#include "page.h"

/* Fetch a stub page by URL. Returns 0 on success, -1 if not found. */
int stub_fetch_page(const char *url, page_buffer_t *page);

#endif /* STUB_PAGES_H */
