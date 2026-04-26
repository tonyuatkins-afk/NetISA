/*
 * audio/dma.h - ISA DMA channel programming.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_AUDIO_DMA_H
#define HEARO_AUDIO_DMA_H

#include "../hearo.h"

/* Allocate a DMA-safe buffer (does not cross a 64K page for 8-bit, 128K for
 * 16-bit).  Returns far pointer or NULL.  *out_phys gets the 24-bit physical
 * address. */
void far *dma_alloc(u16 size, hbool sixteen_bit, u32 *out_phys);
void      dma_free(void far *buf);

/* Program a DMA channel for an auto-initialized read transfer (memory ->
 * device).  After this call, every device DACK pulse will pull the next
 * byte/word from the buffer; when the buffer is exhausted the controller
 * restarts at the beginning. */
void dma_setup_8bit (u8 channel, u32 phys, u16 length);
void dma_setup_16bit(u8 channel, u32 phys, u16 length);

void dma_disable(u8 channel);
void dma_enable (u8 channel);

/* Approximate residual word count in the active transfer. Caller must read
 * twice and compare for stability (counter is split across two byte reads). */
u16  dma_get_count(u8 channel);

#endif
