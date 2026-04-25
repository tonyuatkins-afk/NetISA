/*
 * platform/dos.c - DOS service wrappers.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Watcom build uses INT 21h AH=2Ah for the date. Host builds (HEARO_NOASM)
 * fall back to the C runtime's time().
 */
#include "dos.h"
#include <stdio.h>
#include <string.h>

#ifdef HEARO_NOASM
#include <time.h>
void dos_get_date(char *out11)
{
    time_t t = time(0);
    struct tm *lt = localtime(&t);
    sprintf(out11, "%04d-%02d-%02d",
            lt ? lt->tm_year + 1900 : 2026,
            lt ? lt->tm_mon + 1 : 1,
            lt ? lt->tm_mday : 1);
}
#else
#include <dos.h>
void dos_get_date(char *out11)
{
    union REGS r;
    r.h.ah = 0x2A;
    intdos(&r, &r);
    sprintf(out11, "%04u-%02u-%02u",
            (unsigned)r.x.cx, (unsigned)r.h.dh, (unsigned)r.h.dl);
}
#endif

hbool dos_file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return HFALSE;
    fclose(f);
    return HTRUE;
}
