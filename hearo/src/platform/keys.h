/*
 * platform/keys.h - Keyboard helper aliases.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * The KEY_* defines themselves live in hearo.h (so any module can compare a
 * scancode|ascii word to KEY_F1, etc). This header documents the helper
 * convention used across the UI: a fetched key is `(scan << 8) | ascii`.
 *
 * Nothing else lives here so we have a single point to attach future keymap
 * support without touching every call site.
 */
#ifndef HEARO_PLATFORM_KEYS_H
#define HEARO_PLATFORM_KEYS_H

#include "../hearo.h"

#define KEY_NONE 0x0000

#endif
