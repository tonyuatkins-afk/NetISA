/*
 * url.h - URL parsing, resolution, and encoding for Cathode browser
 */

#ifndef URL_H
#define URL_H

#define URL_MAX 255

typedef struct {
    char scheme[8];       /* "https", "http", "about" */
    char host[128];
    unsigned int port;    /* 443 default for https, 80 for http */
    char path[128];
} url_parts_t;

/* Parse URL into components. Returns 0 on success, -1 on error. */
int  url_parse(const char *url, url_parts_t *parts);

/* Resolve relative URL against base. Result written to buf (URL_MAX+1). */
void url_resolve(const char *base_url, const char *relative,
                 char *result, int result_max);

/* URL-encode input for query strings. Space->+, special->%XX. */
void url_encode(const char *input, char *output, int max_len);

#endif /* URL_H */
