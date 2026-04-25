/*
 * chime.c - CHIME main entry.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Flow:
 *   1. Parse CHIME.CFG for defaults, parse argv to override.
 *   2. Detect NetISA card (or activate stub).
 *   3. Query the configured time source for UTC.
 *   4. Read current DOS clock, compute delta.
 *   5. Confirm with user (unless /AUTO or /DRYRUN).
 *   6. Apply timezone offset, set DOS + CMOS clock.
 *   7. Print final result line.
 *
 * Exit codes:
 *   0  ok
 *   1  user declined or /DRYRUN
 *   2  query failed
 *   3  NetISA not present
 *   4  configuration / argv error
 */
#include "chime.h"
#include "cmdline.h"
#include "config.h"
#include "netisa_api.h"
#include "timesrc.h"
#include "dos_clock.h"
#include "ui_status.h"
#include <stdio.h>
#include <string.h>

static void apply_tz(chime_time_t *t, s16 offset_minutes)
{
    /* offset_minutes is added to UTC to produce local. */
    s32 ts = (s32)t->unix_ts + (s32)offset_minutes * 60L;
    if (ts < 0) ts = 0;
    timesrc_from_unix_ts((u32)ts, t);
}

static void print_help(void)
{
    printf(
        "CHIME %s - NetISA time sync\n"
        "\n"
        "Usage: CHIME [flags]\n"
        "\n"
        "  /AUTO          write without prompting\n"
        "  /DRYRUN        report only, never write\n"
        "  /QUIET         suppress visual report\n"
        "  /VERBOSE       extra diagnostics\n"
        "  /SAFE          use stub backend (no NetISA needed)\n"
        "  /STUBNET       same as /SAFE\n"
        "  /VERSION       print version, exit\n"
        "  /HELP          this list\n"
        "  /SERVER=host   override time-source hostname\n"
        "  /PATH=/foo     URL path for HEAD/GET (default /)\n"
        "  /PORT=443      override port\n"
        "  /MODE=xxx      https-head | https-json | http-head | stub\n"
        "  /TZ=+/-HH:MM   timezone offset (default +00:00)\n",
        CHIME_VER_STRING);
}

int main(int argc, char *argv[])
{
    chime_config_t cfg;
    na_card_info_t card;
    chime_time_t   server_time;
    chime_time_t   local_time;
    chime_result_t qr;
    int detect_rc;
    s32 delta;
    cbool wrote;

    memset(&cfg, 0, sizeof(cfg));
    cfg.tz_offset_minutes = 0;
    cfg.mode = TS_AUTO;

    config_load("CHIME.CFG", &cfg);
    cmdline_parse(argc, argv, &cfg);

    if (cmdline_wants_version()) {
        printf("CHIME %s %s\n", CHIME_VER_STRING, CHIME_COPYRIGHT);
        return 0;
    }
    if (cmdline_wants_help()) {
        print_help();
        return 0;
    }

    if (cfg.stub_net || cfg.safe || cfg.mode == TS_STUB) na_use_stub(CTRUE);

    memset(&card, 0, sizeof(card));
    detect_rc = na_detect(&card);
    if (detect_rc != NA_OK && !na_is_stub()) {
        if (!cfg.quiet) status_banner(&cfg, 0);
        status_show_error("netisa-detect", detect_rc, na_strerror(detect_rc));
        return 3;
    }
    if (!cfg.quiet) status_banner(&cfg, &card);

    qr = timesrc_query(&cfg, &server_time);
    if (qr != TR_OK) {
        const char *msg;
        switch (qr) {
            case TR_NETISA_NOT_READY: msg = "NetISA not ready"; break;
            case TR_DNS_FAILED:       msg = "DNS resolution failed"; break;
            case TR_CONNECT_FAILED:   msg = "TCP connect failed"; break;
            case TR_TLS_HANDSHAKE:    msg = "TLS handshake failed"; break;
            case TR_HTTP_BAD_STATUS:  msg = "HTTP status not 200"; break;
            case TR_NO_DATE_HEADER:   msg = "no Date header in response"; break;
            case TR_PARSE_FAILED:     msg = "failed to parse response"; break;
            case TR_TIMEOUT:          msg = "operation timed out"; break;
            default:                  msg = "unknown failure"; break;
        }
        status_show_error("query", (int)qr, msg);
        return 2;
    }

    dos_clock_get(&local_time);
    local_time.unix_ts = timesrc_to_unix_ts(&local_time);
    delta = (s32)server_time.unix_ts - (s32)local_time.unix_ts;

    if (!cfg.quiet) {
        status_show_query(&cfg, &server_time, &local_time, delta);
    }

    /* Apply timezone offset to produce the value to actually write. */
    apply_tz(&server_time, cfg.tz_offset_minutes);

    if (cfg.dry_run) {
        status_show_set_result(&server_time, CFALSE);
        return 1;
    }
    if (!cfg.auto_write && !cfg.quiet) {
        if (!status_confirm_write()) {
            status_show_set_result(&server_time, CFALSE);
            return 1;
        }
    }
    dos_clock_set(&server_time);
    wrote = CTRUE;
    status_show_set_result(&server_time, wrote);

    config_save("CHIME.CFG", &cfg);
    return 0;
}
