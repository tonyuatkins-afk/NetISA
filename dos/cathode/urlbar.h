/*
 * urlbar.h - URL bar types for Cathode browser
 */

#ifndef URLBAR_H
#define URLBAR_H

#define URL_MAX_LEN 255

typedef struct {
    char buf[URL_MAX_LEN + 1];
    int len;
    int cursor;
    int editing;
} urlbar_t;

void urlbar_init(urlbar_t *u);
void urlbar_set(urlbar_t *u, const char *url);
void urlbar_start_edit(urlbar_t *u);
void urlbar_cancel_edit(urlbar_t *u, const char *restore_url);
int  urlbar_handle_key(urlbar_t *u, int key);  /* returns 1 if Enter pressed */

#endif /* URLBAR_H */
