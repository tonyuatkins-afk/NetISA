/*
 * ui_status.h - CHIME terminal output (deadpan, scriptable).
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef CHIME_UI_STATUS_H
#define CHIME_UI_STATUS_H

#include "chime.h"
#include "netisa_api.h"

void status_banner(const chime_config_t *cfg, const na_card_info_t *card);
void status_show_query(const chime_config_t *cfg, const chime_time_t *server_time,
                       const chime_time_t *local_time, s32 delta_seconds);
void status_show_set_result(const chime_time_t *t, cbool wrote);
void status_show_error(const char *what, int code, const char *msg);
cbool status_confirm_write(void);

#endif
