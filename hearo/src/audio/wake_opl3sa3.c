/*
 * audio/wake_opl3sa3.c - Yamaha OPL3-SAx (YMF711/715/719) wake backend.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * The OPL3-SAx family is an ISA chip that emulates Sound Blaster Pro 2
 * (DSP v3.01) plus OPL3 plus a Crystal-codec WSS interface. Under Win9x's
 * "restart in MS-DOS mode" path, the chip's SB block is left in a
 * power-down state with SBPDR (control register 0x10) set; the DSP
 * responds to version queries but PCM output is gated until SBPDR is
 * cleared. Vendor utilities (UNISOUND.COM, YMFSB.EXE, OPL3SAX.EXE) clear
 * that state; vendor BIOSes do it via PnP enumeration; pure MS-DOS skips
 * it entirely. This backend duplicates the pre-init in HEARO so the
 * Toshiba 320CDT (and other YMF715-class laptops) plays from MS-DOS
 * without a separate vendor utility.
 *
 * Discovery: port probe at the known control-port bases. The OPL3-SA3
 * MISC register at index 0x0A is the validation oracle: the low 3 bits
 * are writable (so toggling them and reading back proves the chip is
 * alive on this base), and the original value's low 3 bits encode the
 * chip variant code per the research dossier. Then the MIC register at
 * index 0x09 has a known mask-stable readback at 0x8A.
 *
 * Init sequence is per the YMF715 datasheet (Yamaha, Feb 1997) and
 * mirrors what Linux's sound/isa/opl3sa2.c does, written from scratch
 * with no copied code.
 *
 * IRQ and DMA route values (registers 0x03 and 0x06) are hardcoded for
 * the canonical SB Pro 2 default (IRQ 5 / DMA 1). The chip's internal
 * routing is independent of physical IRQ wiring, but the first-cut
 * encoding here may not match systems that have remapped IRQ-A to a
 * different physical line. Iron testing on the 320CDT (primary target)
 * uses IRQ 5 / DMA 1 so the hardcoded values fit; remapping is future
 * work.
 *
 * PnP BIOS scan and ISA-PnP isolation are listed in the research dossier
 * as more reliable discovery methods. The port-probe fallback covers the
 * Toshiba 320CDT case; PnP BIOS scan is left as follow-up work.
 */
#include "wake.h"
#include <conio.h>
#include <stdio.h>

/* Control-register access. Index port at base+0, data port at base+1. */
#define OSA_REG_PM_CTRL    0x01  /* power management control */
#define OSA_REG_SYS_CTRL   0x02  /* system control: AT bus, DSP version */
#define OSA_REG_IRQ_ROUTE  0x03  /* OPL3 + SB IRQ route */
#define OSA_REG_DMA_ROUTE  0x06  /* SB DMA route */
#define OSA_REG_MASTER_L   0x07  /* master volume left, 0 = 0 dB unmuted */
#define OSA_REG_MASTER_R   0x08
#define OSA_REG_MIC_VOL    0x09  /* mic input volume */
#define OSA_REG_MISC       0x0A  /* misc: chip variant + control flags */
#define OSA_REG_SBPDR      0x10  /* SB power-down: 1 = suspended, 0 = woken */
#define OSA_REG_DPD        0x12  /* digital power-down per block */
#define OSA_REG_APD        0x13  /* analog power-down per block */

/* State stashed during probe so wake() can reuse the validated base
 * without re-running the port scan. Reset by every fresh probe call
 * so a misfired probe does not poison subsequent runs. */
static u16 g_base;
static u8  g_variant;

static u8 osa_read(u16 base, u8 reg)
{
    outp(base + 0, reg);
    return (u8)inp(base + 1);
}

static void osa_write(u16 base, u8 reg, u8 val)
{
    outp(base + 0, reg);
    outp(base + 1, val);
}

/* MISC fingerprint test. The low 3 bits are writable on every variant
 * we care about; flipping them with XOR 0x07 and reading back is a
 * non-destructive proof that the chip is alive at this base. The probe
 * restores the original value before returning so any bits beyond the
 * tested 3 stay untouched. Floating-bus reads of 0xFF / 0x00 are
 * rejected first because both happen to round-trip the toggle (0xFF
 * stays 0xFF on a floating bus regardless of what we write). */
static hbool misc_validate(u16 base, u8 *out_variant)
{
    u8 saved, probe, after;
    saved = osa_read(base, OSA_REG_MISC);
    if (saved == 0xFF || saved == 0x00) return HFALSE;
    probe = (u8)(saved ^ 0x07);
    osa_write(base, OSA_REG_MISC, probe);
    after = osa_read(base, OSA_REG_MISC);
    osa_write(base, OSA_REG_MISC, saved);
    if ((after & 0x07) != (probe & 0x07)) return HFALSE;
    /* The variant code in the original byte's low 3 bits, per the research
     * dossier: 1=YMF711(SA2), 2=YMF715(SA3), 3=YMF715B, 4=YM719(SA4),
     * 5=Toshiba Libretto SA3, 7=NeoMagic. 0 and 6 are reserved. We do
     * NOT gate validation on the variant code being a known value
     * because the bits are also writable and the original snapshot may
     * have come from a previous program that touched MISC; the toggle
     * test above is the actual liveness oracle. */
    if (out_variant) *out_variant = (u8)(saved & 0x07);
    return HTRUE;
}

/* MIC fingerprint test. Writing 0x8A and confirming that the readback
 * masked with 0x9F equals 0x8A is the canonical OPL3-SAx MIC probe per
 * the research dossier. Bits 5 and 6 of MIC have other meanings on some
 * variants so they are masked out of the comparison. The probe leaves
 * MIC at 0x9F (silenced) regardless of the prior value: a non-OPL3-SAx
 * chip that happens to live at this base would not have responded to
 * the 0x8A write meaningfully anyway, and 0x9F is a safe "muted mic"
 * default. */
static hbool mic_validate(u16 base)
{
    u8 after;
    osa_write(base, OSA_REG_MIC_VOL, 0x8A);
    after = osa_read(base, OSA_REG_MIC_VOL);
    osa_write(base, OSA_REG_MIC_VOL, 0x9F);
    return ((after & 0x9F) == 0x8A) ? HTRUE : HFALSE;
}

static hbool opl3sa3_probe(void)
{
    /* Bases per the research dossier. Order is the prompt's listed order;
     * 0x100 is second because Toshiba laptops (the primary target) use
     * 0x100 more often than 0x370 in practice, but a 320CDT could go
     * either way and the order does not matter beyond detection latency
     * (a few microseconds per miss). */
    static const u16 candidates[] = { 0x370, 0x100, 0x538, 0xE80, 0xF86, 0 };
    u16 i;
    u8  variant = 0;

    /* Reset cached state in case a prior probe failed mid-validation. */
    g_base    = 0;
    g_variant = 0;

    for (i = 0; candidates[i]; i++) {
        u16 base = candidates[i];
        if (!misc_validate(base, &variant)) continue;
        if (!mic_validate(base))            continue;
        g_base    = base;
        g_variant = variant;
        return HTRUE;
    }
    return HFALSE;
}

static hbool opl3sa3_wake(const hw_profile_t *hw)
{
    if (!g_base) return HFALSE;
    (void)hw;  /* not consulted yet; IRQ/DMA route values hardcoded for
                * the canonical SB Pro 2 default below. */

    /* Init sequence per the YMF715 datasheet, written from scratch:
     * SBPDR clear is the step that ungates SB-mode PCM, the order
     * matters because power management has to release before the
     * downstream blocks (DPD/APD) accept their own clears. */
    osa_write(g_base, OSA_REG_SBPDR,     0x00);  /* clear SBPDR -> wake SB block */
    osa_write(g_base, OSA_REG_PM_CTRL,   0x00);  /* PM_CTRL = D0 full power */
    osa_write(g_base, OSA_REG_DPD,       0x00);  /* clear all digital PD */
    osa_write(g_base, OSA_REG_APD,       0x00);  /* clear all analog PD (SB DAC) */
    osa_write(g_base, OSA_REG_SYS_CTRL,  0x00);  /* AT bus, DSP 3.01 (SB Pro 2) */
    /* IRQ_ROUTE 0x0A = both OPL3 and SB to IRQ-A. The chip's IRQ-A is
     * wired to physical IRQ 5 on the canonical Toshiba 320CDT layout,
     * which is also the SB Pro 2 default. Boards that have remapped
     * IRQ-A to a different line will need a remapping step we do not
     * implement here. */
    osa_write(g_base, OSA_REG_IRQ_ROUTE, 0x0A);
    /* DMA_ROUTE 0x04 = SB to DMA-A. Same wiring assumption: DMA-A is
     * the canonical DMA 1. */
    osa_write(g_base, OSA_REG_DMA_ROUTE, 0x04);
    /* MISC: VEN bit set, chip ID code as YMF715B (3) for compatibility
     * with downstream code that fingerprints the variant. */
    osa_write(g_base, OSA_REG_MISC,      0x83);
    osa_write(g_base, OSA_REG_MASTER_L,  0x00);  /* L = 0 dB, unmuted */
    osa_write(g_base, OSA_REG_MASTER_R,  0x00);  /* R = 0 dB, unmuted */
    return HTRUE;
}

const wake_backend_t opl3sa3_wake_backend = {
    "OPL3-SA3",
    opl3sa3_probe,
    opl3sa3_wake
};
