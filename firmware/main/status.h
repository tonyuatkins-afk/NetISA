/**
 * status.h - Shared status/error codes matching DOS netisa.h
 *
 * These codes are returned to the ISA bus and must match the DOS-side
 * definitions in lib/netisa.h exactly.
 */

#ifndef STATUS_H
#define STATUS_H

/* Success */
#define NI_OK                   0x00

/* General errors */
#define NI_ERR_NOT_READY        0x01
#define NI_ERR_INVALID_HANDLE   0x02
#define NI_ERR_NO_SESSIONS      0x03
#define NI_ERR_TIMEOUT          0x0A
#define NI_ERR_BUFFER_SMALL     0x0C
#define NI_ERR_INVALID_PARAM    0x0F
#define NI_ERR_NOT_IMPLEMENTED  0x14
#define NI_ERR_MODE_UNSUPPORTED 0x15
#define NI_ERR_UNKNOWN          0xFF

/* Network errors */
#define NI_ERR_DNS_FAILED       0x04
#define NI_ERR_CONNECT_FAILED   0x05
#define NI_ERR_TLS_HANDSHAKE    0x06
#define NI_ERR_CERT_INVALID     0x07
#define NI_ERR_CERT_EXPIRED     0x08
#define NI_ERR_CERT_HOSTNAME    0x09
#define NI_ERR_DISCONNECTED     0x0B
#define NI_ERR_NETWORK_DOWN     0x0D
#define NI_ERR_WIFI_AUTH        0x0E
#define NI_ERR_CRYPTO_FAILED    0x13

/* Storage errors */
#define NI_ERR_SD_NOT_FOUND     0x10
#define NI_ERR_SD_READ          0x11
#define NI_ERR_SD_WRITE         0x12

/* Network status values */
#define NI_NETSTAT_DISCONNECTED 0
#define NI_NETSTAT_CONNECTING   1
#define NI_NETSTAT_CONNECTED    2

/* Session states */
#define NI_SESS_ST_CLOSED       0x00
#define NI_SESS_ST_DNS          0x01
#define NI_SESS_ST_TCP          0x02
#define NI_SESS_ST_TLS          0x03
#define NI_SESS_ST_ESTABLISHED  0x04
#define NI_SESS_ST_CLOSING      0x05
#define NI_SESS_ST_ERROR        0x06

/* Firmware version */
#define FW_VERSION_MAJOR        1
#define FW_VERSION_MINOR        0
#define FW_VERSION_PATCH        0

/* Presence check signature */
#define NI_SIGNATURE            0x4352  /* 'CR' */

/* Session limits */
#define MAX_SESSIONS            4

#endif /* STATUS_H */
