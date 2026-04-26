/*
 * platform/dos.h - DOS service wrappers (date, key, exit).
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_PLATFORM_DOS_H
#define HEARO_PLATFORM_DOS_H

#include "../hearo.h"

void dos_get_date(char *out11);   /* "YYYY-MM-DD" + NUL */
hbool dos_file_exists(const char *path);

/* Walks the DOS MCB chain from the head pointer at ES:[BX-2] returned by
 * INT 21h AH=52h (List of Lists). HTRUE if every block has a valid 'M' or
 * 'Z' signature and the chain terminates with 'Z' inside a sane block-count
 * budget; HFALSE if the chain contains a corrupted block, an absurd size,
 * or runs longer than would fit in 16-bit segment space.
 *
 * Intended use: call after the audio driver shutdown but before main()
 * returns. A stuck DMA cycle that wrote past a freed buffer's MCB will
 * usually produce a 'M' / 'Z' byte mismatch in the chain; surfacing that
 * as a diagnostic before DOS itself panics with "Memory allocation error"
 * lets the operator know HEARO contributed to the corruption rather than
 * blaming COMMAND.COM. */
hbool dos_mcb_validate(void);

#endif
