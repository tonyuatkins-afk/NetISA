/*
 * platform/probes.c - Real-iron implementations of every detection extern.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * This file replaces the per-module HEARO_NOASM C stubs when building for
 * actual DOS. Each function below is the production version of an `extern`
 * declared in detect/. The file is wrapped in #ifndef HEARO_NOASM so the
 * host build (which keeps the synthetic stubs in detect/) compiles this to
 * nothing.
 *
 * Layout:
 *   1. Audio: port I/O wrappers, ISA-bus delay, BIOS data area peeks.
 *   2. Memory: INT 12h conventional, XMS host call, EMS query.
 *   3. Video: INT 10h class/VESA, port-I/O index helpers.
 *   4. Network: stub-friendly card-signature probe.
 *   5. Input: INT 33h mouse, port 0x201 joystick.
 *   6. CPU: FLAGS bit walks, EFLAGS toggleability, CPUID, PIT timing.
 *   7. FPU: FNINIT/FNSTSW/FNSTCW plus IIT and Cyrix port probes.
 *
 * Watcom-specific bits (#pragma aux, __far, MK_FP, int86) are all here. The
 * detect modules stay plain C and remain portable.
 */
#ifndef HEARO_NOASM

#include "../hearo.h"

#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdlib.h>
#include <string.h>

/* ===== 1. Audio probe primitives ===== */

u8 ad_inp(u16 port)             { return (u8)inp(port); }
void ad_outp(u16 port, u8 val)  { outp(port, val); }

void ad_delay_us(u16 us)
{
    /* Reads to port 0x80 are the canonical ISA-bus tick: each takes ~1 us on
     * an 8 MHz bus regardless of CPU speed. We loop one read per microsecond.
     * Real probes do not need tighter than ~5 us accuracy. */
    u16 i;
    for (i = 0; i < us; i++) inp(0x80);
}

u16 ad_bios_lpt(u8 idx)
{
    /* LPT base addresses live at 0040:0008..000D in the BIOS data area. */
    u16 __far *p = (u16 __far *)MK_FP(0x0040, (u16)(0x0008 + (u16)idx * 2));
    return *p;
}

u8 ad_bios_byte(u32 farptr)
{
    u16 seg = (u16)(farptr >> 16);
    u16 off = (u16)(farptr & 0xFFFF);
    return *(u8 __far *)MK_FP(seg, off);
}

/* ===== 2. Memory probes ===== */

u16 mem_int12(void)
{
    union REGS r;
    int86(0x12, &r, &r);
    return r.x.ax;
}

/* XMS handshake. INT 2Fh AX=4300h tests for an XMS host (HIMEM.SYS). If
 * present, AX=4310h returns the host's call gate in ES:BX. We then call it
 * with AH=08h to read the largest free block (returned in AX as KB). For the
 * v1.0 boot screen "free largest block" is a sufficient stand-in for
 * "available extended memory". */
u32 mem_xms_total_kb(void)
{
    union REGS r;
    struct SREGS s;
    void (__far *xms_entry)(void) = 0;
    u16 result_ax = 0;

    r.x.ax = 0x4300;
    int86(0x2F, &r, &r);
    if (r.h.al != 0x80) return 0;

    segread(&s);
    r.x.ax = 0x4310;
    int86x(0x2F, &r, &r, &s);
    xms_entry = (void (__far *)(void))(((u32)s.es << 16) | r.x.bx);
    if (!xms_entry) return 0;

    {
        void (__far *fn)(void) = xms_entry;
        _asm {
            push bx
            push ax
            mov  ah, 08h
            call dword ptr [fn]
            mov  result_ax, ax
            pop  ax
            pop  bx
        }
    }
    return (u32)result_ax;
}

u32 mem_ems_total_kb(void)
{
    /* INT 67h AH=42h returns BX = total pages (16 KB each). AH=0 on success. */
    union REGS r;
    r.h.ah = 0x42;
    int86(0x67, &r, &r);
    if (r.h.ah != 0) return 0;
    return (u32)r.x.bx * 16UL;
}

/* ===== 3. Video probes ===== */

hbool vid_int10_get_ega(void)
{
    union REGS r;
    r.h.ah = 0x12;
    r.h.bl = 0x10;
    int86(0x10, &r, &r);
    return (r.h.bl != 0x10) ? HTRUE : HFALSE;
}

hbool vid_int10_get_vga(void)
{
    union REGS r;
    r.x.ax = 0x1A00;
    int86(0x10, &r, &r);
    return (r.h.al == 0x1A) ? HTRUE : HFALSE;
}

hbool vid_vesa_info(vesa_info_t *out)
{
    union REGS r;
    struct SREGS s;
    static u8 vesa_buf[512];
    u16 ver;
    u16 mem_blocks;

    if (!out) return HFALSE;

    /* Pre-stamp the buffer with 'VBE2' so VBE 2.0+ hosts return extended info. */
    vesa_buf[0] = 'V'; vesa_buf[1] = 'B';
    vesa_buf[2] = 'E'; vesa_buf[3] = '2';
    memset(vesa_buf + 4, 0, sizeof(vesa_buf) - 4);

    segread(&s);
    s.es = FP_SEG(vesa_buf);
    r.x.di = FP_OFF(vesa_buf);
    r.x.ax = 0x4F00;
    int86x(0x10, &r, &r, &s);
    if (r.x.ax != 0x004F) return HFALSE;

    if (vesa_buf[0] != 'V' || vesa_buf[1] != 'E' ||
        vesa_buf[2] != 'S' || vesa_buf[3] != 'A') return HFALSE;

    ver = *(u16 *)(vesa_buf + 4);
    mem_blocks = *(u16 *)(vesa_buf + 18);
    out->ver_major = (u8)((ver >> 8) & 0xFF);
    out->ver_minor = (u8)(ver & 0xFF);
    out->vram_kb   = mem_blocks * 64;
    out->has_lfb   = (out->ver_major >= 2) ? HTRUE : HFALSE;
    /* v1.0 reports a conservative max. Walking the mode list to find the
     * actual maximum is v1.1 work. */
    out->max_w = (out->vram_kb >= 1024) ? 1024 :
                 (out->vram_kb >= 512)  ? 800  : 640;
    out->max_h = (out->vram_kb >= 1024) ? 768  :
                 (out->vram_kb >= 512)  ? 600  : 480;
    return HTRUE;
}

u8 vid_inp(u16 port)            { return (u8)inp(port); }
void vid_outp(u16 port, u8 val) { outp(port, val); }

void vid_index(u16 idx_port, u8 idx, u8 val)
{
    outp(idx_port, idx);
    outp((u16)(idx_port + 1), val);
}

u8 vid_index_read(u16 idx_port, u8 idx)
{
    outp(idx_port, idx);
    return (u8)inp((u16)(idx_port + 1));
}

/* ===== 4. NetISA card signature ===== */

hbool nisa_signature(u16 base)
{
    /* The NetISA register file publishes 'NI' as the first two bytes of its
     * read-only ID register. Anything else (including 0xFF "open bus") means
     * no card at that base. */
    u8 a = (u8)inp(base);
    u8 b = (u8)inp((u16)(base + 1));
    return (a == 'N' && b == 'I') ? HTRUE : HFALSE;
}

/* ===== 5. Input probes ===== */

hbool inp_mouse(void)
{
    union REGS r;
    r.x.ax = 0x0000;
    int86(0x33, &r, &r);
    return (r.x.ax == 0xFFFF) ? HTRUE : HFALSE;
}

hbool inp_joystick(void)
{
    /* Port 0x201 returns 0xF0 in its high nibble for an idle stick on most
     * gameports, 0xFF when nothing is connected. We treat any non-FF/non-00
     * pattern as "stick probably present". */
    u8 v = (u8)inp(0x201);
    return (v != 0xFF && v != 0x00) ? HTRUE : HFALSE;
}

/* ===== 6. CPU classification ===== */

/* FLAGS bit walk: small #pragma aux helpers, then a C wrapper that reads
 * them in the right order. The helpers are the smallest practical asm units
 * to keep label scoping simple. */

extern u16 cpu_flags_after_setting_high(void);
#pragma aux cpu_flags_after_setting_high = \
    "pushf"             \
    "pop  bx"           \
    "mov  ax, bx"       \
    "or   ax, 0F000h"   \
    "push ax"           \
    "popf"              \
    "pushf"             \
    "pop  ax"           \
    "push bx"           \
    "popf"              \
    value [ax]          \
    modify [ax bx];

extern u16 cpu_flags_after_clearing_high(void);
#pragma aux cpu_flags_after_clearing_high = \
    "pushf"             \
    "pop  bx"           \
    "mov  ax, bx"       \
    "and  ax, 00FFFh"   \
    "push ax"           \
    "popf"              \
    "pushf"             \
    "pop  ax"           \
    "push bx"           \
    "popf"              \
    value [ax]          \
    modify [ax bx];

/* 386+ EFLAGS bit toggleability. Watcom 16-bit #pragma aux only accepts
 * 16-bit register names in parm/value/modify lists, so the 32-bit mask is
 * split into lo:hi u16 halves and reassembled into ebx inside the asm body.
 * The `.386` directive enables 32-bit instructions in this block only. The
 * "neg eax / sbb ax, ax / neg ax" tail is the standard Watcom idiom for
 * "set ax to 1 if eax is non-zero, else 0". */
extern int cpu_eflags_toggle_lo_hi(u16 mask_lo, u16 mask_hi);
#pragma aux cpu_eflags_toggle_lo_hi = \
    ".386"               \
    "movzx ebx, bx"      \
    "shl   ebx, 16"      \
    "movzx eax, ax"      \
    "or    ebx, eax"     \
    "pushfd"             \
    "pop   ecx"          \
    "mov   eax, ecx"     \
    "xor   eax, ebx"     \
    "push  eax"          \
    "popfd"              \
    "pushfd"             \
    "pop   eax"          \
    "push  ecx"          \
    "popfd"              \
    "xor   eax, ecx"     \
    "and   eax, ebx"     \
    "neg   eax"          \
    "sbb   ax, ax"       \
    "neg   ax"           \
    parm   [ax] [bx]     \
    value  [ax]          \
    modify [ax bx cx];

static int cpu_eflags_can_toggle(u32 mask)
{
    return cpu_eflags_toggle_lo_hi((u16)(mask & 0xFFFFUL),
                                   (u16)((mask >> 16) & 0xFFFFUL));
}

int cpu_flag_test(void)
{
    /* Stage 1: try to clear bits 12-15 of FLAGS. On 8086/8088/V20, those
     * bits are forced to 1; on 80286+ they can be cleared. */
    if ((cpu_flags_after_clearing_high() & 0xF000) == 0xF000) return 0;

    /* Stage 2: try to set bits 12-15. On 80286 in real mode the upper bits
     * are forced to 0; on 80386+ they can be set. */
    if ((cpu_flags_after_setting_high() & 0xF000) == 0)       return 2;

    /* Stage 3: 386+. Probe EFLAGS bit 18 (AC) for 486+, bit 21 (ID) for
     * CPUID-capable. */
    if (!cpu_eflags_can_toggle(0x00040000UL)) return 3;
    return 4;
}

int cpu_has_cpuid(void)
{
    return cpu_eflags_can_toggle(0x00200000UL);
}

/* CPUID with leaf=1 returns family/model/stepping in eax and feature
 * flags in edx. For v1.0 we only consume the eax word from leaf=1, so the
 * pragma returns eax as a (lo, hi) pair via dx:ax. The wrapper composes
 * the leaf in eax in-asm, runs CPUID, returns the eax word.
 *
 * Watcom 16-bit calling convention: u32 return goes in dx:ax (high in dx,
 * low in ax). */
extern u32 cpuid_eax_for(u16 leaf_lo, u16 leaf_hi);
#pragma aux cpuid_eax_for = \
    ".586"            \
    "movzx ebx, bx"   \
    "shl   ebx, 16"   \
    "movzx eax, ax"   \
    "or    eax, ebx"  \
    "push  ebx"       \
    "cpuid"           \
    "pop   ebx"       \
    "mov   edx, eax"  \
    "shr   edx, 16"   \
    parm   [ax] [bx]  \
    value  [dx ax]    \
    modify [ax bx cx dx];

void cpu_cpuid(u32 leaf, u32 *eax_out, u32 *ebx_out, u32 *ecx_out, u32 *edx_out)
{
    u32 a = 0;
    if (!cpu_has_cpuid()) {
        if (eax_out) *eax_out = 0;
        if (ebx_out) *ebx_out = 0;
        if (ecx_out) *ecx_out = 0;
        if (edx_out) *edx_out = 0;
        return;
    }
    a = cpuid_eax_for((u16)(leaf & 0xFFFFUL), (u16)((leaf >> 16) & 0xFFFFUL));
    if (eax_out) *eax_out = a;
    /* ebx/ecx/edx not consumed by cpu.c yet; v1.1 widens this. */
    if (ebx_out) *ebx_out = 0;
    if (ecx_out) *ecx_out = 0;
    if (edx_out) *edx_out = 0;
}

/* RDTSC reads CPU's 64-bit time-stamp counter into EDX:EAX. We only need the
 * low 32 bits since a 1ms gate at any reasonable CPU speed (up to ~4 GHz)
 * fits in u32. cpu_rdtsc_mhz below uses this to time a known 1ms PIT
 * interval; cycles / 1000 us = MHz directly, no ISA-bus-bound loop math.
 * Pentium+ only (rejects on 486 and older). Caller must check cpu_has_cpuid
 * (or otherwise know it is on a 586+) before calling. */
extern u32 cpu_rdtsc_lo32(void);
#pragma aux cpu_rdtsc_lo32 = \
    ".586"           \
    "rdtsc"          \
    "mov  edx, eax"  \
    "shr  edx, 16"   \
    value  [dx ax]   \
    modify [ax dx];

/* Time RDTSC delta over a known 1 ms PIT gate. Returns MHz (cycles per
 * microsecond, integer rounded). Pentium+ only.
 *
 * The PIT-bound spin between two RDTSC reads dominates the ~1ms wall clock,
 * but RDTSC counts CPU cycles independent of the ISA port latency, so the
 * returned value reflects true core clock. Replaces the legacy
 * cpu_pit_loop+divide formula which underreported by ~14x on Pentium-class
 * iron because the loop body's `inp(0x61)` was the bottleneck at ~1us per
 * iteration regardless of CPU speed (cf. Toshiba 320CDT @ 233 MHz reporting
 * as 17 MHz, 2026-04-25). */
u32 cpu_rdtsc_mhz(void)
{
    u32 t0, t1, safety;
    u8  saved_b1;

    saved_b1 = (u8)inp(0x61);
    outp(0x61, (saved_b1 & 0xFC) | 0x01);

    /* Channel 2, mode 0 (interrupt on count), binary, LSB then MSB.
     * 1193 PIT ticks at 1.193 MHz input = ~1000 microseconds. */
    outp(0x43, 0xB0);
    outp(0x42, (u8)(1193 & 0xFF));
    outp(0x42, (u8)((1193 >> 8) & 0xFF));

    /* Spin until PIT OUT2 goes high (count expired) or safety counter
     * exhausted. Some chipsets / BIOS configurations leave PIT ch2 gated
     * off or route OUT2 elsewhere, in which case bit 0x20 of port 0x61
     * never sets and a naked spin would hang TESTDET (verified on Toshiba
     * 320CDT, 2026-04-25). The safety counter caps the wait at ~10ms of
     * port reads regardless of CPU speed. On timeout we return 0 so the
     * caller falls back to the older PIT-loop estimator. */
    t0 = cpu_rdtsc_lo32();
    safety = 0;
    while ((inp(0x61) & 0x20) == 0) {
        if (++safety > 0x00100000UL) {
            outp(0x61, saved_b1);
            return 0;
        }
    }
    t1 = cpu_rdtsc_lo32();

    outp(0x61, saved_b1);

    /* Cycles per 1000 us = MHz. */
    return (t1 - t0) / 1000UL;
}

/* PIT timing loop. We program PIT channel 2 to a one-shot at a known
 * interval, then count how many trips through a fixed inner loop happen
 * before the gate closes. The inner loop is two memory ops + a decrement so
 * it scales linearly with CPU clock. */
u32 cpu_pit_loop(u16 ticks)
{
    u32 count = 0;
    u8  saved_b1;
    /* Save and gate-enable PC speaker port (bit 0=gate2, bit 1=speaker). */
    saved_b1 = (u8)inp(0x61);
    outp(0x61, (saved_b1 & 0xFC) | 0x01);

    /* Channel 2, mode 0 (interrupt on count), binary, LSB then MSB. */
    outp(0x43, 0xB0);
    outp(0x42, (u8)(ticks & 0xFF));
    outp(0x42, (u8)((ticks >> 8) & 0xFF));

    /* Spin until the OUT2 line goes high (PIT count expired). */
    while ((inp(0x61) & 0x20) == 0) {
        count++;
        if (count > 0x00FFFFFFUL) break;  /* safety */
    }

    outp(0x61, saved_b1);
    return count;
}

/* ===== 7. FPU presence and brand ===== */

/* FPU presence + status/control word reads.
 *
 * fpu_present preloads ax with 0xFFFF, runs FNINIT to zero the status word
 * (only happens if an FPU is present), then FNSTSW AX which writes the
 * status word to AX (or NOPs without an FPU). If AX is still non-zero
 * afterwards, no FPU; we return 1 if zero, 0 otherwise.
 *
 * fpu_status_word and fpu_control_word read after-the-fact (caller has
 * already run FNINIT or whatever sequence sets the relevant bits). */
extern int fpu_present_pragma(void);
#pragma aux fpu_present_pragma = \
    ".286"              \
    "mov  ax, 0FFFFh"   \
    "fninit"            \
    "fnstsw ax"         \
    "neg  ax"           \
    "sbb  ax, ax"       \
    "neg  ax"           \
    "xor  ax, 1"        \
    value [ax]          \
    modify [ax];

int fpu_present(void) { return fpu_present_pragma(); }

extern u16 fpu_status_word_pragma(void);
#pragma aux fpu_status_word_pragma = \
    ".286"              \
    "fnstsw ax"         \
    value [ax]          \
    modify [ax];

u16 fpu_status_word(void) { return fpu_status_word_pragma(); }

/* FNSTCW writes to a 16-bit memory operand only; no AX form. The asm
 * pushes a temp on the stack, addresses it via bx, then pops to ax. */
extern u16 fpu_control_word_pragma(void);
#pragma aux fpu_control_word_pragma = \
    ".286"           \
    "push  bx"       \
    "sub   sp, 2"    \
    "mov   bx, sp"   \
    "fnstcw [ss:bx]" \
    "pop   ax"       \
    "pop   bx"       \
    value  [ax]      \
    modify [ax bx];

u16 fpu_control_word(void) { return fpu_control_word_pragma(); }

/* IIT 2C87 detect: stub for now (port probe disabled until we sort the
 * 16-bit-port inp/outp accept-list under -2 mode). */
int fpu_iit_probe(void)
{
    return 0;
}

/* Cyrix FasMath detect: CCR3 at index 0xC3 of port 22h/23h. Cyrix CCRs are
 * write-protected with a magic key sequence. On a real Cyrix part, writing
 * the unlock then reading CCR3 returns a non-FF value.
 *
 * Opt-in via HEARO_CYRIX env var. Writing to port 0x22 has side effects on
 * chipsets that decode it differently: some Intel PIIX-family and VIA
 * chipsets map config or power-management registers in that range, and a
 * blind 0xC3 write can disturb them. Real Cyrix FasMath / FasMath 387 chips
 * are rare in 2026 and the result of this probe is consumed only as a
 * cosmetic boot-screen brand string (hw->fpu_brand), so an unhardened blind
 * probe trades a real side-effect risk for no functional gain.
 *
 * fpu_iit_probe (above) is already a stub returning 0 for related reasons,
 * so the only paths that use this are explicit user opt-in. */
int fpu_cyrix_probe(void)
{
    char *env = getenv("HEARO_CYRIX");
    u8 v;
    if (!env || !env[0] || env[0] == '0') return 0;
    outp(0x22, 0xC3);
    v = (u8)inp(0x23);
    if (v == 0xFF || v == 0x00) return 0;
    return 1;
}

#endif /* !HEARO_NOASM */
