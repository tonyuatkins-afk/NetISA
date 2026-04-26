/*
 * platform/io.h - DOS file IO helpers that survive >64K reads under Watcom
 *                 large-model far pointer arithmetic.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Watcom 16-bit large model far pointer arithmetic does NOT normalize
 * segment:offset on `+=`.  After four 16K reads the offset wraps to 0 and
 * the next fread overwrites the buffer base.  Every site that reads a
 * single >64K block must re-normalize the destination pointer between
 * chunks; we centralise that here so the bug fix lives in one place.
 */
#ifndef HEARO_PLATFORM_IO_H
#define HEARO_PLATFORM_IO_H

#include "../hearo.h"
#include <stdio.h>

/* Read `n` bytes from f into dst.  Handles dst spanning multiple 64K
 * segments by computing a normalized far pointer each chunk.  Returns
 * HFALSE on any short read. */
hbool io_read_chunked(FILE *f, void *dst, u32 n);

/* Subtract 128 from every byte in [dst, dst+n).  Used by the WAV decoder
 * to convert 8-bit unsigned PCM to signed in place; needs the same
 * cross-segment treatment as io_read_chunked. */
void  io_unsigned_to_signed_8(void *dst, u32 n);

#endif
