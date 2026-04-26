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
#include "platform/dos.h"

#include <dos.h>      /* _harderr, _HARDERR_FAIL */
#include <signal.h>   /* signal, SIGINT, SIGBREAK */
#include <stdio.h>
#include <stdlib.h>   /* atexit, exit */
#include <string.h>

/* Shutdown is centralised here so every exit path runs the same teardown
 * exactly once. The g_shutdown_done flag makes run_shutdown idempotent so
 * calling it from main, atexit, AND the signal handler is safe (whichever
 * fires first wins; subsequent calls short-circuit). */
static hbool g_shutdown_done = HFALSE;

static void run_shutdown(void)
{
    if (g_shutdown_done) return;
    g_shutdown_done = HTRUE;
    playback_stop();
    if (audiodrv_active() && audiodrv_active()->shutdown)
        audiodrv_active()->shutdown();
    if (!dos_mcb_validate()) {
        fprintf(stderr,
                "WARNING: DOS MCB chain corrupt at exit. The system may\n"
                "be unstable until reboot. Please report this with the\n"
                "active driver name and the file that was last played.\n");
    }
}

/* Ctrl-C (INT 23h via SIGINT) and Ctrl-Break (INT 1Bh via SIGBREAK) handler.
 * Without this, hitting either key during playback returns control to DOS
 * but leaves the SB IRQ vector pointing at our (now reclaimed) ISR; the next
 * IRQ jumps into freed memory and the machine wedges. Same shape testplay.c
 * uses, just calling our shared run_shutdown so the MCB validation also
 * runs on the abnormal-exit path. */
static void ctrl_break_handler(int sig)
{
    (void)sig;
    run_shutdown();
    exit(130);
}

/* INT 24h critical-error handler. Runs from DOS context with stack switched
 * and DOS itself not reentrant; do nothing here that touches the file system,
 * keyboard, or screen. Just return _HARDERR_FAIL so the failed I/O bubbles
 * up to the caller as a normal libc error rather than DOS prompting
 * "Abort, Retry, Fail" over our text-mode UI. Cleanup happens later via
 * atexit when the caller eventually exits. */
static int hard_error_handler(unsigned deverr,
                              unsigned errcode,
                              unsigned __far *devhdr)
{
    (void)deverr; (void)errcode; (void)devhdr;
    return _HARDERR_FAIL;
}

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

    /* Install cleanup handlers BEFORE any work that could fail or be
     * interrupted. atexit covers normal exit and any explicit exit() call;
     * SIGINT/SIGBREAK cover Ctrl-C and Ctrl-Break; _harderr covers DOS
     * critical errors (disk not ready, write-protect, etc.) which would
     * otherwise prompt the user mid-UI and corrupt the screen. The shared
     * run_shutdown is idempotent so calling it from multiple paths is safe. */
    atexit(run_shutdown);
    signal(SIGINT,   ctrl_break_handler);
    signal(SIGBREAK, ctrl_break_handler);
    _harderr(hard_error_handler);

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

    /* Explicit shutdown for the documented normal-exit path. atexit will
     * also fire after `return 0` below, and run_shutdown's g_shutdown_done
     * flag short-circuits the second call. The redundancy is deliberate:
     * keeps the shutdown site visible to anyone reading main() while still
     * guaranteeing cleanup on exit() / SIGINT / SIGBREAK paths via the
     * registered handlers. */
    run_shutdown();

    return 0;
}
