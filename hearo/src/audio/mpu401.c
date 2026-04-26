/*
 * audio/mpu401.c - MPU-401 UART-mode MIDI output driver.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#include "audiodrv.h"
#include "mpu401.h"
#include <conio.h>

static u16 mpu_base = 0x330;

/* Watcom 16-bit int is signed; 0xFFFF as int is -1, so the previous
 * `int t = 0xFFFF; return t > 0` always returned FALSE and the entire
 * MPU-401 init silently failed. Use unsigned. */
static hbool mpu_wait_write(u16 base)
{
    unsigned int t = 0xFFFF;
    while ((inp(base + 1) & 0x40) && --t) ;
    return t > 0;
}

static hbool mpu_wait_read(u16 base)
{
    unsigned int t = 0xFFFF;
    while ((inp(base + 1) & 0x80) && --t) ;
    return t > 0;
}

void mpu_send(u8 val)
{
    if (mpu_wait_write(mpu_base)) outp(mpu_base, val);
}

void mpu_send_msg(u8 status, u8 d1, u8 d2)
{
    u8 t = status & 0xF0;
    mpu_send(status);
    mpu_send(d1);
    if (t != 0xC0 && t != 0xD0) mpu_send(d2);
}

void mpu_send_sysex(const u8 *data, u16 len)
{
    u16 i;
    for (i = 0; i < len; i++) mpu_send(data[i]);
}

static hbool m_init(const hw_profile_t *hw)
{
    if (hw->mpu_base) mpu_base = hw->mpu_base;
    /* Enter UART mode: write 0x3F to command port and wait for ACK 0xFE. */
    if (!mpu_wait_write(mpu_base)) return HFALSE;
    outp(mpu_base + 1, 0x3F);
    if (!mpu_wait_read(mpu_base)) return HFALSE;
    (void)inp(mpu_base);
    return HTRUE;
}

static void m_shutdown(void)
{
    /* Reset MPU back to intelligent / standby. */
    if (mpu_wait_write(mpu_base)) outp(mpu_base + 1, 0xFF);
}

static hbool m_open(u32 r, u8 f, audio_callback_t cb) { (void)r; (void)f; (void)cb; return HTRUE; }
static void  m_close(void) {}
static void  m_volume(u8 v) { (void)v; /* MIDI volume is per-channel CC 7 */ }
static void  m_caps(audio_caps_t *c)
{
    c->name = "Roland MPU-401"; c->chip = "UART";
    c->formats = 0; c->max_rate = 0; c->max_channels = 16; c->max_bits = 0;
    c->has_hardware_mix = HTRUE;
    c->hardware_voices = 0;     /* depends on attached synth */
    c->sample_ram = 0;
}

const audio_driver_t mpu401_driver = {
    "mpu401", m_init, m_shutdown, m_open, m_close, m_volume, m_caps, 0, 0, 0
};
