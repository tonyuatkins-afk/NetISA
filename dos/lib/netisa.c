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
    uint16_t sig;
    uint8_t maj, min, pat;

    _asm {
        mov ah, NI_GRP_SYSTEM
        mov al, NI_SYS_NOP
        int NI_INT_VECTOR
        jc  _fail
        mov sig, ax
        mov maj, bh
        mov min, bl
        mov pat, ch
        jmp _ok
    _fail:
        mov sig, 0
    _ok:
    }

    if (sig != NI_SIGNATURE)
        return 0;

    if (ver) {
        ver->major = maj;
        ver->minor = min;
        ver->patch = pat;
    }
    return 1;
}

int ni_card_status(ni_card_status_t *status)
{
    uint16_t err;
    uint8_t flags, sessions, maxsess;

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

    /* Get network status separately */
    {
        uint8_t ns, sp;
        _asm {
            mov ah, NI_GRP_SYSTEM
            mov al, NI_SYS_NETSTATUS
            int NI_INT_VECTOR
            jc  _nfail
            mov ns, al
            mov sp, ah
            jmp _nok
        _nfail:
            mov ns, 0
            mov sp, 0
        _nok:
        }
        if (status) {
            status->net_status = ns;
            status->signal_pct = sp;
        }
    }

    return NI_OK;
}

int ni_fw_version(ni_version_t *ver)
{
    uint16_t err;
    uint8_t maj, min, pat;

    _asm {
        mov ah, NI_GRP_SYSTEM
        mov al, NI_SYS_FWVERSION
        int NI_INT_VECTOR
        jc  _fail
        mov maj, bh
        mov min, bl
        mov pat, ch
        xor ax, ax
    _fail:
        mov err, ax
    }

    if (err != NI_OK)
        return err;

    if (ver) {
        ver->major = maj;
        ver->minor = min;
        ver->patch = pat;
    }
    return NI_OK;
}

int ni_card_reset(void)
{
    uint16_t err;
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
    uint16_t err;
    uint16_t count;
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
    uint16_t err;

    /* Set SSID (small model: DS already correct, use near pointer) */
    _asm {
        mov ah, NI_GRP_NETCFG
        mov al, NI_NET_SET_SSID
        mov si, ssid
        int NI_INT_VECTOR
        jc  _fail
    }

    /* Set password */
    _asm {
        mov ah, NI_GRP_NETCFG
        mov al, NI_NET_SET_PASS
        mov si, password
        int NI_INT_VECTOR
        jc  _fail
    }

    /* Connect */
    _asm {
        mov ah, NI_GRP_NETCFG
        mov al, NI_NET_CONNECT
        int NI_INT_VECTOR
        jc  _fail
        xor ax, ax
    _fail:
        mov err, ax
    }

    return err;
}

int ni_wifi_status(ni_wifi_status_t *status)
{
    /* Network status comes from 00/03 (net status) and the WiFi info
     * would come from card registers. For now, use what the API provides. */
    uint16_t err;
    uint8_t ns, sp;

    _asm {
        mov ah, NI_GRP_SYSTEM
        mov al, NI_SYS_NETSTATUS
        int NI_INT_VECTOR
        jc  _fail
        mov ns, al
        mov sp, ah
        xor ax, ax
    _fail:
        mov err, ax
    }

    if (err != NI_OK)
        return err;

    if (status) {
        status->connected = (ns == NI_NETSTAT_CONNECTED) ? 1 : 0;
        status->rssi = -(int8_t)(100 - sp);  /* approximate dBm from % */
    }

    return NI_OK;
}

int ni_wifi_disconnect(void)
{
    uint16_t err;
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
    uint16_t err;
    uint8_t h;

    _asm {
        mov ah, NI_GRP_SESSION
        mov al, NI_SESS_OPEN
        mov si, hostname
        mov bx, port
        int NI_INT_VECTOR
        jc  _fail
        mov h, al
        xor ax, ax
    _fail:
        mov err, ax
    }

    if (err == NI_OK && handle)
        *handle = h;

    return err;
}

int ni_session_close(uint8_t handle)
{
    uint16_t err;
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
    uint16_t err;
    _asm {
        push ds
        push si
        mov ah, NI_GRP_SESSION
        mov al, NI_SESS_SEND
        mov bl, handle
        lds si, buf
        mov cx, len
        int NI_INT_VECTOR
        pop si
        pop ds
        jc  _fail
        xor ax, ax
    _fail:
        mov err, ax
    }
    return err;
}

int ni_session_recv(uint8_t handle, char far *buf, uint16_t bufsize,
                    uint16_t *bytes_read)
{
    uint16_t err;
    uint16_t got;
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

int ni_rng_get(uint8_t *buf, uint16_t len)
{
    uint16_t err;
    _asm {
        push es
        push di
        mov ah, NI_GRP_CRYPTO
        mov al, NI_CRYPTO_RANDOM
        push ds
        pop es
        mov di, buf
        mov cx, len
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
    uint16_t err;
    uint16_t lo, hi;
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
