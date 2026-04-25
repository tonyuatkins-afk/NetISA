/*
 * detect/netisa.h - NetISA card probe (real or stub).
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_DETECT_NETISA_H
#define HEARO_DETECT_NETISA_H

#include "../hearo.h"

void netisa_detect(hw_profile_t *hw, hbool stub_mode);

#endif
