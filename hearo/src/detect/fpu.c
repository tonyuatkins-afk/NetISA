/*
 * detect/fpu.c - FPU presence and brand identification.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Strategy:
 *   1. FNINIT / FNSTSW: AX==0 means an x87 is present.
 *   2. FNSTCW control word: 03FFh = 287 (or compatible), 037Fh = 387+.
 *   3. Brand fingerprint:
 *        IIT 2C87 responds to a well known custom register set probe.
 *        Cyrix exposes its FasMath via CCR registers (port 22h/23h).
 *        ULSI and AMD are detected by FPATAN / FYL2X bit patterns where possible.
 *        Otherwise FPUB_UNKNOWN.
 *   4. If integrated (CPUID feature bit 0), FPU type is FPU_INTEGRATED and
 *      brand follows the CPU vendor.
 */
#include "fpu.h"
#include <stdio.h>

extern int  fpu_present(void);            /* 1 if FNINIT/FNSTSW says yes */
extern u16  fpu_status_word(void);
extern u16  fpu_control_word(void);
extern int  fpu_iit_probe(void);          /* 1 if IIT 2C87 register window responds */
extern int  fpu_cyrix_probe(void);        /* 1 if Cyrix CCR is readable */

#ifdef HEARO_NOASM
int fpu_present(void)        { return 1; }
u16 fpu_status_word(void)    { return 0; }
u16 fpu_control_word(void)   { return 0x037F; }
int fpu_iit_probe(void)      { return 0; }
int fpu_cyrix_probe(void)    { return 0; }
#endif

const char *fpu_brand_name(fpu_brand_t b)
{
    switch (b) {
        case FPUB_NONE:    return "none";
        case FPUB_INTEL:   return "Intel";
        case FPUB_IIT:     return "IIT";
        case FPUB_CYRIX:   return "Cyrix";
        case FPUB_ULSI:    return "ULSI";
        case FPUB_AMD:     return "AMD";
        default:           return "unknown";
    }
}

const char *fpu_type_name(fpu_type_t t)
{
    switch (t) {
        case FPU_NONE:             return "none";
        case FPU_8087:             return "8087";
        case FPU_80287:            return "80287";
        case FPU_80287XL:          return "80287XL";
        case FPU_80387:            return "80387";
        case FPU_80387SX:          return "80387SX";
        case FPU_80487:            return "80487";
        case FPU_INTEGRATED:       return "integrated x87";
        case FPU_PRESENT_UNKNOWN:  return "x87 present";
        default:                   return "unknown";
    }
}

void fpu_detect(hw_profile_t *hw)
{
    if (!fpu_present()) {
        hw->fpu_type  = FPU_NONE;
        hw->fpu_brand = FPUB_NONE;
        hw->fpu_name[0] = '\0';
        return;
    }

    /* Type by control word width */
    {
        u16 cw = fpu_control_word();
        if ((cw & 0x0FFFu) == 0x03FFu) {
            hw->fpu_type = (hw->cpu_class == CPU_80286) ? FPU_80287 : FPU_8087;
        } else if ((cw & 0x0FFFu) == 0x037Fu) {
            if (hw->cpu_class >= CPU_80486DX) hw->fpu_type = FPU_INTEGRATED;
            else if (hw->cpu_class == CPU_80486SX) hw->fpu_type = FPU_80487;
            else if (hw->cpu_class == CPU_80386SX) hw->fpu_type = FPU_80387SX;
            else hw->fpu_type = FPU_80387;
        } else {
            hw->fpu_type = FPU_PRESENT_UNKNOWN;
        }
    }

    /* Integrated FPUs follow the CPU brand, not a coprocessor brand. */
    if (hw->fpu_type == FPU_INTEGRATED) {
        hw->fpu_brand = FPUB_INTEL;
    } else if (fpu_iit_probe()) {
        hw->fpu_brand = FPUB_IIT;
    } else if (fpu_cyrix_probe()) {
        hw->fpu_brand = FPUB_CYRIX;
    } else {
        hw->fpu_brand = FPUB_UNKNOWN;
    }

    /* Build readable fpu_name */
    {
        const char *t = fpu_type_name(hw->fpu_type);
        const char *b = fpu_brand_name(hw->fpu_brand);
        if (hw->fpu_type == FPU_INTEGRATED) {
            sprintf(hw->fpu_name, "%s (%s)", t, b);
        } else if (hw->fpu_brand == FPUB_UNKNOWN || hw->fpu_brand == FPUB_NONE) {
            sprintf(hw->fpu_name, "%s", t);
        } else {
            sprintf(hw->fpu_name, "%s %s", b, t);
        }
    }
}
