/*
 * netisa_stub.c - Stub implementation returning fake data
 *
 * Allows testing the full UI in DOSBox-X without NetISA hardware.
 * Link this file instead of netisa.c for DOSBox-X testing.
 */

#include "netisa.h"
#include <string.h>
#include <stdlib.h>
#include <dos.h>

/* Fake state */
static int  stub_connected = 0;
static char stub_ssid[33] = "";

int ni_detect(ni_version_t *ver)
{
    /* Stub always reports TSR present */
    if (ver) {
        ver->major = 1;
        ver->minor = 0;
        ver->patch = 1;
    }
    return 1;
}

int ni_card_status(ni_card_status_t *status)
{
    if (status) {
        status->status_flags = 0x41;  /* CMD_READY + BOOT_COMPLETE */
        status->active_sessions = 0;
        status->max_sessions = 4;
        status->net_status = stub_connected ? NI_NETSTAT_CONNECTED
                                            : NI_NETSTAT_DISCONNECTED;
        status->signal_pct = stub_connected ? 78 : 0;
    }
    return NI_OK;
}

int ni_fw_version(ni_version_t *ver)
{
    if (ver) {
        ver->major = 1;
        ver->minor = 0;
        ver->patch = 1;
    }
    return NI_OK;
}

int ni_card_reset(void)
{
    stub_connected = 0;
    stub_ssid[0] = '\0';
    return NI_OK;
}

int ni_wifi_scan(ni_wifi_network_t *list, int max_networks)
{
    static const struct {
        const char *ssid;
        int8_t      rssi;
        uint8_t     security;
        uint8_t     channel;
    } fake_nets[] = {
        { "HomeNetwork_5G",   -42, NI_WIFI_WPA3,  36 },
        { "NETGEAR-Guest",    -55, NI_WIFI_WPA2,   6 },
        { "xfinitywifi",      -62, NI_WIFI_OPEN,  11 },
        { "TP-Link_2.4G",     -70, NI_WIFI_WPA2,   1 },
        { "DIRECT-roku-123",  -78, NI_WIFI_WPA2, 149 }
    };
    int count = 5;
    int i;

    if (count > max_networks)
        count = max_networks;

    for (i = 0; i < count; i++) {
        memset(&list[i], 0, sizeof(ni_wifi_network_t));
        strncpy(list[i].ssid, fake_nets[i].ssid, 32);
        list[i].ssid[32] = '\0';
        list[i].rssi = fake_nets[i].rssi;
        list[i].security = fake_nets[i].security;
        list[i].channel = fake_nets[i].channel;
    }

    return count;
}

int ni_wifi_connect(const char *ssid, const char *password)
{
    /* Simulate a brief connection delay */
    unsigned long start;
    (void)password;

    _asm {
        xor ax, ax
        int 1Ah
        mov word ptr start, dx
        mov word ptr start+2, cx
    }

    /* Busy wait ~1 second (18.2 ticks) */
    {
        unsigned long now;
        for (;;) {
            _asm {
                xor ax, ax
                int 1Ah
                mov word ptr now, dx
                mov word ptr now+2, cx
            }
            if ((now - start) >= 18)
                break;
        }
    }

    strncpy(stub_ssid, ssid, 32);
    stub_ssid[32] = '\0';
    stub_connected = 1;
    return NI_OK;
}

int ni_wifi_status(ni_wifi_status_t *status)
{
    if (status) {
        status->connected = (uint8_t)stub_connected;
        if (stub_connected) {
            strncpy(status->ssid, stub_ssid, 32);
            status->ssid[32] = '\0';
            status->ip[0] = 192;
            status->ip[1] = 168;
            status->ip[2] = 1;
            status->ip[3] = 47;
            status->rssi = -42;
            status->channel = 36;
        } else {
            status->ssid[0] = '\0';
            memset(status->ip, 0, 4);
            status->rssi = 0;
            status->channel = 0;
        }
    }
    return NI_OK;
}

int ni_wifi_disconnect(void)
{
    stub_connected = 0;
    stub_ssid[0] = '\0';
    return NI_OK;
}

int ni_session_open(const char *hostname, uint16_t port, uint8_t *handle)
{
    (void)hostname;
    (void)port;
    if (handle) *handle = 0;
    return NI_OK;
}

int ni_session_close(uint8_t handle)
{
    (void)handle;
    return NI_OK;
}

int ni_session_send(uint8_t handle, const char far *buf, uint16_t len)
{
    (void)handle;
    (void)buf;
    (void)len;
    return NI_OK;
}

int ni_session_recv(uint8_t handle, char far *buf, uint16_t bufsize,
                    uint16_t *bytes_read)
{
    (void)handle;
    (void)buf;
    (void)bufsize;
    if (bytes_read) *bytes_read = 0;
    return NI_OK;
}

int ni_rng_get(uint8_t *buf, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++)
        buf[i] = (uint8_t)(rand() & 0xFF);
    return NI_OK;
}

int ni_diag_uptime(uint32_t *seconds)
{
    /* Return approximate uptime from BIOS tick counter */
    unsigned long ticks;
    _asm {
        xor ax, ax
        int 1Ah
        mov word ptr ticks, dx
        mov word ptr ticks+2, cx
    }
    if (seconds)
        *seconds = ticks / 18;  /* ~18.2 ticks per second */
    return NI_OK;
}
