/*
 * platform/dos.h - DOS service wrappers (date, key, exit).
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_PLATFORM_DOS_H
#define HEARO_PLATFORM_DOS_H

#include "../hearo.h"

void dos_get_date(char *out11);   /* "YYYY-MM-DD" + NUL */
hbool dos_file_exists(const char *path);

#endif
