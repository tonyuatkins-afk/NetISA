/*
 * detect/netisa.c - NetISA card probe.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Real probe is a register read at the configured I/O base, looking for the
 * NetISA signature. When stub_mode is HTRUE we synthesise a present card so
 * downstream code paths exercise without hardware on the bench.
 */
#include "netisa.h"
#include <string.h>

extern hbool nisa_signature(u16 base);

#ifdef HEARO_NOASM
hbool nisa_signature(u16 b) { (void)b; return HFALSE; }
#endif

void netisa_detect(hw_profile_t *hw, hbool stub_mode)
{
    if (stub_mode) {
        hw->nisa_status = NISA_LINK_UP;
        hw->nisa_base   = 0x300;
        strcpy(hw->nisa_fw, "stub-1.0.0");
        return;
    }

    {
        u16 candidates[] = { 0x300, 0x320, 0x340, 0x280, 0 };
        u16 i;
        for (i = 0; candidates[i]; i++) {
            if (nisa_signature(candidates[i])) {
                hw->nisa_status = NISA_LINK_UP;
                hw->nisa_base = candidates[i];
                strcpy(hw->nisa_fw, "1.0.0");
                return;
            }
        }
    }
    hw->nisa_status = NISA_NOT_FOUND;
    hw->nisa_base = 0;
    hw->nisa_fw[0] = '\0';
}
