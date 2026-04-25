/*
 * test/testdet.c - Detect everything and dump the master profile.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#include "../src/hearo.h"
#include "../src/detect/detect.h"
#include "../src/detect/cpu.h"
#include "../src/detect/fpu.h"
#include "../src/detect/video.h"
#include "../src/detect/audio.h"
#include "../src/config/cmdline.h"
#include "../src/unlock/unlock.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
    hw_profile_t hw;
    u8 i;
    memset(&hw, 0, sizeof(hw));

    cmdline_parse(argc, argv);
    detect_all(&hw);
    unlock_evaluate(&hw);

    printf("HEARO TESTDET %s\n", HEARO_VER_STRING);
    printf("===============================\n");
    printf("CPU:    %s @ %u MHz%s (nominal %u MHz)\n",
           cpu_name(hw.cpu_class), hw.cpu_mhz,
           hw.cpu_overclock ? " [oc]" : "", hw.cpu_nominal_mhz);
    printf("FPU:    %s\n", hw.fpu_name[0] ? hw.fpu_name : "none");
    printf("Memory: %uK conv + %luK XMS + %luK EMS\n",
           hw.mem_conv_kb, hw.mem_xms_kb, hw.mem_ems_kb);
    printf("Video:  %s (tier %u)\n", hw.vid_name, (unsigned)hw.vid_tier);
    if (hw.vid_class == VID_SVGA) {
        printf("        VESA %u.%u, %ux%u, %uKB%s\n",
               hw.vesa.ver_major, hw.vesa.ver_minor,
               hw.vesa.max_w, hw.vesa.max_h, hw.vesa.vram_kb,
               hw.vesa.has_lfb ? " [LFB]" : "");
    }

    printf("Audio:  %u cards detected (mask 0x%08lX)\n",
           hw.aud_card_count, (unsigned long)hw.aud_devices);
    for (i = 0; i < hw.aud_card_count; i++) {
        printf("        - %s\n", hw.aud_cards[i]);
    }
    if (hw.aud_devices & (AUD_SB_1X|AUD_SB_20|AUD_SB_PRO|AUD_SB_PRO2|
                          AUD_SB_16|AUD_SB_16ASP|AUD_SB_AWE32|AUD_SB_AWE64)) {
        printf("        SB:   base=%03Xh irq=%u dma=%u/%u dsp=%u.%02u%s\n",
               hw.sb.base, hw.sb.irq, hw.sb.dma_lo, hw.sb.dma_hi,
               hw.sb.dsp_major, hw.sb.dsp_minor,
               hw.sb.has_asp ? " [ASP]" : "");
    }
    if (hw.aud_devices & (AUD_GUS|AUD_GUS_MAX|AUD_GUS_ACE|AUD_GUS_PNP)) {
        printf("        GUS:  base=%03Xh irq=%u dma=%u ram=%luK%s%s\n",
               hw.gus.base, hw.gus.irq, hw.gus.dma, hw.gus.ram_kb,
               hw.gus.has_db ? " [DB]" : "",
               hw.gus.has_codec ? " [CODEC]" : "");
    }
    if (hw.aud_devices & AUD_MPU401) printf("        MPU:  %03Xh\n", hw.mpu_base);
    if (hw.midi_synth != MIDI_NONE)  printf("        MIDI: %s\n", hw.midi_name);

    printf("NetISA: %s\n",
           hw.nisa_status == NISA_LINK_UP ? "card present (or stub)" :
           hw.nisa_status == NISA_NO_LINK ? "card present, no link" : "not found");
    if (hw.nisa_status != NISA_NOT_FOUND) {
        printf("        base=%03Xh fw=%s\n", hw.nisa_base, hw.nisa_fw);
    }
    printf("Input:  mouse=%s joystick=%s\n",
           hw.has_mouse ? "yes" : "no",
           hw.has_joystick ? "yes" : "no");
    printf("Date:   %s\n", hw.detect_date);
    printf("Fingerprint: %08lX\n", (unsigned long)hw.fingerprint);
    printf("Unlocks unlocked: %u of %u\n", unlock_count_enabled(), (unsigned)UL_COUNT);

    return 0;
}
