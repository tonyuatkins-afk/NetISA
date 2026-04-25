/*
 * test/testtime.c - Unit-style test for the parsers.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Exercises:
 *   - timesrc_to_unix_ts / timesrc_from_unix_ts roundtrip
 *   - timesrc_parse_http_date on a known RFC 7231 string
 *   - timesrc_parse_worldtime_json on a known JSON snippet
 *   - timesrc_query in stub mode produces the expected canned timestamp
 */
#include "../src/chime.h"
#include "../src/timesrc.h"
#include "../src/netisa_api.h"
#include "../src/cmdline.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void expect_eq_u32(const char *what, u32 got, u32 want)
{
    if (got == want) {
        printf("  ok    %s = %lu\n", what, (unsigned long)got);
    } else {
        printf("  FAIL  %s: got %lu, want %lu\n", what,
               (unsigned long)got, (unsigned long)want);
        failures++;
    }
}

static void expect_eq_u(const char *what, unsigned got, unsigned want)
{
    if (got == want) {
        printf("  ok    %s = %u\n", what, got);
    } else {
        printf("  FAIL  %s: got %u, want %u\n", what, got, want);
        failures++;
    }
}

static void test_unix_roundtrip(void)
{
    chime_time_t t;
    u32 ts;
    chime_time_t back;
    printf("test_unix_roundtrip\n");
    t.year = 2026; t.month = 4; t.day = 25;
    t.hour = 14; t.minute = 30; t.second = 42;
    t.pad = 0; t.unix_ts = 0;
    ts = timesrc_to_unix_ts(&t);
    expect_eq_u32("unix_ts(2026-04-25T14:30:42Z)", ts, 1777127442UL);
    timesrc_from_unix_ts(ts, &back);
    expect_eq_u("year",   back.year,   2026);
    expect_eq_u("month",  back.month,  4);
    expect_eq_u("day",    back.day,    25);
    expect_eq_u("hour",   back.hour,   14);
    expect_eq_u("minute", back.minute, 30);
    expect_eq_u("second", back.second, 42);
}

static void test_parse_http_date(void)
{
    chime_time_t out;
    cbool ok;
    printf("test_parse_http_date\n");
    ok = timesrc_parse_http_date("Sat, 25 Apr 2026 14:30:42 GMT", &out);
    if (!ok) { printf("  FAIL  parse returned false\n"); failures++; return; }
    expect_eq_u("year",   out.year,   2026);
    expect_eq_u("month",  out.month,  4);
    expect_eq_u("day",    out.day,    25);
    expect_eq_u("hour",   out.hour,   14);
    expect_eq_u("minute", out.minute, 30);
    expect_eq_u("second", out.second, 42);
    expect_eq_u32("unix_ts", out.unix_ts, 1777127442UL);
}

static void test_parse_json(void)
{
    chime_time_t out;
    cbool ok;
    const char *body =
        "{\"unixtime\":1777127442,"
        "\"utc_datetime\":\"2026-04-25T14:30:42+00:00\"}";
    printf("test_parse_json\n");
    ok = timesrc_parse_worldtime_json(body, &out);
    if (!ok) { printf("  FAIL  parse returned false\n"); failures++; return; }
    expect_eq_u("year", out.year, 2026);
    expect_eq_u("hour", out.hour, 14);
    expect_eq_u("minute", out.minute, 30);
    expect_eq_u("second", out.second, 42);
}

static void test_stub_query(void)
{
    chime_config_t cfg;
    chime_time_t out;
    chime_result_t r;
    printf("test_stub_query\n");
    memset(&cfg, 0, sizeof(cfg));
    strcpy(cfg.server, "stub.local");
    strcpy(cfg.path, "/");
    cfg.port = 443;
    cfg.mode = TS_HTTPS_HEAD;
    cfg.stub_net = CTRUE;
    na_use_stub(CTRUE);
    r = timesrc_query(&cfg, &out);
    if (r != TR_OK) {
        printf("  FAIL  query returned %d\n", (int)r);
        failures++;
        return;
    }
    expect_eq_u("year",  out.year,  2026);
    expect_eq_u("month", out.month, 4);
    expect_eq_u("day",   out.day,   25);
    expect_eq_u("hour",  out.hour,  14);
}

int main(void)
{
    printf("CHIME TESTTIME %s\n", CHIME_VER_STRING);
    printf("==============================\n");
    test_unix_roundtrip();
    test_parse_http_date();
    test_parse_json();
    test_stub_query();
    printf("\n%d failure%s\n", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
