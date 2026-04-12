/**
 * ws_client.h - WebSocket client (stub for v1)
 *
 * WebSocket support reserved for v2 (Discord Gateway, etc).
 * All functions return NI_ERR_NOT_IMPLEMENTED.
 */

#ifndef WS_CLIENT_H
#define WS_CLIENT_H

#include <stdint.h>

int ws_connect(const char *url);
int ws_send(int handle, const char *data, int len);
int ws_recv(int handle, char *buf, int bufsize);
int ws_close(int handle);

#endif /* WS_CLIENT_H */
