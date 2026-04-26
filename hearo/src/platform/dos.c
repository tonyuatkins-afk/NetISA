/*
 * platform/dos.c - DOS service wrappers.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#include "dos.h"
#include <stdio.h>
#include <string.h>
#include <dos.h>
#include <i86.h>

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

hbool dos_mcb_validate(void)
{
    union REGS r;
    struct SREGS sr;
    u16 first_mcb_seg;
    u16 mcb_seg;
    u16 chain_walks = 0;

    /* INT 21h AH=52h: get pointer to "List of Lists" (DOS internal data
     * structures) in ES:BX. The head of the MCB chain lives in the u16
     * immediately preceding the LoL: ES:[BX-2]. The location is officially
     * undocumented but stable from DOS 3.x through 7.x and is the same
     * lookup used by every memory utility (MEM, CHKDSK, etc.).
     *
     * Guard r.x.bx >= 2 before the BX-2 subtraction. A bogus return
     * with bx in {0, 1} would underflow to offset 0xFFFE/0xFFFF inside
     * the LoL segment and read whatever bytes happen to be there. The
     * head-segment range check below catches that, but failing the
     * walk earlier is cheaper. */
    memset(&r, 0, sizeof(r));
    memset(&sr, 0, sizeof(sr));
    r.h.ah = 0x52;
    int86x(0x21, &r, &r, &sr);
    if (r.x.bx < 2) return HFALSE;
    {
        u16 far *head = (u16 far *)MK_FP(sr.es, (u16)(r.x.bx - 2));
        first_mcb_seg = *head;
    }
    /* Sanity floor on the head segment. The first MCB lives in low memory
     * (typically around 0x02xx-0x07xx depending on DOS version and TSRs).
     * A near-zero or absurdly high head pointer means we got back garbage
     * from INT 21h AH=52h, treat as corrupt. */
    if (first_mcb_seg < 0x0040 || first_mcb_seg >= 0xA000) return HFALSE;

    mcb_seg = first_mcb_seg;
    while (chain_walks++ < 0x4000) {
        u8 far  *mcb  = (u8 far *)MK_FP(mcb_seg, 0);
        u8       type = mcb[0];
        u16      size = *(u16 far *)(mcb + 3);
        u32      next_seg32;

        if (type == 'Z') return HTRUE;        /* end of chain, healthy */
        if (type != 'M') return HFALSE;       /* corrupt: bad signature */
        if (size == 0)   return HFALSE;       /* corrupt: zero-size block */

        /* Walk past the 1-paragraph MCB header and the size-paragraph block.
         * Compute in u32 first so a corrupt size cannot wrap silently into
         * a small segment that still walks happily forward. */
        next_seg32 = (u32)mcb_seg + 1UL + (u32)size;
        if (next_seg32 >= 0x10000UL) return HFALSE;  /* wrapped past 1 MiB */
        if ((u16)next_seg32 <= mcb_seg) return HFALSE; /* did not advance */
        mcb_seg = (u16)next_seg32;
    }
    /* Walked too long without hitting 'Z'; chain is either corrupt or the
     * head pointer was bogus. Either way we cannot vouch for it. */
    return HFALSE;
}
