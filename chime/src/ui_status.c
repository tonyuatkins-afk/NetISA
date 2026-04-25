/*
 * ui_status.c - Terminal output and the confirmation prompt.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Output is plain stdio (no direct VRAM) so CHIME runs cleanly under DOS,
 * DOSBox-X, AUTOEXEC.BAT, redirection to a log file, and pipe-into-something.
 * The aesthetic is "ntpdate output" not "boot screen celebration": one
 * machine-readable line per fact, deadpan tone, no boxes or right-justify.
 */
#include "ui_status.h"
#include <stdio.h>
#include <string.h>

void status_banner(const chime_config_t *cfg, const na_card_info_t *card)
{
    printf("CHIME %s\n", CHIME_VER_STRING);
    if (na_is_stub()) {
        printf("NetISA: stub mode\n");
    } else if (card && card->ready) {
        printf("NetISA: ready (firmware %s, IP %u.%u.%u.%u)\n",
               card->fw_string,
               card->ip[0], card->ip[1], card->ip[2], card->ip[3]);
    } else if (card) {
        printf("NetISA: detected, network not ready\n");
    } else {
        printf("NetISA: not detected\n");
    }
    {
        const char *mode_str =
            cfg->mode == TS_HTTPS_JSON ? "HTTPS GET JSON" :
            cfg->mode == TS_HTTP_HEAD  ? "HTTP HEAD"      :
            cfg->mode == TS_STUB       ? "stub"           : "HTTPS HEAD";
        if (cfg->path[0] && strcmp(cfg->path, "/") != 0) {
            printf("Server: %s%s (%s)\n", cfg->server, cfg->path, mode_str);
        } else {
            printf("Server: %s (%s)\n", cfg->server, mode_str);
        }
    }
}

static void format_iso(const chime_time_t *t, char *out)
{
    sprintf(out, "%04u-%02u-%02u %02u:%02u:%02u",
            t->year, t->month, t->day, t->hour, t->minute, t->second);
}

void status_show_query(const chime_config_t *cfg, const chime_time_t *server_time,
                       const chime_time_t *local_time, s32 delta_seconds)
{
    char iso[24];
    if (server_time) {
        format_iso(server_time, iso);
        if (cfg->tz_offset_minutes != 0) {
            int h = cfg->tz_offset_minutes / 60;
            int m = cfg->tz_offset_minutes % 60;
            if (m < 0) m = -m;
            printf("Time:   %s UTC (%+d:%02d offset)\n", iso, h, m);
        } else {
            printf("Time:   %s UTC\n", iso);
        }
    }
    if (local_time) {
        format_iso(local_time, iso);
        printf("Now:    %s (DOS clock)\n", iso);
    }
    if (delta_seconds == 0) {
        printf("Delta:  0 (already in sync)\n");
    } else {
        printf("Delta:  %+ld seconds\n", (long)delta_seconds);
    }
}

void status_show_set_result(const chime_time_t *t, cbool wrote)
{
    char iso[24];
    if (!t) return;
    format_iso(t, iso);
    if (wrote) printf("DOS clock set to %s\n", iso);
    else       printf("DOS clock NOT set (dry run or declined)\n");
}

void status_show_error(const char *what, int code, const char *msg)
{
    fprintf(stderr, "CHIME error: %s (%d): %s\n",
            what ? what : "operation", code, msg ? msg : "");
}

cbool status_confirm_write(void)
{
    int c;
    printf("Write to DOS clock? [Y/n] ");
    fflush(stdout);
    c = getchar();
    /* Drain the rest of the line so leftover input does not feed the next
     * program. */
    while (c != EOF && c != '\n') {
        int next = getchar();
        if (next == EOF || next == '\n') break;
        c = next;
    }
    if (c == 'n' || c == 'N') return CFALSE;
    return CTRUE;
}
