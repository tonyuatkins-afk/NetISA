/*
 * detect/cpu.h - CPU class and clock detection.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_DETECT_CPU_H
#define HEARO_DETECT_CPU_H

#include "../hearo.h"

void cpu_detect(hw_profile_t *hw);
const char *cpu_name(cpu_class_t c);

#endif
