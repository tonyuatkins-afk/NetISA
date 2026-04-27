/*
 * audio/audiodrv.c - Audio driver registry and active-driver routing.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#pragma off (check_stack)  /* see Makefile CF16_ISR -- belt-and-braces */
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

/* Driver lifecycle contract:
 *   1. audiodrv_register() adds the static descriptor to the table.
 *   2. audiodrv_set_active() makes a registered driver the current one.
 *      If a different driver was previously active, its shutdown() runs
 *      first to release IRQ vectors, DMA channels, and DSP state.
 *   3. After set_active, the caller must call drv->init() before drv->open().
 *      audiodrv_auto_select handles this for the auto-pick path; manual
 *      callers MUST call init themselves. set_active does NOT init for
 *      them because the hw_profile_t may not be in scope at the call site.
 *   4. open / close pairs may repeat any number of times after init.
 *   5. shutdown() is the terminator and is safe to call multiple times.
 *      A subsequent open() requires a fresh init().
 *
 * Re-activating a driver that was previously shutdown REQUIRES re-init:
 *   set_active(drvA);  drvA->init(hw);  drvA->open(...);  drvA->close();
 *   set_active(drvB);  // calls drvA->shutdown internally
 *   set_active(drvA);  drvA->init(hw);  // MUST re-init
 */
hbool audiodrv_set_active(const audio_driver_t *drv)
{
    if (active && active != drv && active->shutdown) active->shutdown();
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

    /* Re-running auto-select on a hot system (config-changed reselect, hot
     * unplug fallback) must shut down the previously active driver first.
     * Otherwise its ISR vector + DMA channel + DSP state remain hooked
     * while we init a new driver against the same IRQ/DMA/PIC slots, which
     * leaves two ISRs racing on one PIC line. */
    if (active && active->shutdown) active->shutdown();
    active = 0;

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
