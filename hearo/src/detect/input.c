/*
 * detect/input.c - Mouse and joystick presence.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#include "input.h"

extern hbool inp_mouse(void);     /* INT 33h AX=0000h */
extern hbool inp_joystick(void);  /* port 201h sanity */

#ifdef HEARO_NOASM
hbool inp_mouse(void)    { return HFALSE; }
hbool inp_joystick(void) { return HFALSE; }
#endif

void input_detect(hw_profile_t *hw)
{
    hw->has_mouse    = inp_mouse();
    hw->has_joystick = inp_joystick();
}
