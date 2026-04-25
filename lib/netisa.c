/*
 * netisa.c - INT 63h API C wrappers (real hardware implementation)
 *
 * Each function dispatches to the NetISA TSR via int86() or int86x().
 * Link this file for builds targeting real NetISA hardware. For DOSBox-X
 * testing without hardware, link netisa_stub.c instead.
 *
 * Watcom V2's inline assembler trips on several patterns the original
 * code used (MASM-reserved operator names like MIN/LEN, register-name
 * collisions like SP, cross-block label scoping). int86()/int86x() is
 * the suite-app pattern (chime/ uses it identically) and is immune to
 * those quirks.
 */

#include "netisa.h"
#include <i86.h>
#include <string.h>

int ni_detect(ni_version_t *ver)
{
    union REGS r;
    r.h.ah = NI_GRP_SYSTEM;
    r.h.al = NI_SYS_NOP;
    int86(NI_INT_VECTOR, &r, &r);
    if (r.x.cflag & 0x01)
        return 0;
    if (r.x.ax != NI_SIGNATURE)
        return 0;
    if (ver) {
        ver->major = r.h.bh;
        ver->minor = r.h.bl;
        ver->patch = r.h.ch;
    }
    return 1;
}

int ni_card_status(ni_card_status_t *status)
{
    union REGS r;

    r.h.ah = NI_GRP_SYSTEM;
    r.h.al = NI_SYS_STATUS;
    int86(NI_INT_VECTOR, &r, &r);
    if (r.x.cflag & 0x01)
        return r.x.ax;

    if (status) {
        status->status_flags = r.h.al;
        status->active_sessions = r.h.ah;
        status->max_sessions = r.h.bl;
    }

    /* Network status. CF set means "not connected"; treat as zeroed
     * fallback rather than a hard failure. */
    r.h.ah = NI_GRP_SYSTEM;
    r.h.al = NI_SYS_NETSTATUS;
    int86(NI_INT_VECTOR, &r, &r);
    if (status) {
        if (r.x.cflag & 0x01) {
            status->net_status = 0;
            status->signal_pct = 0;
        } else {
            status->net_status = r.h.al;
            status->signal_pct = r.h.ah;
        }
    }

    return NI_OK;
}

int ni_fw_version(ni_version_t *ver)
{
    union REGS r;
    r.h.ah = NI_GRP_SYSTEM;
    r.h.al = NI_SYS_FWVERSION;
    int86(NI_INT_VECTOR, &r, &r);
    if (r.x.cflag & 0x01)
        return r.x.ax;
    if (ver) {
        ver->major = r.h.bh;
        ver->minor = r.h.bl;
        ver->patch = r.h.ch;
    }
    return NI_OK;
}

int ni_card_reset(void)
{
    union REGS r;
    r.h.ah = NI_GRP_SYSTEM;
    r.h.al = NI_SYS_RESET;
    int86(NI_INT_VECTOR, &r, &r);
    if (r.x.cflag & 0x01)
        return r.x.ax;
    return NI_OK;
}

int ni_wifi_scan(ni_wifi_network_t *list, int max_networks)
{
    union REGS r;
    struct SREGS s;
    uint16_t bufsz;

    bufsz = (uint16_t)(max_networks * sizeof(ni_wifi_network_t));

    segread(&s);
    s.es = FP_SEG((ni_wifi_network_t far *)list);
    r.x.di = FP_OFF((ni_wifi_network_t far *)list);
    r.x.cx = bufsz;
    r.h.ah = NI_GRP_NETCFG;
    r.h.al = NI_NET_SCAN;
    int86x(NI_INT_VECTOR, &r, &r, &s);
    if (r.x.cflag & 0x01)
        return -(int)r.x.ax;
    return (int)r.x.ax;
}

int ni_wifi_connect(const char *ssid, const char *password)
{
    union REGS r;
    struct SREGS s;

    /* Set SSID */
    segread(&s);
    s.ds = FP_SEG((const char far *)ssid);
    r.x.si = FP_OFF((const char far *)ssid);
    r.h.ah = NI_GRP_NETCFG;
    r.h.al = NI_NET_SET_SSID;
    int86x(NI_INT_VECTOR, &r, &r, &s);
    if (r.x.cflag & 0x01)
        return r.x.ax;

    /* Set password */
    segread(&s);
    s.ds = FP_SEG((const char far *)password);
    r.x.si = FP_OFF((const char far *)password);
    r.h.ah = NI_GRP_NETCFG;
    r.h.al = NI_NET_SET_PASS;
    int86x(NI_INT_VECTOR, &r, &r, &s);
    if (r.x.cflag & 0x01)
        return r.x.ax;

    /* Connect */
    r.h.ah = NI_GRP_NETCFG;
    r.h.al = NI_NET_CONNECT;
    int86(NI_INT_VECTOR, &r, &r);
    if (r.x.cflag & 0x01)
        return r.x.ax;

    return NI_OK;
}

int ni_wifi_status(ni_wifi_status_t *status)
{
    /* Network status comes from 00/03; the WiFi info would come from
     * card registers. v1.0 reports just what the API provides. */
    union REGS r;
    r.h.ah = NI_GRP_SYSTEM;
    r.h.al = NI_SYS_NETSTATUS;
    int86(NI_INT_VECTOR, &r, &r);
    if (r.x.cflag & 0x01)
        return r.x.ax;

    if (status) {
        uint8_t net = r.h.al;
        uint8_t sig = r.h.ah;
        status->connected = (net == NI_NETSTAT_CONNECTED) ? 1 : 0;
        status->rssi = -(int8_t)(100 - sig);  /* approximate dBm from % */
    }
    return NI_OK;
}

int ni_wifi_disconnect(void)
{
    union REGS r;
    r.h.ah = NI_GRP_NETCFG;
    r.h.al = NI_NET_DISCONNECT;
    int86(NI_INT_VECTOR, &r, &r);
    if (r.x.cflag & 0x01)
        return r.x.ax;
    return NI_OK;
}

int ni_session_open(const char *hostname, uint16_t port, uint8_t *handle)
{
    union REGS r;
    struct SREGS s;
    segread(&s);
    s.ds = FP_SEG((const char far *)hostname);
    r.x.si = FP_OFF((const char far *)hostname);
    r.x.bx = port;
    r.h.ah = NI_GRP_SESSION;
    r.h.al = NI_SESS_OPEN;
    int86x(NI_INT_VECTOR, &r, &r, &s);
    if (r.x.cflag & 0x01)
        return r.x.ax;
    if (handle)
        *handle = r.h.al;
    return NI_OK;
}

int ni_session_close(uint8_t handle)
{
    union REGS r;
    r.h.bl = handle;
    r.h.ah = NI_GRP_SESSION;
    r.h.al = NI_SESS_CLOSE;
    int86(NI_INT_VECTOR, &r, &r);
    if (r.x.cflag & 0x01)
        return r.x.ax;
    return NI_OK;
}

int ni_session_send(uint8_t handle, const char far *buf, uint16_t len)
{
    union REGS r;
    struct SREGS s;
    segread(&s);
    s.ds = FP_SEG(buf);
    r.x.si = FP_OFF(buf);
    r.x.cx = len;
    r.h.bl = handle;
    r.h.ah = NI_GRP_SESSION;
    r.h.al = NI_SESS_SEND;
    int86x(NI_INT_VECTOR, &r, &r, &s);
    if (r.x.cflag & 0x01)
        return r.x.ax;
    return NI_OK;
}

int ni_session_recv(uint8_t handle, char far *buf, uint16_t bufsize,
                    uint16_t *bytes_read)
{
    union REGS r;
    struct SREGS s;
    segread(&s);
    s.es = FP_SEG(buf);
    r.x.di = FP_OFF(buf);
    r.x.cx = bufsize;
    r.h.bl = handle;
    r.h.ah = NI_GRP_SESSION;
    r.h.al = NI_SESS_RECV;
    int86x(NI_INT_VECTOR, &r, &r, &s);
    if (r.x.cflag & 0x01) {
        if (bytes_read) *bytes_read = 0;
        return r.x.ax;
    }
    if (bytes_read) *bytes_read = r.x.ax;
    return NI_OK;
}

int ni_rng_get(uint8_t *buf, uint16_t len)
{
    union REGS r;
    struct SREGS s;
    segread(&s);
    s.es = FP_SEG((uint8_t far *)buf);
    r.x.di = FP_OFF((uint8_t far *)buf);
    r.x.cx = len;
    r.h.ah = NI_GRP_CRYPTO;
    r.h.al = NI_CRYPTO_RANDOM;
    int86x(NI_INT_VECTOR, &r, &r, &s);
    if (r.x.cflag & 0x01)
        return r.x.ax;
    return NI_OK;
}

int ni_diag_uptime(uint32_t *seconds)
{
    union REGS r;
    r.h.ah = NI_GRP_DIAG;
    r.h.al = NI_DIAG_UPTIME;
    int86(NI_INT_VECTOR, &r, &r);
    if (r.x.cflag & 0x01)
        return r.x.ax;
    if (seconds)
        *seconds = ((uint32_t)r.x.dx << 16) | r.x.ax;
    return NI_OK;
}
