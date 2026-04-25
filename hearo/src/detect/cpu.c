/*
 * detect/cpu.c - CPU class and clock detection.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Strategy:
 *   1. FLAGS bit walk (12-13 IOPL, 18 AC, 21 ID) classifies 8086/186/286/386/486+.
 *   2. CPUID (only if EFLAGS bit 21 toggleable) gives family/model.
 *   3. PIT channel 2 timing measures the actual core clock; we match against
 *      a list of period nominals and flag overclock when measured >10% above.
 *
 * The PIT timing path is intentionally simple: it produces a best effort MHz
 * for display purposes, not a benchmark number. The classification path is
 * authoritative.
 */
#include "cpu.h"
#include <stdio.h>
#include <string.h>

/* External assembly stubs (asm/cpustubs.asm) */
extern int  cpu_flag_test(void);     /* returns class hint: 0=8086,1=186,2=286,3=386,4=486+ */
extern int  cpu_has_cpuid(void);
extern void cpu_cpuid(u32 leaf, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx);
extern u32  cpu_pit_loop(u16 ticks); /* returns inner loop count for a PIT interval */

#ifdef HEARO_NOASM
/* Fallback stubs let the host compiler build; numbers are synthetic. */
int  cpu_flag_test(void) { return 4; }
int  cpu_has_cpuid(void) { return 0; }
void cpu_cpuid(u32 l, u32 *a, u32 *b, u32 *c, u32 *d) { (void)l; *a=*b=*c=*d=0; }
u32  cpu_pit_loop(u16 t)  { (void)t; return 100000UL; }
#endif

static u16 nominal_clocks[] = {
    4, 5, 6, 8, 10, 12, 16, 20, 25, 33, 40, 50, 66, 75, 90, 100, 120, 133, 0
};

static u16 nearest_nominal(u16 mhz)
{
    u16 best = 0;
    u16 i;
    long bestd = 0x7FFFL;
    for (i = 0; nominal_clocks[i]; i++) {
        long d = (long)mhz - (long)nominal_clocks[i];
        if (d < 0) d = -d;
        if (d < bestd) { bestd = d; best = nominal_clocks[i]; }
    }
    return best;
}

const char *cpu_name(cpu_class_t c)
{
    switch (c) {
        case CPU_8088:    return "Intel 8088";
        case CPU_8086:    return "Intel 8086";
        case CPU_80186:   return "Intel 80186";
        case CPU_80286:   return "Intel 80286";
        case CPU_80386SX: return "Intel 80386SX";
        case CPU_80386DX: return "Intel 80386DX";
        case CPU_80486SX: return "Intel 80486SX";
        case CPU_80486DX: return "Intel 80486DX";
        case CPU_PENTIUM: return "Intel Pentium";
        default:          return "Unknown CPU";
    }
}

void cpu_detect(hw_profile_t *hw)
{
    int hint;
    u32 loop_count;
    u32 mhz;

    hint = cpu_flag_test();
    switch (hint) {
        case 0: hw->cpu_class = CPU_8088;    break;
        case 1: hw->cpu_class = CPU_80186;   break;
        case 2: hw->cpu_class = CPU_80286;   break;
        case 3: hw->cpu_class = CPU_80386DX; break;
        case 4: hw->cpu_class = CPU_80486DX; break;
        default: hw->cpu_class = CPU_UNKNOWN;
    }

    if (cpu_has_cpuid()) {
        u32 eax = 0, ebx = 0, ecx = 0, edx = 0;
        cpu_cpuid(1, &eax, &ebx, &ecx, &edx);
        {
            u8 family = (u8)((eax >> 8) & 0x0F);
            if (family == 4) {
                /* Distinguish DX vs SX via FPU presence: rough but workable. */
                hw->cpu_class = (edx & 1UL) ? CPU_80486DX : CPU_80486SX;
            } else if (family == 5) {
                hw->cpu_class = CPU_PENTIUM;
            } else if (family >= 6) {
                hw->cpu_class = CPU_PENTIUM; /* clamp to highest known */
            }
        }
    }

    /* PIT timing -> approximate MHz. The constant 1000 is calibrated for the
     * tight loop in cpu_pit_loop; tune in DOSBox-X then on real iron. */
    loop_count = cpu_pit_loop(1193); /* one PIT tick at the standard rate */
    if (loop_count == 0) loop_count = 1;
    mhz = (loop_count + 999UL) / 1000UL;
    if (mhz > 999) mhz = 999;
    hw->cpu_mhz = (u16)mhz;
    hw->cpu_nominal_mhz = nearest_nominal(hw->cpu_mhz);
    hw->cpu_overclock = (hw->cpu_mhz > (hw->cpu_nominal_mhz + (hw->cpu_nominal_mhz / 10))) ? HTRUE : HFALSE;
}
