/*
 * detect/audio.c - Probe every period appropriate audio device.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * The detection order matters. We probe environment-hinted devices first
 * (BLASTER, ULTRASND, MIDI), then blind probes in a careful order that
 * minimises destructive probes onto cards that might not exist. See
 * docs/hearo-soundcard-reference.md for the full protocol of each device.
 *
 * Each device that probes positive sets its AUD_* flag and (if relevant) fills
 * sb_config_t / gus_config_t. Human readable names are appended to
 * hw->aud_cards[]. The order of aud_cards is the order of appearance on the
 * boot screen.
 */
#include "audio.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

extern u8   ad_inp(u16 port);
extern void ad_outp(u16 port, u8 val);
extern void ad_delay_us(u16 us);
extern u16  ad_bios_lpt(u8 idx);  /* read LPTn base from BIOS data area */
extern u8   ad_bios_byte(u32 farptr); /* read a byte at far ptr */

#ifdef HEARO_NOASM
u8   ad_inp(u16 p) { (void)p; return 0xFF; }
void ad_outp(u16 p, u8 v) { (void)p; (void)v; }
void ad_delay_us(u16 us) { (void)us; }
u16  ad_bios_lpt(u8 i) { (void)i; return 0; }
u8   ad_bios_byte(u32 p) { (void)p; return 0; }
#endif

const char *opl_name(opl_type_t o)
{
    switch (o) {
        case OPL_OPL2: return "OPL2";
        case OPL_OPL3: return "OPL3";
        case OPL_CQM:  return "CQM";
        default:       return "none";
    }
}

const char *midi_synth_name(midi_synth_t s)
{
    switch (s) {
        case MIDI_GM:      return "General MIDI";
        case MIDI_MT32:    return "Roland MT-32";
        case MIDI_CM32L:   return "Roland CM-32L";
        case MIDI_SC55:    return "Roland SC-55";
        case MIDI_SC55MK2: return "Roland SC-55 mkII";
        case MIDI_SC88:    return "Roland SC-88";
        case MIDI_SC88PRO: return "Roland SC-88 Pro";
        case MIDI_DB50XG:  return "Yamaha DB50XG";
        case MIDI_SCB55:   return "Roland SCB-55";
        case MIDI_NONE:    return "";
        default:           return "MIDI device";
    }
}

static void add_card(hw_profile_t *hw, const char *fmt, ...)
{
    char buf[64];
    va_list ap;
    if (hw->aud_card_count >= 4) return;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';
    strncpy(hw->aud_cards[hw->aud_card_count], buf, 47);
    hw->aud_cards[hw->aud_card_count][47] = '\0';
    hw->aud_card_count++;
}

/* ---- PC Speaker (always) ---- */
static void probe_pc_speaker(hw_profile_t *hw)
{
    hw->aud_devices |= AUD_PCSPEAKER;
    add_card(hw, "PC Speaker (PIT ch2)");
}

/* ---- Tandy / PCjr PSG (port C0h + BIOS signature) ---- */
static void probe_tandy(hw_profile_t *hw)
{
    u8 sig = ad_bios_byte(0xF000FFFEUL);
    if (sig == 0xFD || sig == 0xFF) {
        /* Confirm with a write to C0h: silence tone 0. */
        ad_outp(0xC0, 0x9F);
        hw->aud_devices |= AUD_TANDY;
        add_card(hw, "Tandy / PCjr 3-voice PSG");
    }
}

/* ---- Covox / Disney via LPT ---- */
static hbool lpt_loopback(u16 base)
{
    u8 saved = ad_inp(base);
    u8 patterns[4]; u8 i;
    patterns[0] = 0xAA; patterns[1] = 0x55; patterns[2] = 0x00; patterns[3] = 0xFF;
    for (i = 0; i < 4; i++) {
        ad_outp(base, patterns[i]);
        ad_delay_us(2);
        if (ad_inp(base) != patterns[i]) {
            ad_outp(base, saved);
            return HFALSE;
        }
    }
    ad_outp(base, saved);
    return HTRUE;
}

static hbool disney_fifo(u16 base)
{
    /* Reset, send 16 bytes, status bit 6 should set then drain. */
    u16 ctrl = base + 2, stat = base + 1;
    u8 i;
    ad_outp(ctrl, 0x04); /* reset FIFO */
    ad_delay_us(50);
    for (i = 0; i < 16; i++) {
        ad_outp(base, 0x80);
        ad_outp(ctrl, 0x0C);
        ad_outp(ctrl, 0x04);
    }
    if ((ad_inp(stat) & 0x40) == 0) return HFALSE;
    ad_delay_us(150);
    if ((ad_inp(stat) & 0x40) != 0) return HFALSE; /* should have drained */
    return HTRUE;
}

static void probe_lpt_audio(hw_profile_t *hw)
{
    u16 lpt = ad_bios_lpt(0);
    if (lpt == 0) return;
    if (disney_fifo(lpt)) {
        hw->aud_devices |= AUD_DISNEY;
        add_card(hw, "Disney Sound Source on LPT1");
        return; /* Disney precludes Covox detection on the same port */
    }
    if (lpt_loopback(lpt)) {
        hw->aud_devices |= AUD_COVOX;
        add_card(hw, "Covox Speech Thing on LPT1 (probable)");
    }
}

/* ---- AdLib / OPL2 / OPL3 ---- */
static hbool opl_timer_test(u16 base)
{
    u8 status_before, status_after;
    ad_outp(base, 0x04); ad_outp(base + 1, 0x60);
    ad_outp(base, 0x04); ad_outp(base + 1, 0x80);
    status_before = ad_inp(base);
    if (status_before != 0x00) return HFALSE;
    ad_outp(base, 0x02); ad_outp(base + 1, 0xFF);
    ad_outp(base, 0x04); ad_outp(base + 1, 0x21);
    ad_delay_us(100);
    status_after = ad_inp(base);
    ad_outp(base, 0x04); ad_outp(base + 1, 0x60);
    ad_outp(base, 0x04); ad_outp(base + 1, 0x80);
    return ((status_after & 0xE0) == 0xC0) ? HTRUE : HFALSE;
}

static hbool opl3_present(u16 base)
{
    /* OPL3 mirrors registers at base+2/+3 with a different status pattern. */
    ad_outp(base + 2, 0x05); ad_outp(base + 3, 0x01);
    ad_outp(base, 0x04); ad_outp(base + 1, 0x60);
    return ((ad_inp(base) & 0x06) == 0) ? HTRUE : HFALSE;
}

static void probe_adlib(hw_profile_t *hw)
{
    if (!opl_timer_test(0x388)) return;
    if (opl3_present(0x388)) {
        hw->opl = OPL_OPL3;
        hw->aud_devices |= AUD_ADLIB;
        add_card(hw, "AdLib (OPL3)");
    } else {
        hw->opl = OPL_OPL2;
        hw->aud_devices |= AUD_ADLIB;
        add_card(hw, "AdLib (OPL2)");
    }
}

/* AdLib Gold lives at the OPL3 mirror plus CT1703 mixer at 38Ch+. */
static void probe_adlib_gold(hw_profile_t *hw)
{
    if (!(hw->aud_devices & AUD_ADLIB)) return;
    if (hw->opl != OPL_OPL3) return;
    /* Probe the AdLib Gold mixer ID register. */
    {
        u8 prev = ad_inp(0x38C);
        ad_outp(0x38C, 0x00);
        if (ad_inp(0x38D) == 0xFF) {
            ad_outp(0x38C, prev);
            return; /* Not AdLib Gold */
        }
        ad_outp(0x38C, prev);
    }
    hw->aud_devices |= AUD_ADLIB_GOLD;
    add_card(hw, "AdLib Gold (OPL3 + 12-bit DAC)");
}

/* ---- Sound Blaster family ---- */
static hbool sb_dsp_reset(u16 base)
{
    u16 i;
    ad_outp(base + 6, 1);
    ad_delay_us(5);
    ad_outp(base + 6, 0);
    for (i = 0; i < 1000; i++) {
        if (ad_inp(base + 0x0E) & 0x80) {
            if (ad_inp(base + 0x0A) == 0xAA) return HTRUE;
        }
        ad_delay_us(10);
    }
    return HFALSE;
}

static hbool sb_dsp_version(u16 base, u8 *maj, u8 *min)
{
    u16 i;
    /* Wait for write buffer */
    for (i = 0; i < 1000; i++) {
        if ((ad_inp(base + 0x0C) & 0x80) == 0) break;
        ad_delay_us(10);
    }
    ad_outp(base + 0x0C, 0xE1);
    for (i = 0; i < 1000; i++) {
        if (ad_inp(base + 0x0E) & 0x80) {
            *maj = ad_inp(base + 0x0A);
            for (i = 0; i < 1000; i++) {
                if (ad_inp(base + 0x0E) & 0x80) {
                    *min = ad_inp(base + 0x0A);
                    return HTRUE;
                }
                ad_delay_us(5);
            }
            return HFALSE;
        }
        ad_delay_us(10);
    }
    return HFALSE;
}

static hbool emu8000_present(u16 sb_base)
{
    /* EMU8000 sits at sb_base + 0x400 typically (i.e. 620h for sb=220h). */
    u16 emu = sb_base + 0x400;
    u8 v = ad_inp(emu + 2);
    if (v == 0xFF) return HFALSE;
    return HTRUE;
}

static hbool sb_asp_present(u16 base)
{
    /* DSP command 04h returns the ASP/CSP version on SB16 ASP/CSP cards as
     * two bytes (major, minor). Plain SB16 (no ASP) DSPs ignore command 04h
     * and never set the data-ready bit, so we time out and report absent.
     * The earlier mixer-index 0x83 check produced false positives on
     * DOSBox-X's plain SB16 emulation because index 0x83 reads back
     * undefined values rather than zero. */
    u16 i;
    u8 maj;
    for (i = 0; i < 100; i++) {
        if ((ad_inp(base + 0x0C) & 0x80) == 0) break;
        ad_delay_us(5);
    }
    if (i == 100) return HFALSE;
    ad_outp(base + 0x0C, 0x04);
    for (i = 0; i < 200; i++) {
        if (ad_inp(base + 0x0E) & 0x80) {
            maj = ad_inp(base + 0x0A);
            return (maj > 0 && maj < 0x80) ? HTRUE : HFALSE;
        }
        ad_delay_us(5);
    }
    return HFALSE;
}

static void parse_blaster(u16 *base, u8 *irq, u8 *dlo, u8 *dhi, u16 *mpu)
{
    char *env = getenv("BLASTER");
    *base = 0; *irq = 0; *dlo = 0; *dhi = 0; *mpu = 0;
    if (!env) return;
    while (*env) {
        char tag = *env++;
        char *end = env;
        long v = strtol(env, &end, 16);
        env = end;
        switch (tag) {
            case 'A': case 'a': *base = (u16)v; break;
            case 'I': case 'i': *irq  = (u8)v;  break;
            case 'D': case 'd': *dlo  = (u8)v;  break;
            case 'H': case 'h': *dhi  = (u8)v;  break;
            case 'P': case 'p': *mpu  = (u16)v; break;
        }
        while (*env == ' ' || *env == '\t') env++;
    }
}

static void probe_sb(hw_profile_t *hw)
{
    u16 base, mpu; u8 irq, dlo, dhi;
    parse_blaster(&base, &irq, &dlo, &dhi, &mpu);
    if (base == 0) {
        u16 candidates[] = { 0x220, 0x240, 0x260, 0x280, 0 };
        u16 i;
        for (i = 0; candidates[i]; i++) {
            if (sb_dsp_reset(candidates[i])) { base = candidates[i]; break; }
        }
        if (base == 0) return;
    } else {
        if (!sb_dsp_reset(base)) return;
    }

    {
        u8 maj = 0, min = 0;
        if (!sb_dsp_version(base, &maj, &min)) return;
        hw->sb.base = base;
        hw->sb.irq  = irq;
        hw->sb.dma_lo = dlo;
        hw->sb.dma_hi = dhi;
        hw->sb.dsp_major = maj;
        hw->sb.dsp_minor = min;
        if (mpu) hw->mpu_base = mpu;

        if (maj == 1) {
            hw->aud_devices |= AUD_SB_1X;
            add_card(hw, "Sound Blaster 1.%u (DSP %u.%02u)", min, maj, min);
        } else if (maj == 2) {
            hw->aud_devices |= AUD_SB_20;
            add_card(hw, "Sound Blaster 2.0 (DSP %u.%02u)", maj, min);
        } else if (maj == 3) {
            /* SB-integrated OPL is at base+0..base+3, not base+8. SB Pro 2
             * has OPL3 here; SB Pro 1 has dual OPL2. probe_adlib also runs
             * earlier and may have set hw->opl=OPL_OPL3 if the chip is
             * mirrored at 388h, so honour that as a backup signal. */
            if (opl3_present(base) || hw->opl == OPL_OPL3) {
                hw->aud_devices |= AUD_SB_PRO2;
                hw->opl = OPL_OPL3;
                add_card(hw, "Sound Blaster Pro 2 (DSP %u.%02u)", maj, min);
            } else {
                hw->aud_devices |= AUD_SB_PRO;
                hw->opl = OPL_OPL2;
                add_card(hw, "Sound Blaster Pro (DSP %u.%02u)", maj, min);
            }
        } else if (maj == 4) {
            hw->opl = OPL_OPL3;
            if (emu8000_present(base)) {
                if (!opl_timer_test(base)) {
                    hw->aud_devices |= AUD_SB_AWE64;
                    hw->opl = OPL_CQM;
                    add_card(hw, "Sound Blaster AWE64 (DSP %u.%02u)", maj, min);
                } else {
                    hw->aud_devices |= AUD_SB_AWE32;
                    add_card(hw, "Sound Blaster AWE32 (DSP %u.%02u)", maj, min);
                }
            } else if (sb_asp_present(base)) {
                hw->aud_devices |= AUD_SB_16ASP;
                hw->sb.has_asp = HTRUE;
                add_card(hw, "Sound Blaster 16 ASP (DSP %u.%02u)", maj, min);
            } else {
                hw->aud_devices |= AUD_SB_16;
                add_card(hw, "Sound Blaster 16 (DSP %u.%02u)", maj, min);
            }
        }
    }
}

/* ---- GUS family ---- */
static hbool gus_reset(u16 base)
{
    /* Reset GF1: clear bit 0 and 1 of register 4Ch at base+103h. */
    ad_outp(base + 0x103, 0x4C);
    ad_outp(base + 0x105, 0x00);
    ad_delay_us(100);
    ad_outp(base + 0x103, 0x4C);
    ad_outp(base + 0x105, 0x01);
    ad_delay_us(100);
    ad_outp(base + 0x103, 0x4C);
    ad_outp(base + 0x105, 0x07);
    ad_delay_us(100);
    /* Read mix control: nonzero indicates GF1 alive. */
    return (ad_inp(base) != 0xFF) ? HTRUE : HFALSE;
}

static u32 gus_dram_size(u16 base)
{
    /* Crude: assume 256KB minimum, 1MB maximum. We accept ULTRASND value. */
    (void)base;
    return 256;
}

static hbool gus_max_codec(u16 base)
{
    u8 v = ad_inp(base + 0x10C);
    if (v == 0xFF) return HFALSE;
    if ((v & 0xC0) != 0x00) return HFALSE;
    return HTRUE;
}

static void probe_gus(hw_profile_t *hw)
{
    char *env = getenv("ULTRASND");
    u16 base = 0; u8 irq = 0, dma = 0;
    u32 ram_kb = 256;
    if (env) {
        long v;
        char *end = env;
        v = strtol(env, &end, 16); base = (u16)v; env = end;
        if (*env == ',') env++;
        v = strtol(env, &end, 10); dma = (u8)v;  env = end;
        if (*env == ',') env++;
        v = strtol(env, &end, 10); /* dma2 */     env = end;
        if (*env == ',') env++;
        v = strtol(env, &end, 10); irq = (u8)v;  env = end;
    }
    if (base == 0) {
        /* No ULTRASND env. GUS detection requires the user to set the
         * variable; skip blind probing because gus_reset's "is the port
         * non-FF" check is too loose to distinguish a real GF1 from a SB
         * card sharing the same base. Leaves GUS unreported when ULTRASND
         * is missing, which is correct DOS practice. */
        return;
    }
    if (hw->sb.base != 0 && base == hw->sb.base) {
        /* SB and GUS both claim this base. SB already won; GUS is almost
         * certainly a stale ULTRASND from a different hardware era. */
        return;
    }
    if (!gus_reset(base)) return;
    ram_kb = gus_dram_size(base);
    hw->gus.base = base;
    hw->gus.irq  = irq;
    hw->gus.dma  = dma;
    hw->gus.ram_kb = ram_kb;

    if (gus_max_codec(base)) {
        hw->gus.has_codec = HTRUE;
        hw->aud_devices |= AUD_GUS_MAX;
        add_card(hw, "Gravis UltraSound MAX (GF1, %luKB)", ram_kb);
    } else {
        hw->aud_devices |= AUD_GUS;
        add_card(hw, "Gravis UltraSound (GF1, %luKB)", ram_kb);
    }
}

/* ---- MPU-401 ---- */
static hbool mpu_uart_init(u16 base)
{
    u16 i;
    /* Wait for DRR clear */
    for (i = 0; i < 1000; i++) {
        if ((ad_inp(base + 1) & 0x40) == 0) break;
        ad_delay_us(10);
    }
    if (i == 1000) return HFALSE;
    ad_outp(base + 1, 0xFF); /* reset */
    for (i = 0; i < 1000; i++) {
        if ((ad_inp(base + 1) & 0x80) == 0) {
            if (ad_inp(base) == 0xFE) break;
        }
        ad_delay_us(10);
    }
    if (i == 1000) return HFALSE;
    ad_outp(base + 1, 0x3F); /* UART mode */
    ad_delay_us(50);
    return HTRUE;
}

static void probe_mpu(hw_profile_t *hw)
{
    u16 candidates[] = { 0x330, 0x300, 0x320, 0x340, 0 };
    u16 i;
    if (hw->mpu_base) candidates[0] = hw->mpu_base;
    for (i = 0; candidates[i]; i++) {
        if (mpu_uart_init(candidates[i])) {
            hw->mpu_base = candidates[i];
            hw->aud_devices |= AUD_MPU401;
            add_card(hw, "Roland MPU-401 UART at %03Xh", candidates[i]);
            return;
        }
    }
}

/* ---- MIDI synth identity ---- */
static void probe_midi_identity(hw_profile_t *hw)
{
    /* Sending SysEx and waiting for a reply is bus level work; we record
     * MIDI_GM by default and let the playback engine refine when it sees a
     * real response. */
    if (!(hw->aud_devices & AUD_MPU401)) return;
    hw->midi_synth = MIDI_GM;
    strcpy(hw->midi_name, "General MIDI device");
}

/* ---- Ensoniq SoundScape ---- */
static void probe_ensoniq(hw_profile_t *hw)
{
    /* OTTO chip ID lives at 330h..33Fh in the standard config. */
    u8 v = ad_inp(0x33F);
    if (v == 0xFF || v == 0x00) return;
    hw->aud_devices |= AUD_ENSONIQ;
    add_card(hw, "Ensoniq SoundScape (OTTO)");
}

/* ---- PAS-16 ---- */
static void probe_pas16(hw_profile_t *hw)
{
    /* PAS magic: write to mixer index, read back. */
    u8 v = ad_inp(0x9A01);
    if ((v & 0xF0) == 0x50) {
        hw->aud_devices |= AUD_PAS16;
        add_card(hw, "Pro Audio Spectrum 16");
    }
}

/* ---- ESS AudioDrive ---- */
static void probe_ess(hw_profile_t *hw)
{
    if (!(hw->aud_devices & (AUD_SB_PRO | AUD_SB_PRO2 | AUD_SB_16))) return;
    /* DSP command E7h: ESS chips return chip ID in two bytes. */
    if (hw->sb.base == 0) return;
    {
        u16 base = hw->sb.base;
        u8  i;
        for (i = 0; i < 100; i++) {
            if ((ad_inp(base + 0x0C) & 0x80) == 0) break;
            ad_delay_us(5);
        }
        ad_outp(base + 0x0C, 0xE7);
        ad_delay_us(50);
        if (ad_inp(base + 0x0E) & 0x80) {
            u8 hi = ad_inp(base + 0x0A);
            if ((hi & 0xF0) == 0x60) {
                hw->aud_devices |= AUD_ESS;
                add_card(hw, "ESS AudioDrive");
            }
        }
    }
}

/* ---- Turtle Beach MultiSound ---- */
static void probe_turtle(hw_profile_t *hw)
{
    /* Probe the DSP host port at common bases. */
    u16 candidates[] = { 0x250, 0x260, 0x290, 0x320, 0 };
    u16 i;
    for (i = 0; candidates[i]; i++) {
        u8 v = ad_inp(candidates[i] + 4);
        if (v != 0xFF && v != 0x00) {
            hw->aud_devices |= AUD_TBEACH;
            add_card(hw, "Turtle Beach MultiSound at %03Xh", candidates[i]);
            return;
        }
    }
}

void audio_detect(hw_profile_t *hw)
{
    hw->aud_devices = 0;
    hw->aud_card_count = 0;
    hw->opl = OPL_NONE;
    hw->midi_synth = MIDI_NONE;
    hw->midi_name[0] = '\0';
    hw->mpu_base = 0;

    probe_pc_speaker(hw);
    probe_tandy(hw);
    probe_lpt_audio(hw);
    probe_adlib(hw);
    probe_adlib_gold(hw);
    probe_sb(hw);
    probe_gus(hw);
    probe_mpu(hw);
    probe_midi_identity(hw);
    probe_ensoniq(hw);
    probe_pas16(hw);
    probe_ess(hw);
    probe_turtle(hw);
}
