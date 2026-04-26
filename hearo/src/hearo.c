/*
 * hearo.c - HEARO main entry point.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#include "hearo.h"
#include "detect/detect.h"
#include "detect/cpu.h"
#include "detect/fpu.h"
#include "detect/video.h"
#include "detect/audio.h"
#include "unlock/unlock.h"
#include "unlock/hall.h"
#include "unlock/whisper.h"
#include "ui/boot.h"
#include "ui/ui.h"
#include "ui/screen.h"
#include "ui/hallview.h"
#include "ui/settings.h"
#include "ui/playback.h"
#include "config/config.h"
#include "config/cmdline.h"
#include "stub/netisa_stub.h"
#include "audio/audiodrv.h"
#include "audio/mixer.h"

#include <stdio.h>
#include <string.h>

static void print_version(void)
{
    printf("HEARO " HEARO_VER_STRING " " HEARO_COPYRIGHT "\n");
    printf("NetISA Music Player.\n");
}

static void print_unlocks(void)
{
    u16 i;
    const unlock_entry_t *all = unlock_get_all();
    printf("HEARO Unlock Matrix\n");
    printf("-------------------\n");
    for (i = 0; i < UL_COUNT; i++) {
        const unlock_entry_t *e = &all[i];
        const char *mark = e->unlocked ? "[*]" : "[ ]";
        printf("%s %-28s %s\n",
               mark,
               e->name ? e->name : "(unspecified)",
               e->unlocked ? "" : (e->requirement ? e->requirement : ""));
    }
    printf("\n%u of %u features unlocked.\n", unlock_count_enabled(), (unsigned)UL_COUNT);
}

static void print_hall(const hw_profile_t *hw)
{
    u16 i, n;
    printf("Hall of Recognition for fingerprint %08lX\n", (unsigned long)hw->fingerprint);
    printf("First boot: %s   Boot count: %u\n\n", hall_first_date(), hall_boot_count());
    n = hall_event_count();
    for (i = 0; i < n; i++) {
        const hall_event_t *e = hall_event(i);
        if (!e) break;
        printf("%s  %-7s %s\n", e->date, e->category, e->text);
    }
}

int main(int argc, char *argv[])
{
    hw_profile_t hw;
    memset(&hw, 0, sizeof(hw));

    cmdline_parse(argc, argv);

    if (cmdline_has("VERSION")) { print_version(); return 0; }

    if (cmdline_has("STUBNET")) nstub_enable();

    config_load("HEARO.CFG");
    detect_all(&hw);
    unlock_evaluate(&hw);
    hall_load("HEARO.HAL");
    hall_update(&hw);

    if (cmdline_has("UNLOCKS")) { print_unlocks(); return 0; }
    if (cmdline_has("HALL"))    { print_hall(&hw); return 0; }

    boot_screen_render(&hw);

    hall_save("HEARO.HAL");
    config_save("HEARO.CFG");

    /* Bring the audio engine up before the UI so the now-playing pane has a
     * valid driver to talk to.  audiodrv_auto_select falls back to the null
     * driver if nothing else opens, so failure here is non-fatal. */
    audiodrv_register_all();
    audiodrv_auto_select(&hw);
    mixer_init(22050UL, AFMT_S16_STEREO, hw.fpu_type != FPU_NONE);
    playback_init(22050UL, AFMT_S16_STEREO);

    ui_run(&hw);

    /* Close any in-flight track via the playback module so the driver
     * close path runs before the unconditional shutdown below. The two
     * are not strictly redundant: shutdown is a one-shot, while close
     * is the per-session teardown that the chunk-A sb_close hardening
     * sequences correctly against a wedged chip. */
    playback_stop();

    if (audiodrv_active() && audiodrv_active()->shutdown)
        audiodrv_active()->shutdown();

    return 0;
}
