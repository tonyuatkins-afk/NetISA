/*
 * platform/dos.c - DOS service wrappers.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#include "dos.h"
#include <stdio.h>
#include <string.h>
#include <dos.h>

void dos_get_date(char *out11)
{
    union REGS r;
    r.h.ah = 0x2A;
    intdos(&r, &r);
    sprintf(out11, "%04u-%02u-%02u",
            (unsigned)r.x.cx, (unsigned)r.h.dh, (unsigned)r.h.dl);
}

hbool dos_file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return HFALSE;
    fclose(f);
    return HTRUE;
}
