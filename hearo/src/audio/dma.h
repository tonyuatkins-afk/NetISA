/*
 * audio/dma.h - ISA DMA channel programming.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_AUDIO_DMA_H
#define HEARO_AUDIO_DMA_H

#include "../hearo.h"

/* Allocate a DMA-safe buffer (does not cross a 64K page for 8-bit, 128K for
 * 16-bit).  Returns far pointer or NULL.  *out_phys gets the 24-bit physical
 * address. size is in bytes; max is 64K for 8-bit, 128K for 16-bit. */
void far *dma_alloc(u32 size, hbool sixteen_bit, u32 *out_phys);
void      dma_free(void far *buf);

/* Program a DMA channel for a read transfer (memory -> device). When
 * auto_init is HTRUE the controller restarts at the beginning when the
 * buffer is exhausted; HFALSE selects single-cycle and the controller
 * halts at terminal count. SB 1.x and force_single_cycle paths require
 * single-cycle; auto-init mode at the controller while the DSP issues
 * single-cycle commands desyncs the two and overruns the buffer. */
void dma_setup_8bit (u8 channel, u32 phys, u32 length, hbool auto_init);
void dma_setup_16bit(u8 channel, u32 phys, u32 length, hbool auto_init);

void dma_disable(u8 channel);
void dma_enable (u8 channel);

/* Approximate residual word count in the active transfer. Caller must read
 * twice and compare for stability (counter is split across two byte reads). */
u16  dma_get_count(u8 channel);

/* Issue master-clear to BOTH 8237 controllers. Cancels any in-flight cycle
 * and masks all 8 channels. Call from a teardown path before dma_free when
 * the source device may still be asserting DREQ (e.g. wedged YMF715). */
void dma_master_clear(void);

/* Spin until two consecutive count reads agree, or the iteration budget
 * is exhausted. HTRUE if quiescent, HFALSE on timeout. */
hbool dma_wait_quiescent(u8 channel, unsigned int max_iters);

#endif
