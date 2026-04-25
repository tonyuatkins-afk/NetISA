/*
 * unlock/whisper.c - Dormant feature whisper.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Picks one unlocked feature that has not been used in 60+ days and surfaces
 * a single, status bar level whisper. One whisper per session, globally.
 */
#include "whisper.h"
#include <stdio.h>
#include <string.h>

static char message[120];
static hbool pending = HFALSE;

static hbool date_older_than(const char *iso, u16 days)
{
    /* Stub: always treat empty dates as old. We will integrate real date math
     * once the date utility module lands. */
    if (!iso || !iso[0]) return HFALSE;
    (void)days;
    return HFALSE;
}

void whisper_check(const unlock_entry_t *unlocks)
{
    u16 i;
    pending = HFALSE;
    if (!unlocks) return;
    for (i = 0; i < UL_COUNT; i++) {
        if (!unlocks[i].unlocked) continue;
        if (unlocks[i].ever_used) {
            if (date_older_than(unlocks[i].first_used, 60)) {
                sprintf(message,
                        "You unlocked \"%.40s\" but have not used it lately.",
                        unlocks[i].name ? unlocks[i].name : "(feature)");
                message[sizeof(message) - 1] = '\0';
                pending = HTRUE;
                return;
            }
        }
    }
}

hbool whisper_pending(void) { return pending; }
const char *whisper_message(void) { return pending ? message : ""; }
void whisper_dismiss(void) { pending = HFALSE; message[0] = '\0'; }
