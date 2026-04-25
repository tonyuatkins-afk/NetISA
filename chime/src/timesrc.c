/*
 * timesrc.c - Time-source query and HTTP-Date / JSON parsers.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * timesrc_query orchestrates: open a session, build a request, read the
 * response, parse, close. Parsing is split into RFC 7231 Date-header parsing
 * (used by both HTTPS HEAD and HTTP HEAD modes) and a worldtime JSON parser
 * (used by HTTPS JSON mode). All parsing is done character by character with
 * no regex, no malloc, no scanf-style locale dependence.
 */
#include "timesrc.h"
#include "netisa_api.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static const char *month_names[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/* ---- Date math ---- */

static cbool is_leap(u16 y)
{
    if ((y % 4) != 0) return CFALSE;
    if ((y % 100) != 0) return CTRUE;
    if ((y % 400) != 0) return CFALSE;
    return CTRUE;
}

static u8 days_in_month(u16 y, u8 m)
{
    static const u8 dim[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m < 1 || m > 12) return 0;
    if (m == 2 && is_leap(y)) return 29;
    return dim[m - 1];
}

u32 timesrc_to_unix_ts(const chime_time_t *t)
{
    u32 seconds = 0;
    u16 y;
    u8 m;
    if (!t || t->year < 1970) return 0;
    for (y = 1970; y < t->year; y++) {
        seconds += (is_leap(y) ? 366UL : 365UL) * 86400UL;
    }
    for (m = 1; m < t->month; m++) {
        seconds += (u32)days_in_month(t->year, m) * 86400UL;
    }
    seconds += (u32)(t->day - 1) * 86400UL;
    seconds += (u32)t->hour * 3600UL;
    seconds += (u32)t->minute * 60UL;
    seconds += (u32)t->second;
    return seconds;
}

void timesrc_from_unix_ts(u32 unix_ts, chime_time_t *out)
{
    u16 y;
    u8 m;
    u32 day_seconds;
    u32 days;
    if (!out) return;
    days = unix_ts / 86400UL;
    day_seconds = unix_ts % 86400UL;
    out->hour   = (u8)(day_seconds / 3600UL);
    out->minute = (u8)((day_seconds % 3600UL) / 60UL);
    out->second = (u8)(day_seconds % 60UL);
    y = 1970;
    while (1) {
        u32 yd = is_leap(y) ? 366UL : 365UL;
        if (days < yd) break;
        days -= yd;
        y++;
    }
    out->year = y;
    m = 1;
    while (m <= 12) {
        u32 md = (u32)days_in_month(y, m);
        if (days < md) break;
        days -= md;
        m++;
    }
    out->month = m;
    out->day   = (u8)(days + 1);
    out->unix_ts = unix_ts;
    out->pad = 0;
}

/* ---- HTTP Date header parser ---- */

static int month_index(const char *s)
{
    int i;
    for (i = 0; i < 12; i++) {
        if (s[0] == month_names[i][0] &&
            s[1] == month_names[i][1] &&
            s[2] == month_names[i][2]) return i + 1;
    }
    return 0;
}

static const char *skip_spaces(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static const char *parse_uint(const char *s, u32 *out, u8 max_digits)
{
    u32 v = 0;
    u8 n = 0;
    while (*s >= '0' && *s <= '9' && n < max_digits) {
        v = v * 10 + (u32)(*s - '0');
        s++;
        n++;
    }
    if (out) *out = v;
    return s;
}

cbool timesrc_parse_http_date(const char *h, chime_time_t *out)
{
    /* Expects RFC 7231 IMF-fixdate: e.g. "Sat, 25 Apr 2026 14:30:42 GMT".
     * We accept any weekday spelling (3-letter or full); we do not validate
     * day-of-week against the date. The trailing zone must be GMT or UTC. */
    const char *p;
    u32 v;
    int mi;
    if (!h || !out) return CFALSE;
    p = h;
    /* Skip optional weekday + comma. */
    while (*p && *p != ',') p++;
    if (*p == ',') p++;
    p = skip_spaces(p);
    /* Day */
    p = parse_uint(p, &v, 2);
    if (v == 0 || v > 31) return CFALSE;
    out->day = (u8)v;
    p = skip_spaces(p);
    /* Month */
    mi = month_index(p);
    if (mi == 0) return CFALSE;
    out->month = (u8)mi;
    p += 3;
    p = skip_spaces(p);
    /* Year */
    p = parse_uint(p, &v, 4);
    if (v < 1980 || v > 2099) return CFALSE;
    out->year = (u16)v;
    p = skip_spaces(p);
    /* HH:MM:SS */
    p = parse_uint(p, &v, 2);
    if (v > 23) return CFALSE;
    out->hour = (u8)v;
    if (*p != ':') return CFALSE;
    p++;
    p = parse_uint(p, &v, 2);
    if (v > 59) return CFALSE;
    out->minute = (u8)v;
    if (*p != ':') return CFALSE;
    p++;
    p = parse_uint(p, &v, 2);
    if (v > 60) return CFALSE; /* allow leap second 60 */
    out->second = (u8)(v == 60 ? 59 : v);
    p = skip_spaces(p);
    /* GMT/UTC */
    if (!(p[0] == 'G' && p[1] == 'M' && p[2] == 'T') &&
        !(p[0] == 'U' && p[1] == 'T' && p[2] == 'C')) return CFALSE;
    out->unix_ts = timesrc_to_unix_ts(out);
    out->pad = 0;
    return CTRUE;
}

/* ---- worldtimeapi JSON parser (minimal) ---- */

cbool timesrc_parse_worldtime_json(const char *body, chime_time_t *out)
{
    const char *p;
    u32 v;
    if (!body || !out) return CFALSE;
    p = strstr(body, "\"unixtime\"");
    if (!p) return CFALSE;
    p = strchr(p, ':');
    if (!p) return CFALSE;
    p++;
    p = skip_spaces(p);
    p = parse_uint(p, &v, 10);
    if (v < 315532800UL) return CFALSE;  /* sanity floor: 1980-01-01 */
    timesrc_from_unix_ts(v, out);
    return CTRUE;
}

/* ---- HTTP request builder + response walker ---- */

static cbool find_header_value(const char *resp, const char *name,
                               char *out, u16 out_max)
{
    /* Case-insensitive header search. Stops at CRLF or LF. */
    u16 nlen = (u16)strlen(name);
    const char *p = resp;
    while (*p) {
        u16 i;
        cbool match = CTRUE;
        for (i = 0; i < nlen; i++) {
            char a = p[i], b = name[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) { match = CFALSE; break; }
        }
        if (match && p[nlen] == ':') {
            const char *v = p + nlen + 1;
            u16 j = 0;
            while (*v == ' ' || *v == '\t') v++;
            while (*v && *v != '\r' && *v != '\n' && j < out_max - 1) {
                out[j++] = *v++;
            }
            out[j] = '\0';
            return CTRUE;
        }
        /* Advance to next line */
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return CFALSE;
}

static const char *find_body_start(const char *resp)
{
    const char *p = strstr(resp, "\r\n\r\n");
    if (p) return p + 4;
    p = strstr(resp, "\n\n");
    if (p) return p + 2;
    return 0;
}

static int http_status(const char *resp)
{
    /* "HTTP/1.1 200 OK" */
    int code = 0;
    while (*resp && *resp != ' ') resp++;
    while (*resp == ' ') resp++;
    while (*resp >= '0' && *resp <= '9') {
        code = code * 10 + (*resp - '0');
        resp++;
    }
    return code;
}

static chime_result_t do_http_query(const chime_config_t *cfg, cbool tls,
                                    const char *method,
                                    chime_time_t *out)
{
    char request[256];
    static char response[2048];
    na_handle_t handle;
    int rc;
    u16 total = 0;
    u16 got;

    rc = na_session_open(cfg->server, cfg->port, tls, &handle);
    if (rc != NA_OK) {
        switch (rc) {
            case NA_DNS_FAILED:     return TR_DNS_FAILED;
            case NA_CONNECT_FAILED: return TR_CONNECT_FAILED;
            case NA_TLS_HANDSHAKE:  return TR_TLS_HANDSHAKE;
            case NA_TIMEOUT:        return TR_TIMEOUT;
            default:                return TR_NETISA_NOT_READY;
        }
    }

    sprintf(request,
            "%s %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: CHIME/%s\r\n"
            "Accept: */*\r\n"
            "Connection: close\r\n"
            "\r\n",
            method, cfg->path, cfg->server, CHIME_VER_STRING);
    rc = na_session_send(handle, request, (u16)strlen(request));
    if (rc != NA_OK) { na_session_close(handle); return TR_TIMEOUT; }

    while (total + 1 < (u16)sizeof(response)) {
        got = 0;
        rc = na_session_recv(handle, response + total,
                             (u16)(sizeof(response) - 1 - total), &got);
        if (rc != NA_OK || got == 0) break;
        total = (u16)(total + got);
    }
    response[total] = '\0';
    na_session_close(handle);

    if (total == 0) return TR_TIMEOUT;
    if (http_status(response) != 200) return TR_HTTP_BAD_STATUS;

    if (cfg->mode == TS_HTTPS_JSON) {
        const char *body = find_body_start(response);
        if (!body) return TR_PARSE_FAILED;
        return timesrc_parse_worldtime_json(body, out) ? TR_OK : TR_PARSE_FAILED;
    } else {
        char date_value[64];
        if (!find_header_value(response, "Date", date_value, sizeof(date_value)))
            return TR_NO_DATE_HEADER;
        return timesrc_parse_http_date(date_value, out) ? TR_OK : TR_PARSE_FAILED;
    }
}

chime_result_t timesrc_query(const chime_config_t *cfg, chime_time_t *out)
{
    chime_source_mode_t mode;
    if (!cfg || !out) return TR_UNKNOWN;
    mode = cfg->mode;
    if (mode == TS_AUTO) mode = TS_HTTPS_HEAD;
    switch (mode) {
        case TS_HTTPS_HEAD:  return do_http_query(cfg, CTRUE,  "HEAD", out);
        case TS_HTTPS_JSON:  return do_http_query(cfg, CTRUE,  "GET",  out);
        case TS_HTTP_HEAD:   return do_http_query(cfg, CFALSE, "HEAD", out);
        case TS_STUB:        {
            /* Stub: synthesise a known timestamp for tests. */
            chime_time_t t;
            t.year = 2026; t.month = 4; t.day = 25;
            t.hour = 14; t.minute = 30; t.second = 42;
            t.pad = 0;
            t.unix_ts = timesrc_to_unix_ts(&t);
            *out = t;
            return TR_OK;
        }
        default:             return TR_UNKNOWN;
    }
}
