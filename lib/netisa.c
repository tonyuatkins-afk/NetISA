/*
 * netisa.c - INT 63h API C wrappers (real hardware implementation)
 *
 * Each function uses inline assembly to invoke INT 63h.
 * Link this file for builds targeting real NetISA hardware.
 * For DOSBox-X testing without hardware, link netisa_stub.c instead.
 */

#include "netisa.h"
#include <i86.h>
#include <string.h>

int ni_detect(ni_version_t *ver)
{
    uint16_t sig = 0;
    uint8_t vmaj = 0, vmin = 0, vpat = 0;

    /* MASM-reserved 'min' avoided; BYTE PTR forces 8-bit destination so
     * Watcom V2's inline assembler doesn't promote the locals to 16-bit. */
    _asm {
        mov ah, NI_GRP_SYSTEM
        mov al, NI_SYS_NOP
        int NI_INT_VECTOR
        jc  _fail
        mov sig, ax
        mov BYTE PTR vmaj, bh
        mov BYTE PTR vmin, bl
        mov BYTE PTR vpat, ch
        jmp _ok
    _fail:
        mov sig, 0
    _ok:
    }

    if (sig != NI_SIGNATURE)
        return 0;

    if (ver) {
        ver->major = vmaj;
        ver->minor = vmin;
        ver->patch = vpat;
    }
    return 1;
}

int ni_card_status(ni_card_status_t *status)
{
    uint16_t err = 0;
    uint8_t flags = 0, sessions = 0, maxsess = 0;

    _asm {
        mov ah, NI_GRP_SYSTEM
        mov al, NI_SYS_STATUS
        int NI_INT_VECTOR
        jc  _fail
        mov flags, al
        mov sessions, ah
        mov maxsess, bl
        xor ax, ax
    _fail:
        mov err, ax
    }

    if (err != NI_OK)
        return err;

    if (status) {
        status->status_flags = flags;
        status->active_sessions = sessions;
        status->max_sessions = maxsess;
    }

    /* Get network status separately. 'sp' renamed to 'vsig' because SP
     * is the stack-pointer register and Watcom's inline asm parser
     * resolves the identifier as the register, breaking size matching. */
    {
        uint8_t vns = 0, vsig = 0;
        /* NETSTATUS failure is expected when the card is present but not
         * connected to a network.  On CF set, zeroed values correctly
         * represent "not connected", which is the intentional fallback. */
        _asm {
            mov ah, NI_GRP_SYSTEM
            mov al, NI_SYS_NETSTATUS
            int NI_INT_VECTOR
            jc  _nfail
            mov vns, al
            mov vsig, ah
            jmp _nok
        _nfail:
            mov vns, 0
            mov vsig, 0
        _nok:
        }
        if (status) {
            status->net_status = vns;
            status->signal_pct = vsig;
        }
    }

    return NI_OK;
}

int ni_fw_version(ni_version_t *ver)
{
    /* Watcom V2's inline asm rejected mov-byte-to-byte-local in this
     * function (E1156). int86() avoids the issue and matches the chime/
     * suite-app pattern. */
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
    uint16_t err = 0;
    _asm {
        mov ah, NI_GRP_SYSTEM
        mov al, NI_SYS_RESET
        int NI_INT_VECTOR
        jc  _fail
        xor ax, ax
    _fail:
        mov err, ax
    }
    return err;
}

int ni_wifi_scan(ni_wifi_network_t *list, int max_networks)
{
    uint16_t err = 0;
    uint16_t count = 0;
    uint16_t bufsz;

    bufsz = (uint16_t)(max_networks * sizeof(ni_wifi_network_t));

    _asm {
        push es
        push di
        mov ah, NI_GRP_NETCFG
        mov al, NI_NET_SCAN
        push ds
        pop es
        mov di, list
        mov cx, bufsz
        int NI_INT_VECTOR
        jc  _fail
        mov count, ax
        xor ax, ax
        jmp _done
    _fail:
        mov count, 0
    _done:
        mov err, ax
        pop di
        pop es
    }

    if (err != NI_OK)
        return -(int)err;

    return (int)count;
}

int ni_wifi_connect(const char *ssid, const char *password)
{
    /* Originally three chained _asm blocks branched to a shared _fail
     * label in the third block. Watcom V2 scopes labels per asm block,
     * so the cross-block branch failed to resolve and corrupted the
     * parser state for following functions. int86x() per call is
     * straightforward and matches the chime/ pattern. */
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
    /* Network status comes from 00/03 (net status) and the WiFi info
     * would come from card registers. For now, use what the API provides.
     * 'sp' renamed to 'vsig' to avoid the SP-register collision. */
    uint16_t err = 0;
    uint8_t vns = 0, vsig = 0;

    _asm {
        mov ah, NI_GRP_SYSTEM
        mov al, NI_SYS_NETSTATUS
        int NI_INT_VECTOR
        jc  _fail
        mov vns, al
        mov vsig, ah
        xor ax, ax
    _fail:
        mov err, ax
    }

    if (err != NI_OK)
        return err;

    if (status) {
        status->connected = (vns == NI_NETSTAT_CONNECTED) ? 1 : 0;
        status->rssi = -(int8_t)(100 - vsig);  /* approximate dBm from % */
    }

    return NI_OK;
}

int ni_wifi_disconnect(void)
{
    uint16_t err = 0;
    _asm {
        mov ah, NI_GRP_NETCFG
        mov al, NI_NET_DISCONNECT
        int NI_INT_VECTOR
        jc  _fail
        xor ax, ax
    _fail:
        mov err, ax
    }
    return err;
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
    uint16_t err = 0;
    _asm {
        mov ah, NI_GRP_SESSION
        mov al, NI_SESS_CLOSE
        mov bl, handle
        int NI_INT_VECTOR
        jc  _fail
        xor ax, ax
    _fail:
        mov err, ax
    }
    return err;
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
    uint16_t err = 0;
    uint16_t got = 0;
    _asm {
        push es
        push di
        mov ah, NI_GRP_SESSION
        mov al, NI_SESS_RECV
        mov bl, handle
        les di, buf
        mov cx, bufsize
        int NI_INT_VECTOR
        pop di
        pop es
        jc  _fail
        mov got, ax
        xor ax, ax
        jmp _done
    _fail:
        mov got, 0
    _done:
        mov err, ax
    }

    if (bytes_read)
        *bytes_read = got;

    return err;
}

int ni_rng_get(uint8_t *buf, uint16_t nlen)
{
    /* 'len' renamed to 'nlen' for the same MASM-reserved-operator reason. */
    uint16_t err = 0;
    _asm {
        push es
        push di
        mov ah, NI_GRP_CRYPTO
        mov al, NI_CRYPTO_RANDOM
        push ds
        pop es
        mov di, buf
        mov cx, nlen
        int NI_INT_VECTOR
        pop di
        pop es
        jc  _fail
        xor ax, ax
    _fail:
        mov err, ax
    }
    return err;
}

int ni_diag_uptime(uint32_t *seconds)
{
    uint16_t err = 0;
    uint16_t lo = 0, hi = 0;
    _asm {
        mov ah, NI_GRP_DIAG
        mov al, NI_DIAG_UPTIME
        int NI_INT_VECTOR
        jc  _fail
        mov lo, ax
        mov hi, dx
        xor ax, ax
    _fail:
        mov err, ax
    }

    if (err == NI_OK && seconds)
        *seconds = ((uint32_t)hi << 16) | lo;

    return err;
}
