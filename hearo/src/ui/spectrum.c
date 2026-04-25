/*
 * ui/spectrum.c - ASCII spectrum visualizer.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Two data paths feed the visualiser:
 *   1. Live: spectrum_feed() pushes PCM samples from a decoder. We capture up
 *      to 256 samples into an internal buffer and recompute bin energies on
 *      each step. A simple log2-scaled magnitude approximation runs without
 *      a real FFT (we sum |sample| over each bin's slice of the buffer; the
 *      visual is close enough for the v1.0 ASCII width).
 *   2. Simulation: when no decoder has fed us in the last ~500 ms, we drive
 *      a sine-sweep so the UI looks alive. The sweep has bass-heavy bias and
 *      a moving peak so it reads as a real spectrum.
 *
 * Bin counts:
 *   FPU-less: 16 bins via bipartite-table approximation.
 *   FPU:      32 bins via adaptive CORDIC.
 */
#include "spectrum.h"
#include "screen.h"
#include "../math/cordic.h"
#include "../math/bipartite.h"
#include "../platform/timer.h"
#include <string.h>

#define MAX_BINS 32
#define FEED_BUF_LEN 256
#define FEED_TIMEOUT_MS 500UL

static u8 bin_count = 16;
static u8 bin_height[MAX_BINS];
static u32 frame = 0;
static hbool fpu_avail = HFALSE;

static s16 feed_buf[FEED_BUF_LEN];
static u16 feed_len = 0;
static u32 feed_last_ms = 0;
static hbool feed_active = HFALSE;

static const char *blocks_color = " \xB0\xB1\xB2\xDB";
static const char *blocks_mono  = " .-=#";

void spectrum_init(hbool has_fpu)
{
    fpu_avail = has_fpu;
    bin_count = has_fpu ? 32 : 16;
    if (bin_count > MAX_BINS) bin_count = MAX_BINS;
    memset(bin_height, 0, sizeof(bin_height));
    memset(feed_buf, 0, sizeof(feed_buf));
    feed_len = 0;
    feed_last_ms = 0;
    feed_active = HFALSE;
    frame = 0;
}

void spectrum_feed(const s16 *samples, u16 n)
{
    u16 i;
    if (!samples || n == 0) return;
    if (n > FEED_BUF_LEN) {
        /* Sub-sample: take every k-th sample so we land on FEED_BUF_LEN. */
        u16 k = (u16)((n + FEED_BUF_LEN - 1) / FEED_BUF_LEN);
        u16 written = 0;
        for (i = 0; i < n && written < FEED_BUF_LEN; i += k) {
            feed_buf[written++] = samples[i];
        }
        feed_len = written;
    } else {
        for (i = 0; i < n; i++) feed_buf[i] = samples[i];
        feed_len = n;
    }
    feed_last_ms = timer_ms();
    feed_active = HTRUE;
}

hbool spectrum_has_live_data(void)
{
    if (!feed_active) return HFALSE;
    if (timer_ms() - feed_last_ms > FEED_TIMEOUT_MS) return HFALSE;
    return HTRUE;
}

static void compute_from_feed(void)
{
    u16 per_bin;
    u8 i;
    u16 j;
    if (feed_len == 0) return;
    per_bin = (u16)(feed_len / bin_count);
    if (per_bin == 0) per_bin = 1;
    for (i = 0; i < bin_count; i++) {
        u32 sum = 0;
        u16 start = (u16)(i * per_bin);
        u16 end   = (u16)(start + per_bin);
        if (end > feed_len) end = feed_len;
        for (j = start; j < end; j++) {
            s16 s = feed_buf[j];
            sum += (u32)((s < 0) ? -s : s);
        }
        if (per_bin > 0) sum /= per_bin;
        /* Scale Q15 magnitude down to a 0..8 cell height. */
        {
            u32 scaled = sum >> 11;
            if (scaled > 8) scaled = 8;
            bin_height[i] = (u8)((bin_height[i] * 3 + (u8)scaled) >> 2);
        }
    }
}

static void compute_from_sweep(void)
{
    u8 i;
    s16 phase = (s16)((frame * 1024UL) & 0xFFFFUL);
    for (i = 0; i < bin_count; i++) {
        s32 angle = (s32)((u32)i * 2048UL + (u32)phase);
        s32 v;
        if (fpu_avail) {
            v = cordic_sin(angle, 16);
            v = v >> 11;
        } else {
            s16 s = bipartite_sin((u16)((angle >> 1) & 0xFFFFu));
            v = (s32)s >> 10;
        }
        if (v < 0) v = -v;
        if (i < bin_count / 4) v = (v * 5) / 4;
        if (v > 8) v = 8;
        bin_height[i] = (u8)((bin_height[i] * 3 + (u8)v) >> 2);
    }
}

void spectrum_step(void)
{
    if (spectrum_has_live_data()) compute_from_feed();
    else                          compute_from_sweep();
    frame++;
}

void spectrum_render(u8 x, u8 y, u8 w, u8 h)
{
    u8 inner_w, inner_h;
    u8 i;
    const char *blocks = scr_is_mono() ? blocks_mono : blocks_color;
    scr_box(x, y, w, h, ATTR_DIM);
    if (spectrum_has_live_data()) {
        scr_puts((u8)(x + 2), y, " Spectrum [live] ", ATTR_GREEN);
    } else {
        scr_puts((u8)(x + 2), y, " Spectrum ", ATTR_NORMAL);
    }

    inner_w = (u8)(w - 4);
    inner_h = (u8)(h - 2);
    if (inner_w == 0 || inner_h == 0) return;

    for (i = 0; i < inner_w; i++) {
        u8 bin = (u8)((i * bin_count) / inner_w);
        u8 height = bin_height[bin];
        u8 row;
        for (row = 0; row < inner_h; row++) {
            u8 ry = (u8)(y + h - 2 - row);
            u8 cell_h;
            char ch;
            u8 attr = ATTR_GREEN;
            if (height > row * 2) {
                cell_h = (u8)(height - row * 2);
                if (cell_h > 4) cell_h = 4;
                ch = blocks[cell_h];
                if (cell_h >= 3) attr = ATTR_YELLOW;
                if (cell_h >= 4) attr = ATTR_RED;
            } else {
                ch = ' ';
                attr = ATTR_DIM;
            }
            scr_putch((u8)(x + 2 + i), ry, ch, attr);
        }
    }
}
