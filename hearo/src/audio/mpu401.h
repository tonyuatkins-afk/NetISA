/*
 * audio/mpu401.h - MPU-401 helpers used by the MIDI sequencer.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_AUDIO_MPU401_H
#define HEARO_AUDIO_MPU401_H

#include "../hearo.h"

void mpu_send       (u8 val);
void mpu_send_msg   (u8 status, u8 d1, u8 d2);
void mpu_send_sysex (const u8 *data, u16 len);

#endif
