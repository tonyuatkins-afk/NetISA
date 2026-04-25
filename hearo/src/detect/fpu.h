/*
 * detect/fpu.h - FPU presence and brand detection.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_DETECT_FPU_H
#define HEARO_DETECT_FPU_H

#include "../hearo.h"

void fpu_detect(hw_profile_t *hw);
const char *fpu_brand_name(fpu_brand_t b);
const char *fpu_type_name(fpu_type_t t);

#endif
