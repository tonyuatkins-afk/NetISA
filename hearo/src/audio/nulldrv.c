/*
 * audio/nulldrv.c - Silent fallback driver.  Always succeeds.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#include "audiodrv.h"

static hbool n_init(const hw_profile_t *hw) { (void)hw; return HTRUE; }
static void  n_shutdown(void) {}
static hbool n_open(u32 r, u8 f, audio_callback_t cb) { (void)r; (void)f; (void)cb; return HTRUE; }
static void  n_close(void) {}
static void  n_volume(u8 v) { (void)v; }
static void  n_caps(audio_caps_t *c)
{
    c->name = "Null"; c->chip = "(silent)";
    c->formats = 0xF; c->max_rate = 44100;
    c->max_channels = 2; c->max_bits = 16;
    c->has_hardware_mix = HFALSE;
    c->hardware_voices = 0; c->sample_ram = 0;
}

const audio_driver_t null_driver = {
    "null", n_init, n_shutdown, n_open, n_close, n_volume, n_caps, 0, 0, 0
};
