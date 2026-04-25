/*
 * config.h - CHIME.CFG INI reader.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef CHIME_CONFIG_H
#define CHIME_CONFIG_H

#include "chime.h"

/* Load CHIME.CFG into cfg. Missing keys leave the corresponding field
 * unchanged so the cmdline overrides cleanly afterwards. Returns CTRUE if a
 * file was found and parsed (even partially). */
cbool config_load(const char *path, chime_config_t *cfg);

/* Persist current cfg to CHIME.CFG. */
cbool config_save(const char *path, const chime_config_t *cfg);

#endif
