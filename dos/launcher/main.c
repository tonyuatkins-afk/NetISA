/*
 * main.c - NETISA.EXE launcher shell
 *
 * Full-screen control panel for NetISA card configuration.
 * Uses screen.h for rendering and netisa.h for card communication.
 */

#include "screen.h"
#include "netisa.h"
#include "menu.h"
#include <stdio.h>

int main(void)
{
    ni_version_t ver;

    /* Check for NetISA TSR (or stub) */
    if (!ni_detect(&ver)) {
        printf("NetISA TSR not loaded.\n");
        printf("Run NETISA.COM first to install the TSR.\n");
        return 1;
    }

    /* Initialize screen */
    scr_init();

    /* Run main menu loop */
    menu_run();

    /* Restore screen */
    scr_shutdown();

    return 0;
}
