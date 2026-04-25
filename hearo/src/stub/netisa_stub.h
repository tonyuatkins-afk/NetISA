/*
 * stub/netisa_stub.h - In-process NetISA card emulation.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_STUB_NETISA_H
#define HEARO_STUB_NETISA_H

#include "../hearo.h"

void  nstub_enable(void);
hbool nstub_is_enabled(void);
hbool nstub_link_up(void);
const char *nstub_firmware(void);

#endif
