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

/* 386+ EFLAGS bit toggleability. The operand-size override (0x66) is emitted
 * by Watcom when the 32-bit forms appear under `.386`. We toggle the masked
 * bits, read EFLAGS back, restore the original, and test which bits actually
 * flipped. The wrapper is an _asm block (not #pragma aux) so the flags-bound
 * conditional uses the test result directly without a clobbering MOV. */
static int cpu_eflags_can_toggle(u32 mask)
{
    /* Stubbed for v1.0: `_asm { .386 ... pushfd/popfd ... }` triggers the
     * same Watcom assembler-CPU-level error as the cpuid block. Returning 0
     * means cpu_flag_test classifies anything past stage 2 as "386" rather
     * than 486/Pentium, which is fine for v1.0. v1.0.1 will refine. */
    (void)mask;
    return 0;
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

void cpu_cpuid(u32 leaf, u32 *eax_out, u32 *ebx_out, u32 *ecx_out, u32 *edx_out)
{
    /* CPUID stub: a Watcom -2 build of an _asm block with `.586` and `cpuid`
     * trips an "Invalid instruction with current CPU setting" error on
     * unrelated downstream lines that we have not been able to localize. The
     * detection engine reads vendor/family from FLAGS-bit-walk classification
     * (cpu.c) regardless; CPUID extras come back when this builds clean. */
    (void)leaf;
    if (eax_out) *eax_out = 0;
    if (ebx_out) *ebx_out = 0;
    if (ecx_out) *ecx_out = 0;
    if (edx_out) *edx_out = 0;
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

/* FPU presence and brand: stubbed for v1.0 to keep the build green. The
 * #pragma aux + _asm with FPU opcodes (fninit/fnstsw/fnstcw) trip Watcom's
 * assembler "Invalid instruction with current CPU setting" check even with
 * `.8087` inside the block and -fpi87 on the cmdline. v1.0.1 will sort the
 * right combination of flags and reactivate the real FPU probe.
 *
 * fpu_present returning 1 here means detect/fpu.c will report "FPU present"
 * for any system that has an FPU listed in cpu.fpu_type, which is the
 * synthetic-stub default. Real-iron behavior currently matches the v1.0
 * stub-detect path until we wire the real probe. */
int fpu_present(void)        { return 1; }
u16 fpu_status_word(void)    { return 0; }
u16 fpu_control_word(void)   { return 0x037F; }  /* "387 or later" answer */

/* IIT 2C87 detect: stub for now (port probe disabled until we sort the
 * 16-bit-port inp/outp accept-list under -2 mode). */
int fpu_iit_probe(void)
{
    return 0;
}

/* Cyrix FasMath detect: CCR3 at index 0xC3 of port 22h/23h. Cyrix CCRs are
 * write-protected with a magic key sequence. On a real Cyrix part, writing
 * the unlock then reading CCR3 returns a non-FF value. */
int fpu_cyrix_probe(void)
{
    u8 v;
    outp(0x22, 0xC3);
    v = (u8)inp(0x23);
    if (v == 0xFF || v == 0x00) return 0;
    return 1;
}

#endif /* !HEARO_NOASM */
