/*
 * audio/wake.c - Audio chip wake registry.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#include "wake.h"
#include <stdio.h>

#define WAKE_MAX_BACKENDS 8

static const wake_backend_t *backends[WAKE_MAX_BACKENDS];
static u8 backend_count;

void wake_register(const wake_backend_t *backend)
{
    if (!backend || backend_count >= WAKE_MAX_BACKENDS) return;
    backends[backend_count++] = backend;
}

hbool wake_chip(const hw_profile_t *hw)
{
    u8 i;
    for (i = 0; i < backend_count; i++) {
        const wake_backend_t *b = backends[i];
        if (!b->probe || !b->probe()) continue;
        /* First probe-positive backend wins. wake() ran or didn't; we
         * either way return its result and stop iterating. Probing more
         * backends after a match would risk a false-positive on a
         * different vendor's register space. */
        if (b->wake) return b->wake(hw);
        return HTRUE;
    }
    return HFALSE;
}

void wake_register_all(void)
{
    /* Each Phase 3.x backend adds its wake_register call here. Order
     * matters: more specific backends (vendor-keyed registers, multi-
     * byte fingerprints) ahead of generic catch-alls. */
}
