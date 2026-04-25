/*
 * config/config.h - INI-style HEARO.CFG reader/writer.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_CONFIG_H
#define HEARO_CONFIG_H

#include "../hearo.h"

hbool config_load(const char *path);
hbool config_save(const char *path);
const char *config_get(const char *section, const char *key);
void config_set(const char *section, const char *key, const char *val);
s32  config_get_int(const char *section, const char *key, s32 def);
hbool config_get_bool(const char *section, const char *key, hbool def);

#endif
