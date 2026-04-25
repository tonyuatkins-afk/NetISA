/*
 * test/testboot.c - Render the boot screen and exit on keypress.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#include "../src/hearo.h"
#include "../src/detect/detect.h"
#include "../src/unlock/unlock.h"
#include "../src/unlock/hall.h"
#include "../src/ui/boot.h"
#include "../src/config/cmdline.h"
#include "../src/stub/netisa_stub.h"

#include <string.h>

int main(int argc, char *argv[])
{
    hw_profile_t hw;
    memset(&hw, 0, sizeof(hw));
    cmdline_parse(argc, argv);
    if (cmdline_has("STUBNET")) nstub_enable();
    detect_all(&hw);
    unlock_evaluate(&hw);
    hall_load("HEARO.HAL");
    hall_update(&hw);
    boot_screen_render(&hw);
    return 0;
}
