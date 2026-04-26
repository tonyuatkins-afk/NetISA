/*
 * test/testplay.c - Standalone command-line player.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Usage:  TESTPLAY [/N]  filename
 *   /N    skip detection probes (assume defaults; useful when probes hang)
 *
 * Loads the file, picks an audio driver from detection, opens the mixer,
 * and plays until ESC.  Prints the active driver and the file format on
 * startup so the operator can see what is going on.
 */
#include "../src/hearo.h"
#include "../src/detect/detect.h"
#include "../src/audio/audiodrv.h"
#include "../src/audio/mixer.h"
#include "../src/audio/wake.h"
#include "../src/decode/decode.h"
#include "../src/platform/dos.h"
#include "../src/platform/timer.h"

#include <conio.h>
#include <dos.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Heap-allocated to keep the union out of DGROUP. */
static decode_handle_t *song;
static u32              mixer_rate = 22050;
static u8               mixer_format = AFMT_S16_STEREO;
static hbool            paused;
/* Frames rendered so far. Updated from the audio ISR. The main loop watches
 * this against a deadline so /T does not depend on the BIOS tick advancing,
 * which can stall when the SB ISR is busy. */
static volatile u32     frames_rendered;

/* Ctrl-Break / Ctrl-C handler. Without this, hitting either key during
 * playback returns control to DOS but leaves the SB IRQ vector pointing at
 * our (now reclaimed) ISR; the next IRQ jumps into freed memory and the
 * machine wedges. Shut the active driver down cleanly before exit. */
static void ctrl_break(int sig)
{
    const audio_driver_t *drv = audiodrv_active();
    if (drv && drv->shutdown) drv->shutdown();
    (void)sig;
    exit(130);
}

/* Audio callback: ask decoder to advance the song, then mix into the buffer.
 *
 * frames_rendered is incremented in BOTH the active and paused branches.
 * The watchdog in main() interprets a stalled counter as "ISR not firing";
 * not incrementing during pause meant a SPACE-paused player tripped the
 * watchdog after 2 seconds and printed a misleading "audio ISR not firing"
 * diagnostic, then bailed. The counter measures ISR firings (which still
 * happen during pause: the chip keeps DMA-ing zeros), not song progress,
 * so the increment belongs in both paths. */
static void play_callback(void *buffer, u16 samples, u8 format)
{
    if (paused) {
        memset(buffer, format >= AFMT_S16_MONO ? 0 : 0x80,
               (u32)samples * AFMT_FRAME_BYTES(format));
        frames_rendered += samples;
        return;
    }
    decode_advance(song, samples);
    mixer_render(buffer, samples, format);
    frames_rendered += samples;
}

static void usage(void)
{
    printf("HEARO TESTPLAY " HEARO_VER_STRING "\n");
    printf("Usage:  TESTPLAY [/N] [/Tn] filename\n");
    printf("  /N    skip detection probes (assume SB16 defaults)\n");
    printf("  /Tn   auto-exit after n seconds (headless test mode)\n");
    printf("Keys:   ESC quit  SPACE pause/resume  +/- volume\n");
}

int main(int argc, char *argv[])
{
    hw_profile_t hw;
    const audio_driver_t *drv;
    audio_caps_t caps;
    u8 master = 200;
    int i;
    const char *filename = 0;
    hbool skip_detect = HFALSE;
    u16   timeout_sec = 0;
    u32   frame_deadline = 0;

    /* Force unbuffered stdout so every printf hits disk immediately. With
     * `>> file` redirection, stdio uses block buffering by default and a
     * partial buffer is lost when the system hangs. Unbuffered mode trades
     * a tiny amount of throughput for diagnostic visibility. */
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("[trace] testplay entered main\n");

    /* Hook Ctrl-C and Ctrl-Break before any audio init so an abnormal
     * exit always uninstalls the SB ISR. */
    signal(SIGINT, ctrl_break);
    signal(SIGBREAK, ctrl_break);
    printf("[trace] signals registered\n");
    if (argc < 2) { usage(); return 1; }
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '/' || argv[i][0] == '-') {
            if (argv[i][1] == 'N' || argv[i][1] == 'n') skip_detect = HTRUE;
            else if (argv[i][1] == 'T' || argv[i][1] == 't') timeout_sec = (u16)atoi(argv[i] + 2);
            else if (argv[i][1] == '?' || argv[i][1] == 'h') { usage(); return 0; }
        } else {
            filename = argv[i];
        }
    }
    if (!filename) { usage(); return 1; }

    printf("[trace] argv parsed\n");
    memset(&hw, 0, sizeof(hw));
    if (skip_detect) {
        hw.aud_devices = AUD_SB_16 | AUD_PCSPEAKER;
        hw.sb.base = 0x220; hw.sb.irq = 5;
        hw.sb.dma_lo = 1; hw.sb.dma_hi = 5;
        hw.sb.dsp_major = 4; hw.sb.dsp_minor = 5;
    } else {
        printf("[trace] calling detect_all\n");
        detect_all(&hw);
        printf("[trace] detect_all returned\n");
    }

    printf("[trace] malloc song\n");
    song = (decode_handle_t *)malloc(sizeof(decode_handle_t));
    if (!song) { printf("ERROR: out of memory\n"); return 2; }

    printf("Loading %s...\n", filename);
    if (!decode_load(filename, song)) {
        printf("ERROR: could not decode %s\n", filename);
        return 2;
    }
    printf("Loaded: %s\n", song->title[0] ? song->title : filename);

    /* Register wake backends BEFORE audiodrv_auto_select so sb_init's
     * wake_chip call sees them. Without this, iron testing on a chip
     * that needs vendor pre-init (notably YMF715 OPL3-SAx on the
     * Toshiba 320CDT) goes through TESTPLAY without the wake step
     * and stalls after one half-block. Same call lives in main()
     * for the HEARO.EXE path. */
    wake_register_all();

    printf("[trace] audiodrv_auto_select\n");
    if (!audiodrv_auto_select(&hw)) {
        printf("ERROR: no audio driver available\n");
        decode_free(song);
        return 3;
    }
    drv = audiodrv_active();
    drv->get_caps(&caps);
    printf("Driver: %s (%s)\n", caps.name, caps.chip);

    /* If the active card has no PCM stream (AdLib, MPU401), bail with an
     * informative message; FM/MIDI playback comes online in Phase 3. */
    if (caps.formats == 0) {
        printf("Active driver has no PCM output (FM / MIDI only). Phase 3.\n");
        decode_free(song);
        return 4;
    }

    printf("[trace] mixer_init\n");
    mixer_init(mixer_rate, mixer_format, hw.fpu_type != FPU_NONE);
    mixer_set_master(master);
    printf("[trace] decode_start\n");
    decode_start(song, mixer_rate);
    /* Prime mixer state. MOD's decode_start only resets sequencer state;
     * the first row of channel data isn't pushed until decode_advance runs.
     * Doing it here means the very first ISR sees an already-active mixer. */
    printf("[trace] decode_advance prime\n");
    decode_advance(song, 1);

    printf("[trace] drv->open\n");
    if (!drv->open(mixer_rate, mixer_format, play_callback)) {
        printf("ERROR: driver open failed\n");
        decode_free(song);
        return 5;
    }
    printf("[trace] drv->open returned ok\n");

    if (timeout_sec) {
        printf("Playing for %u seconds...\n", (unsigned)timeout_sec);
        frame_deadline = (u32)timeout_sec * mixer_rate;
    } else {
        printf("Playing.  ESC quit  SPACE pause  +/- volume\n");
    }
    /* No-progress watchdog. If frames_rendered does not advance within
     * ~2 seconds of wall clock, the audio ISR is not firing. Common cause
     * on real iron is an audio chip that responds to SB DSP queries but
     * needs a vendor init utility (UNISOUND, YMFSB.EXE, OPL3SAX.EXE)
     * before its PCM engine is enabled. Without this watchdog, TESTPLAY
     * hangs forever and CTRL-C leaves DMA buffers / IRQ vectors hooked,
     * wedging COMMAND.COM.
     *
     * On watchdog fire, attempt fallback to the PC speaker driver. Quality
     * is much lower (PIT-driven 4-bit-ish PWM at <=18 kHz mono) but the
     * speaker is hardware that always works on any PC; this gives the
     * operator audible output even when the SB chip is in detect-but-no-
     * output mode (the YMF715-without-vendor-init case). */
    {
        u32   last_frames    = frames_rendered;
        u32   watchdog_start = timer_ticks();
        hbool tried_fallback = HFALSE;
        while (1) {
            if (timeout_sec && frames_rendered >= frame_deadline) break;
            if (frames_rendered != last_frames) {
                last_frames    = frames_rendered;
                watchdog_start = timer_ticks();
            } else if ((timer_ticks() - watchdog_start) > 36UL) {
                /* 36 BIOS ticks ~= 2 seconds. */
                if (!tried_fallback) {
                    char *fb_env = getenv("PCSPEAKER_FALLBACK");
                    hbool try_fb = (fb_env && fb_env[0] && fb_env[0] != '0') ? HTRUE : HFALSE;
                    printf("\nWARNING: audio ISR not firing (no frames in 2 sec).\n");
                    fflush(stdout);
                    if (try_fb) {
                        const audio_driver_t *spk;
                        printf("Falling back to PC speaker (PCSPEAKER_FALLBACK env set)...\n");
                        fflush(stdout);
                        /* WARNING: the PC speaker driver reprograms PIT ch0 to ~18 kHz
                         * and toggles the port-0x61 speaker enable bits. On some
                         * chipsets (verified on the Toshiba 320CDT YMF715 OPL3-SAx,
                         * 2026-04-25) this wedges the system, hence the opt-in. */
                        spk = audiodrv_find("pcspeaker");
                        if (spk && spk->open && spk->init) {
                            audiodrv_set_active(spk);
                            spk->init(0);
                            mixer_rate   = 18000UL;
                            mixer_format = AFMT_U8_MONO;
                            mixer_init(mixer_rate, mixer_format, hw.fpu_type != FPU_NONE);
                            mixer_set_master(master);
                            decode_start(song, mixer_rate);
                            decode_advance(song, 1);
                            if (spk->open(mixer_rate, mixer_format, play_callback)) {
                                printf("PC speaker active at %lu Hz mono.\n",
                                       (unsigned long)mixer_rate);
                                fflush(stdout);
                                frames_rendered = 0;
                                last_frames     = 0;
                                frame_deadline  = (u32)timeout_sec * mixer_rate;
                                watchdog_start  = timer_ticks();
                                tried_fallback  = HTRUE;
                                drv             = audiodrv_active();
                                continue;
                            }
                            printf("PC speaker open failed.\n");
                        }
                    }
                    printf("The SB chip likely needs a vendor init utility before\n");
                    printf("SB-mode PCM works (UNISOUND, YMFSB.EXE, OPL3SAX.EXE).\n");
                    printf("On Win98 SE try running from a Win98 DOS Prompt instead\n");
                    printf("of MS-DOS mode (the VxD initializes the chip for you).\n");
                    if (!try_fb) {
                        printf("(Set PCSPEAKER_FALLBACK=1 to attempt PIT-driven speaker\n");
                        printf("playback as a degraded-quality fallback.)\n");
                    }
                    break;
                }
                /* Fallback also stalled. Give up cleanly. */
                printf("\nERROR: PC speaker fallback also not progressing. Giving up.\n");
                break;
            }
            if (kbhit()) {
                int k = getch();
                if (k == 0x1B) break;
                if (k == ' ')  paused = !paused;
                if (k == '+' || k == '=') {
                    if (master < 250) master += 10;
                    mixer_set_master(master);
                }
                if (k == '-' || k == '_') {
                    if (master >= 10) master -= 10;
                    mixer_set_master(master);
                }
                if (k == 0) (void)getch();   /* extended key, discard */
            }
        }
    }
    printf("Done. frames=%lu\n", (unsigned long)frames_rendered);

    drv->close();
    decode_free(song);
    return 0;
}
