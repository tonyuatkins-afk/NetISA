/*
 * bookmark.c - Bookmark persistence for Cathode browser
 *
 * Stores bookmarks in CATHODE.BMK, one URL per line.
 * Generates HTML for the about:bookmarks page via _fmalloc buffer.
 */

#include "bookmark.h"
#include "url.h"
#include <stdio.h>
#include <string.h>
#include <malloc.h>

int bookmark_add(const char *url)
{
    FILE *fp;

    if (!url || !url[0]) return -1;

    /* Check count first */
    if (bookmark_count() >= BM_MAX) return -1;

    fp = fopen(BM_FILE, "a");
    if (!fp) return -1;

    if (fprintf(fp, "%s\n", url) < 0) {
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

int bookmark_count(void)
{
    FILE *fp;
    char buf[URL_MAX + 2];
    int count = 0;

    fp = fopen(BM_FILE, "r");
    if (!fp) return 0;

    while (fgets(buf, URL_MAX + 2, fp) && count < BM_MAX)
        count++;

    fclose(fp);
    return count;
}

#define BM_BUF_SIZE 20480  /* 20KB buffer for bookmark HTML */

static const char far bm_fallback[] =
    "<html><head><title>Bookmarks</title></head><body>"
    "<h1>Bookmarks</h1>"
    "<p>Not enough memory to display bookmarks.</p>"
    "<hr><p><a href=\"about:home\">Back to Start Page</a></p>"
    "</body></html>";

static const char far bm_empty[] =
    "<html><head><title>Bookmarks</title></head><body>"
    "<h1>Bookmarks</h1>"
    "<p>No bookmarks saved yet. Press Ctrl+D to bookmark a page.</p>"
    "<hr><p><a href=\"about:home\">Back to Start Page</a></p>"
    "</body></html>";

/* Helper: append a near string to a far buffer */
static void far_append(char far *buf, int *pos, const char *str, int max)
{
    while (*str && *pos < max - 1) {
        buf[*pos] = *str;
        (*pos)++;
        str++;
    }
}

/* Helper: append a near string with HTML escaping */
static void far_append_esc(char far *buf, int *pos, const char *str, int max)
{
    while (*str && *pos < max - 10) {
        if (*str == '&') { far_append(buf, pos, "&amp;", max); }
        else if (*str == '<') { far_append(buf, pos, "&lt;", max); }
        else if (*str == '>') { far_append(buf, pos, "&gt;", max); }
        else if (*str == '"') { far_append(buf, pos, "&quot;", max); }
        else { buf[*pos] = *str; (*pos)++; }
        str++;
    }
}

char far *bookmark_build_html(void)
{
    FILE *fp;
    char far *buf;
    int pos = 0;
    char line[URL_MAX + 2];
    int count = 0;

    fp = fopen(BM_FILE, "r");
    if (!fp) return (char far *)0;  /* NULL signals to use bm_empty */

    buf = (char far *)_fmalloc(BM_BUF_SIZE);
    if (!buf) {
        fclose(fp);
        return (char far *)0;
    }

    far_append(buf, &pos,
        "<html><head><title>Bookmarks</title></head><body>"
        "<h1>Bookmarks</h1><ul>", BM_BUF_SIZE);

    while (fgets(line, URL_MAX + 2, fp) && count < BM_MAX) {
        int len = (int)strlen(line);
        /* Strip trailing newline */
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
        if (len > 0 && line[len - 1] == '\r') line[--len] = '\0';
        if (len == 0) continue;

        far_append(buf, &pos, "<li><a href=\"", BM_BUF_SIZE);
        far_append_esc(buf, &pos, line, BM_BUF_SIZE);
        far_append(buf, &pos, "\">", BM_BUF_SIZE);
        far_append_esc(buf, &pos, line, BM_BUF_SIZE);
        far_append(buf, &pos, "</a></li>", BM_BUF_SIZE);
        count++;
    }

    fclose(fp);

    far_append(buf, &pos,
        "</ul><hr>"
        "<p><a href=\"about:home\">Back to Start Page</a></p>"
        "</body></html>", BM_BUF_SIZE);

    buf[pos] = '\0';
    return buf;
}
