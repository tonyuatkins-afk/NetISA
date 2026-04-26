/*
 * audio/wake.h - Audio chip wake / pre-init layer.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Two-layer audio. The upper layer is the per-driver vtable
 * (audio_driver_t in audiodrv.h, e.g. sb_driver, gus_driver). The lower
 * layer (this file) is a registry of vendor-specific pre-init backends.
 *
 * Some chips that report SB-compatible respond to DSP version queries
 * but stay PCM-gated until vendor-specific control registers are touched.
 * The canonical example is the Yamaha YMF715 OPL3-SAx in pure MS-DOS
 * mode (Win9x's "restart in MS-DOS mode"): the chip reports DSP v3.01
 * cleanly, but its SB block sits in power-down state with SBPDR set,
 * and TESTPLAY plays exactly one half-block then stalls. UNISOUND.COM,
 * YMFSB.EXE, and OPL3SAX.EXE all clear that state; vendor BIOSes do
 * the same via PnP enumeration; pure DOS skips it.
 *
 * sb_init runs the wake registry before its DSP reset. Backends register
 * themselves via wake_register_all (called once at boot from main).
 * wake_chip iterates registered backends, asks each probe(), and the
 * first one that returns HTRUE has its wake() called. Backends that do
 * not claim the chip return HFALSE; that is the normal real-Creative-SB
 * path and is not an error.
 *
 * Future backends (per the prompt's roadmap):
 *   - PCI legacy-decode + codec unmute for YMF724, ICH, VIA. SBEMU has
 *     prior art there.
 *   - WSS aliasing quirks for Crystal CS4321 family.
 */
#ifndef HEARO_AUDIO_WAKE_H
#define HEARO_AUDIO_WAKE_H

#include "../hearo.h"

typedef struct wake_backend_s {
    const char *name;
    /* Returns HTRUE if this backend's chip is present at one of the bases
     * it knows about. May read I/O ports and may do non-destructive
     * register fingerprint tests, but should leave the chip in the same
     * functional state as it found it. Called for every registered
     * backend in registration order until one returns HTRUE. */
    hbool (*probe)(void);
    /* Wakes the chip so the SB-compatible interface responds. Called
     * exactly once for the first backend whose probe() returned HTRUE.
     * Backend may stash internal state (e.g. discovered base port)
     * during probe and consume it here. Returns HTRUE on success. */
    hbool (*wake)(const hw_profile_t *hw);
} wake_backend_t;

/* Registers a backend. Order of registration is the probe order; first
 * registered is probed first, so put more specific backends ahead of
 * generic catch-alls. Backend pointers must be stable for the life of
 * the program (canonical pattern: a `static const wake_backend_t` in
 * the backend's own .c file, exported via extern). */
void wake_register(const wake_backend_t *backend);

/* Registers all built-in backends. Call once at startup, before any
 * audio driver init that needs a wake step. Empty body in the scaffold;
 * each Phase 3.x backend adds a wake_register call here. */
void wake_register_all(void);

/* Iterates registered backends and runs the first one that probes
 * positive. HTRUE iff a backend matched AND its wake() succeeded. HFALSE
 * means either no backend matched (normal real-SB case, expected) or
 * wake() failed (not expected; surfaces to the caller as "no pre-init
 * applied, proceed and let the chip's behavior speak for itself"). */
hbool wake_chip(const hw_profile_t *hw);

#endif
