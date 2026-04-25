/*
 * cmdline.c - CHIME command-line parsing.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Recognised flags:
 *   /AUTO          write without prompting
 *   /DRYRUN        report only, never write
 *   /QUIET         suppress visual report
 *   /VERBOSE       extra diagnostics
 *   /SAFE          skip detection of the NetISA card; uses stub
 *   /STUBNET       force stub backend (alias for /SAFE in CHIME)
 *   /VERSION       print version, exit
 *   /HELP /?       print help, exit
 *   /SERVER=host   override the time source hostname
 *   /PATH=/foo     URL path used in HEAD/GET (default "/")
 *   /PORT=443      override the port (default 443/80 by mode)
 *   /MODE=xxx      https-head | https-json | http-head | stub
 *   /TZ=+/-HH:MM   timezone offset; default +00:00
 *
 * The struct cfg is left unmodified for fields not mentioned on the line.
 * Defaults belong to the caller.
 */
#include "cmdline.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

static cbool want_help = CFALSE;
static cbool want_version = CFALSE;

cbool cmdline_wants_help(void)    { return want_help; }
cbool cmdline_wants_version(void) { return want_version; }

static void to_upper(char *s)
{
    while (*s) { *s = (char)toupper((unsigned char)*s); s++; }
}

static cbool starts_with(const char *s, const char *prefix)
{
    while (*prefix) {
        char a = *s++, b = *prefix++;
        if (a >= 'a' && a <= 'z') a = (char)(a - 32);
        if (a != b) return CFALSE;
    }
    return CTRUE;
}

static s16 parse_tz(const char *v)
{
    /* Accepts "+HH:MM", "-HH:MM", "HH:MM", "+HH", "-HH". */
    int sign = 1;
    int hh = 0, mm = 0;
    if (*v == '+') v++;
    else if (*v == '-') { sign = -1; v++; }
    while (*v >= '0' && *v <= '9') { hh = hh * 10 + (*v - '0'); v++; }
    if (*v == ':') {
        v++;
        while (*v >= '0' && *v <= '9') { mm = mm * 10 + (*v - '0'); v++; }
    }
    if (hh > 24) hh = 24;
    if (mm > 59) mm = 59;
    return (s16)(sign * (hh * 60 + mm));
}

static chime_source_mode_t parse_mode(const char *v)
{
    if (strstr(v, "HEAD") || strstr(v, "head")) {
        if (strstr(v, "HTTPS") || strstr(v, "https")) return TS_HTTPS_HEAD;
        if (strstr(v, "HTTP")  || strstr(v, "http"))  return TS_HTTP_HEAD;
        return TS_HTTPS_HEAD;
    }
    if (strstr(v, "JSON") || strstr(v, "json")) return TS_HTTPS_JSON;
    if (strstr(v, "STUB") || strstr(v, "stub")) return TS_STUB;
    return TS_AUTO;
}

void cmdline_parse(int argc, char *argv[], chime_config_t *cfg)
{
    int i;
    if (!cfg) return;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v;
        if (!a || (a[0] != '/' && a[0] != '-')) continue;
        a++;
        if (starts_with(a, "AUTO"))    { cfg->auto_write = CTRUE; continue; }
        if (starts_with(a, "DRYRUN"))  { cfg->dry_run    = CTRUE; continue; }
        if (starts_with(a, "QUIET"))   { cfg->quiet      = CTRUE; continue; }
        if (starts_with(a, "VERBOSE")) { cfg->verbose    = CTRUE; continue; }
        if (starts_with(a, "SAFE"))    { cfg->safe       = CTRUE; cfg->stub_net = CTRUE; continue; }
        if (starts_with(a, "STUBNET")) { cfg->stub_net   = CTRUE; continue; }
        if (starts_with(a, "VERSION")) { want_version    = CTRUE; continue; }
        if (starts_with(a, "HELP") || a[0] == '?') { want_help = CTRUE; continue; }

        v = strchr(a, '=');
        if (!v) continue;
        v++;

        if (starts_with(a, "SERVER=")) {
            strncpy(cfg->server, v, sizeof(cfg->server) - 1);
            cfg->server[sizeof(cfg->server) - 1] = '\0';
        } else if (starts_with(a, "PATH=")) {
            strncpy(cfg->path, v, sizeof(cfg->path) - 1);
            cfg->path[sizeof(cfg->path) - 1] = '\0';
        } else if (starts_with(a, "PORT=")) {
            cfg->port = (u16)atoi(v);
        } else if (starts_with(a, "MODE=")) {
            cfg->mode = parse_mode(v);
        } else if (starts_with(a, "TZ=")) {
            cfg->tz_offset_minutes = parse_tz(v);
        }
    }
    if (cfg->mode == TS_AUTO) cfg->mode = TS_HTTPS_HEAD;
    if (cfg->port == 0) cfg->port = (cfg->mode == TS_HTTP_HEAD) ? 80 : 443;
    if (cfg->server[0] == '\0') strcpy(cfg->server, "time.cloudflare.com");
    if (cfg->path[0] == '\0')   strcpy(cfg->path, "/");
}
