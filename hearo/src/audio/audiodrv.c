/*
 * audio/audiodrv.c - Audio driver registry and active-driver routing.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#include "audiodrv.h"
#include <string.h>

extern const audio_driver_t sb_driver;
extern const audio_driver_t gus_driver;
extern const audio_driver_t adlib_driver;
extern const audio_driver_t mpu401_driver;
extern const audio_driver_t pcspeaker_driver;
extern const audio_driver_t null_driver;

static const audio_driver_t *registry[AUDIODRV_MAX];
static int                   registry_count;
static const audio_driver_t *active;
static const audio_driver_t *hardware_mixer;    /* GUS-class voice card */

void audiodrv_register(const audio_driver_t *drv)
{
    if (!drv || registry_count >= AUDIODRV_MAX) return;
    registry[registry_count++] = drv;
}

int audiodrv_count(void) { return registry_count; }

const audio_driver_t *audiodrv_get(int index)
{
    if (index < 0 || index >= registry_count) return 0;
    return registry[index];
}

const audio_driver_t *audiodrv_find(const char *name)
{
    int i;
    if (!name) return 0;
    for (i = 0; i < registry_count; i++) {
        if (registry[i]->name && strcmp(registry[i]->name, name) == 0)
            return registry[i];
    }
    return 0;
}

hbool audiodrv_set_active(const audio_driver_t *drv)
{
    if (active && active->shutdown) active->shutdown();
    active = drv;
    return active ? HTRUE : HFALSE;
}

const audio_driver_t *audiodrv_active(void) { return active; }

void audiodrv_register_all(void)
{
    if (registry_count > 0) return;
    audiodrv_register(&sb_driver);
    audiodrv_register(&gus_driver);
    audiodrv_register(&adlib_driver);
    audiodrv_register(&mpu401_driver);
    audiodrv_register(&pcspeaker_driver);
    audiodrv_register(&null_driver);
}

hbool audiodrv_auto_select(const hw_profile_t *hw)
{
    const audio_driver_t *pick = 0;

    audiodrv_register_all();

    /* SB is preferred as the audio output target because it has a working
     * ISR/DMA path that drives sequencer timing for all decoders. GUS is
     * inited as a SECONDARY hardware-mixer when present, used for direct
     * voice triggering by tracker formats. */
    if (hw->aud_devices & (AUD_SB_16 | AUD_SB_16ASP |
                           AUD_SB_AWE32 | AUD_SB_AWE64 |
                           AUD_SB_PRO | AUD_SB_PRO2 |
                           AUD_SB_1X | AUD_SB_20))
        pick = audiodrv_find("sb");
    if (!pick && (hw->aud_devices & (AUD_GUS | AUD_GUS_MAX | AUD_GUS_PNP | AUD_GUS_ACE)))
        pick = audiodrv_find("gus");
    if (!pick && (hw->aud_devices & (AUD_ADLIB | AUD_ADLIB_GOLD)))
        pick = audiodrv_find("adlib");
    if (!pick && (hw->aud_devices & AUD_PCSPEAKER))
        pick = audiodrv_find("pcspeaker");
    if (!pick) pick = audiodrv_find("null");

    if (!pick) return HFALSE;
    if (pick->init && !pick->init(hw)) {
        /* Roll back any partial init residue (DMA mask bits, ISR vectors)
         * before substituting. shutdown() must be safe to call after a
         * failed init, which is true for every driver here. */
        if (pick->shutdown) pick->shutdown();
        pick = audiodrv_find("null");
        if (pick && pick->init) pick->init(hw);
    }
    audiodrv_set_active(pick);

    /* Init the GUS as a secondary if present and not already active.
     * Identify GUS via strcmp rather than first-letter matching so we do
     * not collide with future drivers whose name happens to start with 'g'. */
    hardware_mixer = 0;
    if (hw->aud_devices & (AUD_GUS | AUD_GUS_MAX | AUD_GUS_PNP | AUD_GUS_ACE)) {
        const audio_driver_t *gus = audiodrv_find("gus");
        if (pick == gus) {
            hardware_mixer = gus;
        } else if (gus && gus->init && gus->init(hw)) {
            hardware_mixer = gus;
        }
    }
    return HTRUE;
}

const audio_driver_t *audiodrv_get_hardware_mixer(void) { return hardware_mixer; }
