/*
 * bookmark.h - Bookmark persistence for Cathode browser
 */

#ifndef BOOKMARK_H
#define BOOKMARK_H

#define BM_MAX  32
#define BM_FILE "CATHODE.BMK"

/* Add current URL to bookmark file. Returns 0 on success. */
int  bookmark_add(const char *url);

/* Count bookmarks in file. */
int  bookmark_count(void);

/* Build HTML for the about:bookmarks page.
   Returns far pointer to dynamically allocated HTML buffer.
   Caller must free with _ffree() after parsing is complete. */
char far *bookmark_build_html(void);

#endif /* BOOKMARK_H */
