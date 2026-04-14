#include "discord.h"
#include <dos.h>
#include <conio.h>

/* PIT frequency = 1,193,180 Hz */
#define PIT_FREQ 1193180L

/* Enable PC speaker with given frequency */
static void speaker_on(int freq_hz)
{
    unsigned int divisor;
    unsigned char gate;

    if (freq_hz < 37) return;
    divisor = (unsigned int)(PIT_FREQ / (long)freq_hz);

    outp(0x43, 0xB6);              /* PIT channel 2, mode 3, binary */
    outp(0x42, divisor & 0xFF);     /* Low byte */
    outp(0x42, (divisor >> 8));     /* High byte */

    gate = (unsigned char)inp(0x61);
    outp(0x61, gate | 0x03);        /* Enable PIT gate + speaker data */
}

/* Disable PC speaker */
static void speaker_off(void)
{
    unsigned char gate = (unsigned char)inp(0x61);
    outp(0x61, gate & 0xFC);
}

/* Delay in milliseconds using BIOS tick polling (crude but works on 8088) */
static void delay_ms(int ms)
{
    /* Use a simple busy loop calibrated roughly for DOS
       On 8088 at 4.77MHz, ~1000 iterations ≈ 1ms
       On 486 it's much faster, but for audio timing this is acceptable */
    long loops = (long)ms * 500L;  /* conservative estimate */
    while (loops-- > 0) {
        inp(0x61);  /* safe port read as delay */
    }
}

void dc_audio_blip(void)
{
    speaker_on(2000);
    delay_ms(20);
    speaker_off();
}

void dc_audio_chirp(void)
{
    speaker_on(1500);
    delay_ms(15);
    speaker_on(2000);
    delay_ms(15);
    speaker_off();
}

void dc_audio_mention(void)
{
    speaker_on(1000);
    delay_ms(30);
    speaker_on(1500);
    delay_ms(30);
    speaker_on(2000);
    delay_ms(30);
    speaker_off();
}

void dc_audio_error(void)
{
    speaker_on(800);
    delay_ms(50);
    speaker_on(400);
    delay_ms(50);
    speaker_off();
}
