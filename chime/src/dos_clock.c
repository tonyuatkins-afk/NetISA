/*
 * dos_clock.c - INT 21h date/time + CMOS RTC write-through.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * DOS exposes date and time via INT 21h:
 *   AH=2Ah   get date  -> CX=year, DH=month, DL=day, AL=day_of_week
 *   AH=2Bh   set date  <- CX=year, DH=month, DL=day, returns AL=0/FF
 *   AH=2Ch   get time  -> CH=hour, CL=minute, DH=second, DL=hundredths
 *   AH=2Dh   set time  <- CH=hour, CL=minute, DH=second, DL=hundredths
 *
 * On modern DOS (MS-DOS 5+, FreeDOS), AH=2Bh/2Dh propagate to the CMOS RTC.
 * On older DOS (3.3, OEM 4.x), the DOS layer advances a soft clock and
 * never touches the RTC, so a reboot loses the change. CHIME does a direct
 * CMOS write after the INT 21h call as a backstop.
 *
 * The CMOS path uses I/O ports 0x70 (index) and 0x71 (data). The RTC stores
 * BCD-encoded values in registers 0x00..0x09.
 */
#include "dos_clock.h"
#include <stdio.h>
#include <dos.h>
#include <i86.h>
#include <conio.h>

void dos_clock_get(chime_time_t *out)
{
    union REGS r;
    if (!out) return;
    r.h.ah = 0x2A; intdos(&r, &r);
    out->year  = r.x.cx;
    out->month = r.h.dh;
    out->day   = r.h.dl;
    r.h.ah = 0x2C; intdos(&r, &r);
    out->hour   = r.h.ch;
    out->minute = r.h.cl;
    out->second = r.h.dh;
    out->pad = 0;
    out->unix_ts = 0;
    /* Unix ts derived from components in timesrc.c if needed; not used by the
     * confirmation prompt. */
}

static u8 cmos_read(u8 reg)
{
    outp(0x70, reg);
    return (u8)inp(0x71);
}

static void cmos_write(u8 reg, u8 val)
{
    outp(0x70, reg);
    outp(0x71, val);
}

static u8 to_bcd(u8 v)  { return (u8)(((v / 10) << 4) | (v % 10)); }

static void cmos_set(const chime_time_t *t)
{
    /* Stop RTC updates while writing (set bit 7 of register B = 0x0B). */
    u8 reg_b = cmos_read(0x0B);
    cmos_write(0x0B, (u8)(reg_b | 0x80));

    cmos_write(0x00, to_bcd(t->second));
    cmos_write(0x02, to_bcd(t->minute));
    cmos_write(0x04, to_bcd(t->hour));
    cmos_write(0x07, to_bcd(t->day));
    cmos_write(0x08, to_bcd(t->month));
    cmos_write(0x09, to_bcd((u8)(t->year % 100)));
    /* Century byte at 0x32 is conventional but not universal. */
    cmos_write(0x32, to_bcd((u8)(t->year / 100)));

    /* Resume updates. */
    cmos_write(0x0B, reg_b);
}

void dos_clock_set(const chime_time_t *t)
{
    union REGS r;
    if (!t) return;
    r.h.ah = 0x2B;
    r.x.cx = t->year;
    r.h.dh = t->month;
    r.h.dl = t->day;
    intdos(&r, &r);
    r.h.ah = 0x2D;
    r.h.ch = t->hour;
    r.h.cl = t->minute;
    r.h.dh = t->second;
    r.h.dl = 0;
    intdos(&r, &r);
    cmos_set(t);
}
