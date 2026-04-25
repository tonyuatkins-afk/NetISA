/*
 * cmdline.h - Command-line parsing for CHIME.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef CHIME_CMDLINE_H
#define CHIME_CMDLINE_H

#include "chime.h"

void cmdline_parse(int argc, char *argv[], chime_config_t *cfg);

/* True if a /HELP or /? was seen; main() then prints help and exits. */
cbool cmdline_wants_help(void);

/* True if /VERSION was seen. */
cbool cmdline_wants_version(void);

#endif
