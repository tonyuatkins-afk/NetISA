/*
 * timesrc.h - Time-source interface and HTTP-Date / JSON parsers.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef CHIME_TIMESRC_H
#define CHIME_TIMESRC_H

#include "chime.h"

/* Query an authoritative time source per the configured mode. The returned
 * chime_time_t carries UTC; the caller is responsible for applying the
 * configured timezone offset before display or DOS clock set. */
chime_result_t timesrc_query(const chime_config_t *cfg, chime_time_t *out);

/* Parse an RFC 7231 Date: header value (e.g. "Sat, 25 Apr 2026 14:30:42 GMT")
 * into broken-down components. Returns CTRUE on success. */
cbool timesrc_parse_http_date(const char *header_value, chime_time_t *out);

/* Parse the JSON body returned by worldtimeapi.org's /api/timezone endpoint.
 * Looks for the unixtime field and the utc_datetime field. */
cbool timesrc_parse_worldtime_json(const char *body, chime_time_t *out);

/* Compose unix_ts from y/m/d/h/m/s. Year must be >= 1970. */
u32 timesrc_to_unix_ts(const chime_time_t *t);

/* Decompose a unix_ts into y/m/d/h/m/s. Result year is in [1970, 2099]. */
void timesrc_from_unix_ts(u32 unix_ts, chime_time_t *out);

#endif
