/*
 * unlock/whisper.h - Dormant feature whisper system.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_WHISPER_H
#define HEARO_WHISPER_H

#include "../hearo.h"

void whisper_check(const unlock_entry_t *unlocks);
hbool whisper_pending(void);
const char *whisper_message(void);
void whisper_dismiss(void);

#endif
