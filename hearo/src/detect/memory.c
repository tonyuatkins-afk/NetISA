/*
 * detect/memory.c - Conventional, XMS, and EMS probes.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#include "memory.h"

extern u16 mem_int12(void);                          /* INT 12h */
extern u32 mem_xms_total_kb(void);                   /* INT 2Fh / XMS host */
extern u32 mem_ems_total_kb(void);                   /* INT 67h */

#ifdef HEARO_NOASM
u16 mem_int12(void)         { return 640; }
u32 mem_xms_total_kb(void)  { return 16384UL; }
u32 mem_ems_total_kb(void)  { return 0; }
#endif

void memory_detect(hw_profile_t *hw)
{
    hw->mem_conv_kb = mem_int12();
    hw->mem_xms_kb  = mem_xms_total_kb();
    hw->mem_ems_kb  = mem_ems_total_kb();
}
