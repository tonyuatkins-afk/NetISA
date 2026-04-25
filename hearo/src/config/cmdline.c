/*
 * config/cmdline.c - Command line parsing.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Recognised flags:
 *   /SAFE        skip audio probes
 *   /REDETECT    force a fresh hardware scan
 *   /BENCHMARK   run the suite benchmark
 *   /HALL        print Hall, exit
 *   /UNLOCKS     print unlock matrix, exit
 *   /VERSION     print version, exit
 *   /VIDEO=xxx   override video mode (mda, ega, vga, svga640, svga800, svga1024)
 *   /DEBUG       verbose logging
 *   /STUBNET     pretend NetISA card is present
 *   /50LINE      use 50 line text mode if supported
 *   /COVOX=378   force Covox detection at the given LPT base
 */
#include "cmdline.h"
#include <string.h>
#include <ctype.h>

#define MAX_FLAGS 16

typedef struct {
    char key[16];
    char value[24];
} cmd_flag_t;

static cmd_flag_t flags[MAX_FLAGS];
static u16 flag_count = 0;

static void to_upper(char *s)
{
    while (*s) { *s = (char)toupper((unsigned char)*s); s++; }
}

void cmdline_parse(int argc, char *argv[])
{
    int i;
    flag_count = 0;
    for (i = 1; i < argc && flag_count < MAX_FLAGS; i++) {
        const char *a = argv[i];
        if (a[0] != '/' && a[0] != '-') continue;
        a++;
        {
            cmd_flag_t *f = &flags[flag_count++];
            const char *eq = strchr(a, '=');
            u16 klen;
            if (eq) {
                klen = (u16)(eq - a);
                if (klen >= sizeof(f->key)) klen = sizeof(f->key) - 1;
                memcpy(f->key, a, klen);
                f->key[klen] = '\0';
                strncpy(f->value, eq + 1, sizeof(f->value) - 1);
                f->value[sizeof(f->value) - 1] = '\0';
            } else {
                strncpy(f->key, a, sizeof(f->key) - 1);
                f->key[sizeof(f->key) - 1] = '\0';
                f->value[0] = '\0';
            }
            to_upper(f->key);
        }
    }
}

hbool cmdline_has(const char *flag)
{
    char up[16];
    u16 i;
    if (!flag) return HFALSE;
    strncpy(up, flag, sizeof(up) - 1);
    up[sizeof(up) - 1] = '\0';
    to_upper(up);
    for (i = 0; i < flag_count; i++) {
        if (strcmp(flags[i].key, up) == 0) return HTRUE;
    }
    return HFALSE;
}

const char *cmdline_value(const char *flag)
{
    char up[16];
    u16 i;
    if (!flag) return 0;
    strncpy(up, flag, sizeof(up) - 1);
    up[sizeof(up) - 1] = '\0';
    to_upper(up);
    for (i = 0; i < flag_count; i++) {
        if (strcmp(flags[i].key, up) == 0) return flags[i].value;
    }
    return 0;
}
