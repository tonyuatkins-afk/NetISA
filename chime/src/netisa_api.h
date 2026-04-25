/*
 * netisa_api.h - CHIME's subset of the NetISA INT 63h API.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * The full NetISA API lives in dos/lib/netisa.h. CHIME only uses a slice:
 *   - presence check
 *   - card status (so we can show firmware version + IP)
 *   - TLS or plaintext session: open, send, receive, close
 *
 * We re-declare only those wrappers here so chime/ links cleanly without
 * pulling in the full TSR client code. The implementation calls the same
 * INT 63h vector that the suite-wide netisa.c uses.
 *
 * /STUBNET activates a synthetic backend that reports a present card and
 * returns canned HTTP responses, so the boot screen and time-source
 * pipeline can be exercised without a real card.
 */
#ifndef CHIME_NETISA_API_H
#define CHIME_NETISA_API_H

#include "chime.h"

#define NA_OK                   0
#define NA_NOT_PRESENT          1
#define NA_NOT_READY            2
#define NA_DNS_FAILED           3
#define NA_CONNECT_FAILED       4
#define NA_TLS_HANDSHAKE        5
#define NA_TIMEOUT              6
#define NA_BUFFER_SMALL         7
#define NA_INVALID_HANDLE       8
#define NA_DISCONNECTED         9
#define NA_UNKNOWN             99

typedef u8 na_handle_t;

typedef struct {
    u8  major, minor, patch;
    u8  ip[4];
    char fw_string[16];
    cbool ready;
} na_card_info_t;

void  na_use_stub(cbool yes);
cbool na_is_stub(void);

int  na_detect(na_card_info_t *out);

/* Open a session. tls=1 for HTTPS, tls=0 for plain HTTP. */
int  na_session_open(const char *hostname, u16 port, cbool tls, na_handle_t *handle);

int  na_session_send(na_handle_t handle, const char *buf, u16 len);
int  na_session_recv(na_handle_t handle, char *buf, u16 bufsize, u16 *bytes_read);
int  na_session_close(na_handle_t handle);

/* Map NA_* code to a printable string. */
const char *na_strerror(int code);

#endif
