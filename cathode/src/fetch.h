/*
 * fetch.h - HTTP/1.0 fetch layer for Cathode browser
 *
 * Single entry point for all URL fetching. Routes about: URLs to
 * stub_pages.c, http/https URLs to NetISA INT 63h API.
 */

#ifndef FETCH_H
#define FETCH_H

#include "url.h"

#define FETCH_OK        0
#define FETCH_ERR_DNS   1
#define FETCH_ERR_CONN  2
#define FETCH_ERR_HTTP  3
#define FETCH_ERR_REDIR 4
#define FETCH_CANCELLED 5

typedef struct {
    int status_code;
    long content_length;    /* -1 if unknown */
    int is_utf8;
    long bytes_received;
    char redirect_url[URL_MAX + 1];
} fetch_state_t;

/* Callback invoked with each chunk of HTML body data */
typedef void (*fetch_cb_t)(const char far *chunk, int len, void *userdata);

/* Fetch a URL. Routes internally based on scheme.
   Returns FETCH_OK on success, error code otherwise. */
int fetch_page(const char *url, fetch_cb_t callback,
               void *userdata, fetch_state_t *state);

#endif /* FETCH_H */
