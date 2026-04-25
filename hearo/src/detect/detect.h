/*
 * detect/detect.h - Master detection orchestrator.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_DETECT_H
#define HEARO_DETECT_H

#include "../hearo.h"

/* Run every probe and populate hw. Caller zeroes hw first.
 * Honours cmdline /SAFE (skip all audio probes) and /STUBNET. */
void detect_all(hw_profile_t *hw);

/* Compute a stable 32 bit hash over the profile, used as machine fingerprint. */
u32  detect_fingerprint(const hw_profile_t *hw);

#endif
