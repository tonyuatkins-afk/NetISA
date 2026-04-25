/*
 * netisa_api.c - CHIME's NetISA INT 63h wrappers + stub backend.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Two backends, switched at runtime:
 *   1. Real: thin INT 63h wrappers. Compiled when not HEARO_NOASM. Uses the
 *      same calling conventions as dos/lib/netisa.c so a future shared lib
 *      can collapse the duplication.
 *   2. Stub: synthetic responses driven from a small playbook table.
 *      Activated via na_use_stub(CTRUE), normally driven from /STUBNET on the
 *      command line. Lets CHIME exercise the time-source pipeline on a
 *      workstation with no card on the bus.
 */
#include "netisa_api.h"
#include <string.h>
#include <stdio.h>

static cbool stub_active = CFALSE;

void  na_use_stub(cbool yes) { stub_active = yes ? CTRUE : CFALSE; }
cbool na_is_stub(void)       { return stub_active; }

const char *na_strerror(int code)
{
    switch (code) {
        case NA_OK:              return "ok";
        case NA_NOT_PRESENT:     return "NetISA card not detected";
        case NA_NOT_READY:       return "NetISA card not ready (network down?)";
        case NA_DNS_FAILED:      return "DNS resolution failed";
        case NA_CONNECT_FAILED:  return "TCP connect failed";
        case NA_TLS_HANDSHAKE:   return "TLS handshake failed";
        case NA_TIMEOUT:         return "operation timed out";
        case NA_BUFFER_SMALL:    return "receive buffer too small";
        case NA_INVALID_HANDLE:  return "invalid session handle";
        case NA_DISCONNECTED:    return "session disconnected";
        default:                 return "unknown error";
    }
}

/* ---- Stub backend ---- */

#define STUB_MAX_HANDLES 4
#define STUB_BUF_SIZE    1024

typedef struct {
    cbool open;
    char  hostname[64];
    u16   port;
    cbool tls;
    /* Canned response for the next recv; populated by send when it sees an
     * HTTP request. */
    char  response[STUB_BUF_SIZE];
    u16   response_len;
    u16   response_pos;
} stub_session_t;

static stub_session_t stub_sessions[STUB_MAX_HANDLES];

static int stub_detect(na_card_info_t *out)
{
    if (out) {
        out->major = 1; out->minor = 0; out->patch = 0;
        out->ip[0] = 192; out->ip[1] = 168; out->ip[2] = 1; out->ip[3] = 42;
        strcpy(out->fw_string, "stub-1.0.0");
        out->ready = CTRUE;
    }
    return NA_OK;
}

static int stub_session_open(const char *hostname, u16 port, cbool tls, na_handle_t *handle)
{
    u8 i;
    if (!handle) return NA_UNKNOWN;
    for (i = 0; i < STUB_MAX_HANDLES; i++) {
        if (!stub_sessions[i].open) {
            stub_sessions[i].open = CTRUE;
            strncpy(stub_sessions[i].hostname, hostname, sizeof(stub_sessions[i].hostname) - 1);
            stub_sessions[i].hostname[sizeof(stub_sessions[i].hostname) - 1] = '\0';
            stub_sessions[i].port = port;
            stub_sessions[i].tls = tls;
            stub_sessions[i].response[0] = '\0';
            stub_sessions[i].response_len = 0;
            stub_sessions[i].response_pos = 0;
            *handle = (na_handle_t)i;
            return NA_OK;
        }
    }
    return NA_UNKNOWN;
}

static void stub_canned_response(stub_session_t *s, const char *request)
{
    /* Two flavors based on the request: HEAD returns a Date header, GET to
     * the worldtimeapi path returns a small JSON body. The synthesised
     * timestamp is a fixed mid-day in 2026-04-25 so test runs are
     * deterministic; testtime.c relies on this. */
    if (request && (strstr(request, "HEAD ") == request)) {
        sprintf(s->response,
                "HTTP/1.1 200 OK\r\n"
                "Date: Sat, 25 Apr 2026 14:30:42 GMT\r\n"
                "Server: stub/1.0\r\n"
                "Content-Length: 0\r\n"
                "Connection: close\r\n"
                "\r\n");
    } else if (request && (strstr(request, "GET ") == request) &&
               strstr(request, "/api/timezone/Etc/UTC")) {
        const char *body = "{\"unixtime\":1777127442,\"utc_datetime\":\"2026-04-25T14:30:42+00:00\"}";
        sprintf(s->response,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: %u\r\n"
                "Connection: close\r\n"
                "\r\n%s",
                (unsigned)strlen(body), body);
    } else {
        strcpy(s->response,
               "HTTP/1.1 404 Not Found\r\n"
               "Content-Length: 0\r\n"
               "\r\n");
    }
    s->response_len = (u16)strlen(s->response);
    s->response_pos = 0;
}

static int stub_session_send(na_handle_t h, const char *buf, u16 len)
{
    stub_session_t *s;
    if (h >= STUB_MAX_HANDLES) return NA_INVALID_HANDLE;
    s = &stub_sessions[h];
    if (!s->open) return NA_INVALID_HANDLE;
    {
        char request[256];
        u16 copy = len < (sizeof(request) - 1) ? len : (u16)(sizeof(request) - 1);
        memcpy(request, buf, copy);
        request[copy] = '\0';
        stub_canned_response(s, request);
    }
    return NA_OK;
}

static int stub_session_recv(na_handle_t h, char *buf, u16 bufsize, u16 *bytes_read)
{
    stub_session_t *s;
    u16 remaining;
    u16 take;
    if (h >= STUB_MAX_HANDLES) return NA_INVALID_HANDLE;
    s = &stub_sessions[h];
    if (!s->open) return NA_INVALID_HANDLE;
    remaining = (u16)(s->response_len - s->response_pos);
    take = remaining < bufsize ? remaining : bufsize;
    if (take > 0) memcpy(buf, s->response + s->response_pos, take);
    s->response_pos = (u16)(s->response_pos + take);
    if (bytes_read) *bytes_read = take;
    return NA_OK;
}

static int stub_session_close(na_handle_t h)
{
    if (h >= STUB_MAX_HANDLES) return NA_INVALID_HANDLE;
    stub_sessions[h].open = CFALSE;
    return NA_OK;
}

/* ---- Real backend (thin INT 63h wrappers) ---- */

#ifndef HEARO_NOASM
#include <i86.h>
#include <dos.h>

static int real_detect(na_card_info_t *out)
{
    union REGS r;
    r.h.ah = 0x00; r.h.al = 0x00;
    int86(0x63, &r, &r);
    if ((r.x.cflag & 0x01) || r.x.ax != 0x4352) return NA_NOT_PRESENT;

    /* Fetch firmware version (0x00/0x05) */
    if (out) {
        memset(out, 0, sizeof(*out));
        r.h.ah = 0x00; r.h.al = 0x05;
        int86(0x63, &r, &r);
        if (!(r.x.cflag & 0x01)) {
            out->major = r.h.ch;
            out->minor = r.h.cl;
            out->patch = r.h.bh;
            sprintf(out->fw_string, "%u.%u.%u", out->major, out->minor, out->patch);
        } else {
            strcpy(out->fw_string, "?");
        }
        /* IP address (0x00/0x07) */
        r.h.ah = 0x00; r.h.al = 0x07;
        int86(0x63, &r, &r);
        if (!(r.x.cflag & 0x01)) {
            out->ip[0] = r.h.ch; out->ip[1] = r.h.cl;
            out->ip[2] = r.h.dh; out->ip[3] = r.h.dl;
        }
        /* Network status (0x00/0x03): AX = NI_NETSTAT_*. */
        r.h.ah = 0x00; r.h.al = 0x03;
        int86(0x63, &r, &r);
        out->ready = (!(r.x.cflag & 0x01) && r.x.ax == 2) ? CTRUE : CFALSE;
    }
    return NA_OK;
}

static int real_session_open(const char *hostname, u16 port, cbool tls, na_handle_t *handle)
{
    union REGS r;
    struct SREGS s;
    if (!hostname || !handle) return NA_UNKNOWN;
    segread(&s);
    s.ds = FP_SEG(hostname);
    r.x.si = FP_OFF(hostname);
    r.x.cx = port;
    r.h.ah = 0x03;
    r.h.al = tls ? 0x00 : 0x07;
    int86x(0x63, &r, &r, &s);
    if (r.x.cflag & 0x01) {
        switch (r.x.ax) {
            case 0x0004: return NA_DNS_FAILED;
            case 0x0005: return NA_CONNECT_FAILED;
            case 0x0006: return NA_TLS_HANDSHAKE;
            case 0x000A: return NA_TIMEOUT;
            default:     return NA_UNKNOWN;
        }
    }
    *handle = (na_handle_t)r.h.bl;
    return NA_OK;
}

static int real_session_send(na_handle_t handle, const char *buf, u16 len)
{
    union REGS r;
    struct SREGS s;
    segread(&s);
    s.ds = FP_SEG(buf);
    r.x.si = FP_OFF(buf);
    r.x.cx = len;
    r.h.bl = handle;
    r.h.ah = 0x03; r.h.al = 0x03;
    int86x(0x63, &r, &r, &s);
    if (r.x.cflag & 0x01) return NA_DISCONNECTED;
    return NA_OK;
}

static int real_session_recv(na_handle_t handle, char *buf, u16 bufsize, u16 *bytes_read)
{
    union REGS r;
    struct SREGS s;
    segread(&s);
    s.es = FP_SEG(buf);
    r.x.di = FP_OFF(buf);
    r.x.cx = bufsize;
    r.h.bl = handle;
    r.h.ah = 0x03; r.h.al = 0x04;
    int86x(0x63, &r, &r, &s);
    if (r.x.cflag & 0x01) return NA_DISCONNECTED;
    if (bytes_read) *bytes_read = r.x.cx;
    return NA_OK;
}

static int real_session_close(na_handle_t handle)
{
    union REGS r;
    r.h.bl = handle;
    r.h.ah = 0x03; r.h.al = 0x02;
    int86(0x63, &r, &r);
    return NA_OK;
}
#else
/* Host build: real backend forwards to stub. The Watcom build of CHIME does
 * not pass HEARO_NOASM, so this branch only runs on developer workstations
 * with an ordinary C compiler. */
static int real_detect(na_card_info_t *o)                                          { return stub_detect(o); }
static int real_session_open(const char *h, u16 p, cbool t, na_handle_t *hd)       { return stub_session_open(h, p, t, hd); }
static int real_session_send(na_handle_t h, const char *b, u16 l)                  { return stub_session_send(h, b, l); }
static int real_session_recv(na_handle_t h, char *b, u16 sz, u16 *n)               { return stub_session_recv(h, b, sz, n); }
static int real_session_close(na_handle_t h)                                       { return stub_session_close(h); }
#endif

/* ---- Public API: dispatch to stub or real ---- */

int na_detect(na_card_info_t *out)
{
    return stub_active ? stub_detect(out) : real_detect(out);
}

int na_session_open(const char *hostname, u16 port, cbool tls, na_handle_t *handle)
{
    return stub_active ? stub_session_open(hostname, port, tls, handle)
                       : real_session_open(hostname, port, tls, handle);
}

int na_session_send(na_handle_t h, const char *buf, u16 len)
{
    return stub_active ? stub_session_send(h, buf, len)
                       : real_session_send(h, buf, len);
}

int na_session_recv(na_handle_t h, char *buf, u16 bufsize, u16 *bytes_read)
{
    return stub_active ? stub_session_recv(h, buf, bufsize, bytes_read)
                       : real_session_recv(h, buf, bufsize, bytes_read);
}

int na_session_close(na_handle_t h)
{
    return stub_active ? stub_session_close(h)
                       : real_session_close(h);
}
