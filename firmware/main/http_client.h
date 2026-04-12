/**
 * http_client.h - HTTPS GET/POST with session management
 *
 * Manages up to 4 concurrent TLS sessions using esp_http_client.
 */

#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stdint.h>

void http_client_init(void);
int  http_open_get(const char *url);
int  http_open_post(const char *url, const char *body, int body_len);
int  http_read(int session_id, char *buf, int bufsize);
int  http_get_status(int session_id);
void http_close(int session_id);
int  http_get_active_count(void);

#endif /* HTTP_CLIENT_H */
