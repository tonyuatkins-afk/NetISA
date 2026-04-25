/*
 * stub/netisa_stub.c - In-process NetISA card emulation.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * /STUBNET on the command line activates this. It exists so streaming and
 * NetISA dependent code paths can be exercised on a workstation that has no
 * card on the bus. A real NetISA card replaces this with its register
 * interface.
 */
#include "netisa_stub.h"

static hbool enabled = HFALSE;

void nstub_enable(void)        { enabled = HTRUE; }
hbool nstub_is_enabled(void)   { return enabled; }
hbool nstub_link_up(void)      { return enabled; }
const char *nstub_firmware(void) { return "stub-1.0.0"; }
