/*
 * chime.h - CHIME global types and constants.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef CHIME_H
#define CHIME_H

#define CHIME_VER_MAJOR   1
#define CHIME_VER_MINOR   0
#define CHIME_VER_PATCH   0
#define CHIME_VER_STRING  "1.0.0"
#define CHIME_COPYRIGHT   "(c) 2026 Tony Atkins, MIT License"

/* C89 portable types */
typedef unsigned char  u8;   typedef signed char  s8;
typedef unsigned short u16;  typedef signed short s16;
typedef unsigned long  u32;  typedef signed long  s32;
typedef int cbool;
#define CTRUE  1
#define CFALSE 0

/* Universal time as broken-down components. The struct mirrors `struct tm` so
 * the platform/dos_clock helpers can move data in or out without an
 * intermediate format. We carry a u32 unix_ts (seconds since 1970-01-01 UTC)
 * alongside so callers do not need to reconstruct it from the components. */
typedef struct {
    u16 year;       /* 1980..2099 (DOS-addressable range) */
    u8  month;      /* 1..12 */
    u8  day;        /* 1..31 */
    u8  hour;       /* 0..23 */
    u8  minute;     /* 0..59 */
    u8  second;     /* 0..59 */
    u8  pad;        /* alignment */
    u32 unix_ts;    /* seconds since 1970-01-01T00:00:00 UTC */
} chime_time_t;

/* Time-source mode and result. */
typedef enum {
    TS_AUTO = 0,        /* try sources in order until one responds */
    TS_HTTPS_HEAD,      /* HTTPS HEAD, parse Date: header */
    TS_HTTPS_JSON,      /* HTTPS GET, parse JSON unixtime field */
    TS_HTTP_HEAD,       /* plain HTTP HEAD, parse Date: header */
    TS_STUB             /* synthetic, /STUBNET tests */
} chime_source_mode_t;

typedef enum {
    TR_OK = 0,
    TR_NETISA_NOT_READY,
    TR_DNS_FAILED,
    TR_CONNECT_FAILED,
    TR_TLS_HANDSHAKE,
    TR_HTTP_BAD_STATUS,
    TR_NO_DATE_HEADER,
    TR_PARSE_FAILED,
    TR_TIMEOUT,
    TR_BUFFER_SMALL,
    TR_UNKNOWN
} chime_result_t;

/* Configured run state, populated once at startup from cmdline + CHIME.CFG. */
typedef struct {
    char    server[64];        /* hostname (no scheme, no path) */
    char    path[64];          /* URL path used in HEAD/GET, default "/" */
    u16     port;              /* 443 default for https, 80 for plain */
    chime_source_mode_t mode;
    s16     tz_offset_minutes; /* -1440..+1440, signed */
    cbool   auto_write;        /* /AUTO */
    cbool   dry_run;           /* /DRYRUN */
    cbool   quiet;             /* /QUIET */
    cbool   stub_net;          /* /STUBNET */
    cbool   safe;              /* /SAFE */
    cbool   verbose;           /* /VERBOSE */
} chime_config_t;

/* UI color attributes (mirrored from HEARO so a future shared lib drops in). */
#define CATTR_NORMAL     0x07
#define CATTR_BRIGHT     0x0F
#define CATTR_DIM        0x08
#define CATTR_CYAN       0x0B
#define CATTR_GREEN      0x0A
#define CATTR_YELLOW     0x0E
#define CATTR_RED        0x0C

#endif /* CHIME_H */
