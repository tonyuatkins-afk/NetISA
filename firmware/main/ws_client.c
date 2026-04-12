/**
 * ws_client.c - WebSocket client stub for v1
 *
 * All functions return NI_ERR_NOT_IMPLEMENTED.
 * WebSocket support reserved for v2 (Discord Gateway, etc).
 */

#include "ws_client.h"
#include "status.h"

int ws_connect(const char *url)
{
    (void)url;
    return -NI_ERR_NOT_IMPLEMENTED;
}

int ws_send(int handle, const char *data, int len)
{
    (void)handle;
    (void)data;
    (void)len;
    return -NI_ERR_NOT_IMPLEMENTED;
}

int ws_recv(int handle, char *buf, int bufsize)
{
    (void)handle;
    (void)buf;
    (void)bufsize;
    return -NI_ERR_NOT_IMPLEMENTED;
}

int ws_close(int handle)
{
    (void)handle;
    return -NI_ERR_NOT_IMPLEMENTED;
}
