/*
 * detect/audio.h - Probe every period appropriate audio device.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_DETECT_AUDIO_H
#define HEARO_DETECT_AUDIO_H

#include "../hearo.h"

void audio_detect(hw_profile_t *hw);
const char *opl_name(opl_type_t o);
const char *midi_synth_name(midi_synth_t s);

#endif
