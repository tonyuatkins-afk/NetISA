/*
 * config/cmdline.h - Command line parsing.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_CMDLINE_H
#define HEARO_CMDLINE_H

#include "../hearo.h"

void cmdline_parse(int argc, char *argv[]);
hbool cmdline_has(const char *flag);          /* "/SAFE" present? case-insensitive, lookup without leading slash */
const char *cmdline_value(const char *flag);  /* returns value of /KEY=VAL, or NULL */

#endif
