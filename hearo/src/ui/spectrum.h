/*
 * ui/spectrum.h - ASCII spectrum visualizer.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_UI_SPECTRUM_H
#define HEARO_UI_SPECTRUM_H

#include "../hearo.h"

void spectrum_init(hbool has_fpu);
void spectrum_step(void);                /* advance one frame; call ~10 Hz */
void spectrum_render(u8 x, u8 y, u8 w, u8 h);

/* Push a block of mono PCM samples (Q15, signed) for the next visualizer
 * frame. Decoders call this once per audio buffer; the visualiser uses the
 * most recent feed when available and falls back to the synthetic sweep
 * otherwise. `n` should be a power of two between 32 and 256 for best
 * results; values outside that range are clamped or sub-sampled. */
void spectrum_feed(const s16 *samples, u16 n);

/* Has the visualiser received any real audio data in the last ~500ms?
 * Visible in the title bar so the user can tell live data from the sweep. */
hbool spectrum_has_live_data(void);

#endif
