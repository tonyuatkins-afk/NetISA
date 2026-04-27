/*
 * audio/audiodrv.h - Audio driver abstraction.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_AUDIO_AUDIODRV_H
#define HEARO_AUDIO_AUDIODRV_H

#include "../hearo.h"

#define AFMT_U8_MONO    0
#define AFMT_U8_STEREO  1
#define AFMT_S16_MONO   2
#define AFMT_S16_STEREO 3

#define AFMT_BYTES(fmt)    (((fmt) & 2) ? 2 : 1)
#define AFMT_CHANNELS(fmt) (((fmt) & 1) ? 2 : 1)
#define AFMT_FRAME_BYTES(fmt) (AFMT_BYTES(fmt) * AFMT_CHANNELS(fmt))

typedef void (*audio_callback_t)(void *buffer, u16 samples, u8 format);

typedef struct {
    const char *name;
    const char *chip;
    u8   formats;
    u32  max_rate;
    u16  max_channels;
    u16  max_bits;
    hbool has_hardware_mix;
    u16  hardware_voices;
    u32  sample_ram;
} audio_caps_t;

typedef struct audio_driver_s {
    const char *name;
    hbool (*init)(const hw_profile_t *hw);
    void  (*shutdown)(void);
    hbool (*open)(u32 rate, u8 format, audio_callback_t cb);
    void  (*close)(void);
    void  (*set_volume)(u8 vol);
    void  (*get_caps)(audio_caps_t *caps);
    /* GUS / hardware-voice cards only; otherwise NULL. */
    hbool (*upload_sample)(u16 id, const void *data, u32 len, u8 bits);
    void  (*trigger_voice)(u8 channel, u16 sample_id, u32 freq, u8 vol, u8 pan);
    void  (*stop_voice)(u8 channel);
} audio_driver_t;

#define AUDIODRV_MAX 12

void                   audiodrv_register(const audio_driver_t *drv);
int                    audiodrv_count(void);
const audio_driver_t  *audiodrv_get(int index);
const audio_driver_t  *audiodrv_find(const char *name);

hbool                  audiodrv_set_active(const audio_driver_t *drv);
const audio_driver_t  *audiodrv_active(void);

/* Selects the best driver for the detected hardware and initializes it.
 * Returns HFALSE if no driver could be opened (still leaves the null
 * driver active so the rest of the engine keeps working). */
hbool                  audiodrv_auto_select(const hw_profile_t *hw);

/* If a hardware-mixed driver (currently: GUS) is registered AND was
 * successfully init'd, returns it.  Tracker decoders use this to upload
 * samples directly to card DRAM and trigger voices in hardware, bypassing
 * the software mixer. Returns NULL otherwise. */
const audio_driver_t  *audiodrv_get_hardware_mixer(void);

/* Registers the built-in drivers. Call once at start. */
void                   audiodrv_register_all(void);

/* PC speaker deferred-work pump. The pcspeaker driver's ISR no longer calls
 * the mixer callback directly (it would starve the foreground for tens of
 * ms per refill on a 386). Instead the ISR sets a refill_pending flag that
 * the foreground main loop must drain by calling pc_pump regularly. Every
 * ~25 ms is enough at the default 18 kHz / 1024-frame buffer. Safe to call
 * when no PC speaker driver is active (no-op if nothing is pending). */
void                   pc_pump(void);

/* Sound Blaster deferred-work pump. Mirrors pc_pump: the SB ISR sets a
 * byte flag naming the idle half, sb_pump drains it on the foreground
 * stack. Callers must invoke this regularly from the main loop -- at
 * 22050 Hz / 2048-frame halves the budget is ~93 ms before the DSP wraps,
 * so any UI loop that polls kbhit is fine. Safe to call when the active
 * driver is not SB (no-op). The deferred pattern is required because the
 * ISR previously called the mixer callback directly, exposing two
 * structural hazards: (a) the decoder + libc call tree on a foreign ISR
 * stack, and (b) DOS reentrancy through any stdio/malloc/fopen/time/
 * assert path the decoder might touch (DOS is not reentrant; INT 21h
 * called from interrupt context corrupts non-reentrant DOS state). The
 * canonical references for this design are John Carmack's Quake
 * snd_dos.c (1996) and SDL3 PR #15377 (April 2026). */
void                   sb_pump(void);

#endif
