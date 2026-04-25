/*
 * detect/video.h - Video adapter, chipset, and VESA detection.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_DETECT_VIDEO_H
#define HEARO_DETECT_VIDEO_H

#include "../hearo.h"

void video_detect(hw_profile_t *hw);
const char *video_class_name(video_class_t c);
const char *svga_chipset_name(svga_chipset_t c);
const char *video_tier_name(video_tier_t t);

#endif
