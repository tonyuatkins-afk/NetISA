/*
 * detect/detect.c - Master orchestrator.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Runs every probe in dependency order and computes the machine fingerprint.
 */
#include "detect.h"
#include "cpu.h"
#include "fpu.h"
#include "video.h"
#include "audio.h"
#include "memory.h"
#include "netisa.h"
#include "input.h"
#include "../config/cmdline.h"
#include <string.h>
#include <stdio.h>

extern void dos_get_date(char *out11);

#ifdef HEARO_NOASM
void dos_get_date(char *o) { strcpy(o, "2026-04-24"); }
#endif

u32 detect_fingerprint(const hw_profile_t *hw)
{
    /* FNV-1a over the stable parts of the profile. */
    const u8 *p = (const u8 *)hw;
    u32 h = 2166136261UL;
    u32 i;
    /* Hash only the early, deterministic part: cpu_class through aud_devices. */
    u32 n = (u32)((const u8 *)&hw->fingerprint - (const u8 *)hw);
    for (i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619UL;
    }
    return h;
}

void detect_all(hw_profile_t *hw)
{
    hbool safe = cmdline_has("SAFE");
    hbool stub = cmdline_has("STUBNET");

    cpu_detect(hw);
    fpu_detect(hw);
    memory_detect(hw);
    video_detect(hw);

    if (!safe) {
        audio_detect(hw);
    } else {
        hw->aud_devices = AUD_PCSPEAKER;
        hw->aud_card_count = 0;
        strcpy(hw->aud_cards[hw->aud_card_count++], "PC Speaker (PIT ch2)");
    }

    netisa_detect(hw, stub);
    input_detect(hw);

    dos_get_date(hw->detect_date);
    hw->fingerprint = detect_fingerprint(hw);
}
