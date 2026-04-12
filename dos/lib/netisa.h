/*
 * netisa.h - NetISA INT 63h API definition
 *
 * Defines function numbers, error codes, structures, and C wrappers
 * for the NetISA INT 63h software interrupt API.
 *
 * Authoritative reference: docs/netisa-architecture-spec.md Section 4.
 *
 * Calling convention:
 *   AH = function group
 *   AL = function number within group
 *   Other registers as specified per function
 *   Returns: CF clear on success, CF set on error, AX = error code
 */

#ifndef NETISA_H
#define NETISA_H

typedef unsigned char  uint8_t;
typedef signed char    int8_t;
typedef unsigned short uint16_t;
typedef signed short   int16_t;
typedef unsigned long  uint32_t;
typedef signed long    int32_t;

/* ===== INT vector ===== */
#define NI_INT_VECTOR   0x63

/* ===== Presence check signature ===== */
#define NI_SIGNATURE    0x4352  /* 'CR' returned in AX from 00/00 */

/* ===== Function groups (AH register) ===== */
#define NI_GRP_SYSTEM   0x00
#define NI_GRP_NETCFG   0x01
#define NI_GRP_DNS      0x02
#define NI_GRP_SESSION  0x03
#define NI_GRP_CERT     0x04
#define NI_GRP_CRYPTO   0x05
#define NI_GRP_EVENT    0x06
#define NI_GRP_DIAG     0x07

/* ===== Function numbers (AL register) ===== */

/* Group 0x00: System */
#define NI_SYS_NOP              0x00  /* Presence check */
#define NI_SYS_STATUS           0x01  /* Get card status */
#define NI_SYS_RESET            0x02  /* Reset card */
#define NI_SYS_NETSTATUS        0x03  /* Get network status */
#define NI_SYS_ERRDETAIL        0x04  /* Get error detail */
#define NI_SYS_FWVERSION        0x05  /* Get firmware version */
#define NI_SYS_MACADDR          0x06  /* Get MAC address */
#define NI_SYS_IPADDR           0x07  /* Get IP address */

/* Group 0x01: Network Configuration */
#define NI_NET_SET_SSID         0x00  /* Set WiFi SSID */
#define NI_NET_SET_PASS         0x01  /* Set WiFi password */
#define NI_NET_CONNECT          0x02  /* Connect WiFi */
#define NI_NET_DISCONNECT       0x03  /* Disconnect WiFi */
#define NI_NET_SCAN             0x04  /* Scan WiFi networks */
#define NI_NET_STATIC_IP        0x05  /* Set static IP */
#define NI_NET_DHCP             0x06  /* Enable DHCP */
#define NI_NET_SET_DNS          0x07  /* Set DNS server */
#define NI_NET_SAVE_CFG         0x08  /* Save config to SD */
#define NI_NET_LOAD_CFG         0x09  /* Load config from SD */
#define NI_NET_SELECT_IF        0x10  /* Select interface */

/* Group 0x02: DNS */
#define NI_DNS_RESOLVE4         0x00  /* Resolve hostname (IPv4) */
#define NI_DNS_RESOLVE6         0x01  /* Resolve hostname (IPv6) */
#define NI_DNS_SETMODE          0x02  /* Set DNS mode */

/* Group 0x03: TLS Sessions */
#define NI_SESS_OPEN            0x00  /* Open TLS session (hostname) */
#define NI_SESS_OPEN_IP         0x01  /* Open TLS session (IP) */
#define NI_SESS_CLOSE           0x02  /* Close session */
#define NI_SESS_SEND            0x03  /* Send data */
#define NI_SESS_RECV            0x04  /* Receive data */
#define NI_SESS_STATUS          0x05  /* Get session status */
#define NI_SESS_SETOPT          0x06  /* Set session option */
#define NI_SESS_OPEN_PLAIN      0x07  /* Open plaintext session */

/* Group 0x05: Raw Crypto */
#define NI_CRYPTO_SHA256        0x00
#define NI_CRYPTO_RANDOM        0x0B

/* Group 0x07: Diagnostics */
#define NI_DIAG_UPTIME          0x00
#define NI_DIAG_MEMINFO         0x01
#define NI_DIAG_LOOPBACK        0x06

/* ===== Error codes (returned in AX when CF set) ===== */
#define NI_OK                   0x0000
#define NI_ERR_NOT_READY        0x0001
#define NI_ERR_INVALID_HANDLE   0x0002
#define NI_ERR_NO_SESSIONS      0x0003
#define NI_ERR_DNS_FAILED       0x0004
#define NI_ERR_CONNECT_FAILED   0x0005
#define NI_ERR_TLS_HANDSHAKE    0x0006
#define NI_ERR_CERT_INVALID     0x0007
#define NI_ERR_CERT_EXPIRED     0x0008
#define NI_ERR_CERT_HOSTNAME    0x0009
#define NI_ERR_TIMEOUT          0x000A
#define NI_ERR_DISCONNECTED     0x000B
#define NI_ERR_BUFFER_SMALL     0x000C
#define NI_ERR_NETWORK_DOWN     0x000D
#define NI_ERR_WIFI_AUTH        0x000E
#define NI_ERR_INVALID_PARAM    0x000F
#define NI_ERR_SD_NOT_FOUND     0x0010
#define NI_ERR_SD_READ          0x0011
#define NI_ERR_SD_WRITE         0x0012
#define NI_ERR_CRYPTO_FAILED    0x0013
#define NI_ERR_NOT_IMPLEMENTED  0x0014
#define NI_ERR_MODE_UNSUPPORTED 0x0015
#define NI_ERR_UNKNOWN          0x00FF

/* ===== Network status values ===== */
#define NI_NETSTAT_DISCONNECTED 0
#define NI_NETSTAT_CONNECTING   1
#define NI_NETSTAT_CONNECTED    2

/* ===== Session states ===== */
#define NI_SESS_CLOSED          0x00
#define NI_SESS_DNS_RESOLVING   0x01
#define NI_SESS_TCP_CONNECTING  0x02
#define NI_SESS_TLS_HANDSHAKE   0x03
#define NI_SESS_ESTABLISHED     0x04
#define NI_SESS_CLOSING         0x05
#define NI_SESS_ERROR           0x06

/* ===== WiFi security types ===== */
#define NI_WIFI_OPEN            0
#define NI_WIFI_WPA2            1
#define NI_WIFI_WPA3            2
#define NI_WIFI_WEP             3

/* ===== Structures ===== */

/* WiFi scan result entry (per-network) */
typedef struct {
    char    ssid[33];       /* SSID string, null-terminated */
    int8_t  rssi;           /* Signal strength in dBm */
    uint8_t security;       /* NI_WIFI_* constant */
    uint8_t channel;        /* WiFi channel number */
    uint8_t pad;            /* Alignment */
} ni_wifi_network_t;

/* WiFi connection status */
typedef struct {
    uint8_t  connected;     /* 1=connected, 0=not */
    char     ssid[33];      /* Connected SSID */
    uint8_t  ip[4];         /* IPv4 address */
    int8_t   rssi;          /* Current signal strength dBm */
    uint8_t  channel;       /* Current channel */
} ni_wifi_status_t;

/* Firmware version */
typedef struct {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
} ni_version_t;

/* Card status */
typedef struct {
    uint8_t status_flags;   /* Status register bits */
    uint8_t active_sessions;
    uint8_t max_sessions;
    uint8_t net_status;     /* NI_NETSTAT_* */
    uint8_t signal_pct;     /* Signal strength 0-100 */
} ni_card_status_t;

/* ===== C wrapper function prototypes ===== */

/* Returns 1 if TSR is loaded, 0 if not. Fills ver if non-NULL. */
int ni_detect(ni_version_t *ver);

/* System functions */
int ni_card_status(ni_card_status_t *status);
int ni_fw_version(ni_version_t *ver);
int ni_card_reset(void);

/* WiFi functions */
int ni_wifi_scan(ni_wifi_network_t *list, int max_networks);
int ni_wifi_connect(const char *ssid, const char *password);
int ni_wifi_status(ni_wifi_status_t *status);
int ni_wifi_disconnect(void);

/* Session functions */
int ni_session_open(const char *hostname, uint16_t port, uint8_t *handle);
int ni_session_close(uint8_t handle);
int ni_session_send(uint8_t handle, const char far *buf, uint16_t len);
int ni_session_recv(uint8_t handle, char far *buf, uint16_t bufsize,
                    uint16_t *bytes_read);

/* Crypto functions */
int ni_rng_get(uint8_t *buf, uint16_t len);

/* Diagnostics */
int ni_diag_uptime(uint32_t *seconds);

#endif /* NETISA_H */
