# Wrong chip family: YMF715 is ISA OPL3-SA3, not PCI AC'97

**The prior research doc was diagnosing the wrong chip.** The Yamaha YMF715 in the Toshiba Satellite 320CDT is an **ISA Plug-and-Play OPL3-SA3** (the "OPL3-SAx" / YMF71x-S family), not a PCI DS-1 chip. It has **no PCI configuration space, no AC'97 codec, and no AC-link**. Every register the prior doc quoted — PCI cfg 0x44 (CRST/XRST), PCI cfg 0x4A (PR0–PR3), AC'97 codec register 0x26 — belongs to a *different* Yamaha chip family (YMF724/740/744/754, the "DS-1/DS-XG" PCI line). Applying those writes to a YMF715 is, at best, inert; at worst it touches whatever device happens to live at PCI slot 0/function 0 of the laptop.

The user's symptom — SB Pro detection succeeds, the first DMA IRQ fires once with frames=2048, then DMA halts — is a textbook OPL3-SA3 partial-power-down state and matches the chip's documented `SBPDR` (Sound Blaster Power-Down Request) handshake that Win98's audio VxD asserts on shutdown. This is fixable from DOS with a short sequence of writes to the OPL3-SA3 control-port index/data pair (typically I/O 0x370/0x371).

Below: what is verified against primary sources, what the user should actually write, and what could not be confirmed.

## How we know YMF715 is OPL3-SA3, not a PCI part

The Yamaha datasheet for YMF715 (bitsavers, Feb 1997) opens: *"YMF715-S (OPL3-SA3) is a single audio chip that integrates OPL3 and its DAC, 16-bit Sigma-delta CODEC, MPU401 MIDI interface, joystick port… **fully compliant with Plug and Play ISA 1.0a**."* The pinout is pure ISA — `D7..0`, `A11..0`, `AEN`, `/IOW`, `/IOR`, `IRQ3/5/7/9/10/11`, `DRQ0/1/3`, `/DACK0/1/3` — with **no PCI signals anywhere**. The Linux kernel agrees: the driver lives at `sound/isa/opl3sa2.c` (ISA tree) and uses `struct isa_driver` + `pnp_driver`, with PnP IDs `YMH0021` and `YMH0030`. The OPL3-SAx family is **YMF701, YMF711 (SA2), YMF715 (SA3), YMF718, YMF719** — all ISA PnP. The PCI DS-1 line is a different silicon: YMF724, YMF740, YMF744, YMF754, with driver `sound/pci/ymfpci/`.

The Toshiba 320CDT does have a PCI bus (PCI 2.1, 32-bit/33 MHz per the Toshiba product spec), but the YMF715 sits on the laptop's *ISA* bus, configured by **PnP-BIOS**, not by ISA-PnP card-isolation. This matters because the popular UNISOUND tool only initializes ISA-PnP cards; the UNISOUND author explicitly notes laptop integrated Yamaha chips "are BIOS-PnP based, and those are not supported."

## The OPL3-SA3 register map that actually matters

OPL3-SA3 exposes **five separate I/O port ranges** assigned by PnP-BIOS: SB-Pro at 0x220 (16 bytes), WSS at 0x530 (8), AdLib/OPL3 at 0x388 (8), MPU-401 at 0x330 (2), and a **CTRL base at 0x370** (2 bytes — index/data pair). The CTRL pair is where all chip-wide configuration happens: write the register index to `CTRL_BASE+0`, then read or write data at `CTRL_BASE+1`. This is the same pattern as AdLib's index/data, but with no "password" — verified directly in Linux's `__snd_opl3sa2_read`/`__snd_opl3sa2_write`.

The control-register indices, taken **verbatim** from `sound/isa/opl3sa2.c`:

| Index | Name | Function |
|------|------|----------|
| 0x01 | `OPL3SA2_PM_CTRL` | Power management. Write `0x00` for full-on (D0). Bits ADOWN/PSV/PDN/PDX. |
| 0x02 | `OPL3SA2_SYS_CTRL` | SBHE (XT bus), IDSEL1:0 (SB DSP version returned to cmd `0xE1`; `00`→3.01=SB Pro 2), `ymode` 3D preset. |
| 0x03 | `OPL3SA2_IRQ_CONFIG` | Low nibble = IRQ-A mask {OPL3, MPU, **SB**, WSS}; high nibble = IRQ-B mask. |
| 0x04 / 0x05 | IRQ-A/B status (RO) | Pending-interrupt source flags. |
| 0x06 | `OPL3SA2_DMA_CONFIG` | Low nibble = DMA-A mask {**SB**, WSS-R, WSS-P}; high nibble = DMA-B mask. |
| 0x07 / 0x08 | Master Vol L/R | Mute bit + 4-bit attenuation, default `0x07` ≈ −14 dB. |
| 0x0A | `OPL3SA2_MISC` | Bit 7 VEN (HW vol enable), bits 2:0 chip ID (`0x03` = YMF715B). |
| **0x10** | SA3 SB-block PD/scan ctrl | **`SBPDR` bit 0** = "1 inhibits further DMA requests and begins SB shutdown." |
| 0x11 | SCAN_DATA | 27-byte internal-state save/restore for SB suspend/resume. |
| **0x12** | `OPL3SA3_DGTL_DOWN` | Per-block digital power-down: bit 1 = **SB block**. |
| **0x13** | `OPL3SA3_ANLG_DOWN` | Per-block analog power-down: bit 1 = **SB DAC**. |

The bolded rows are the ones implicated in the user's symptom. The datasheet's exact wording for `SBPDR` (index 0x10 bit 0) is *"'1' in this bit inhibits further DMA requests and have the internal state begin shutdown procedure"* — which matches "first IRQ fires, DMA halts" precisely.

## Why "first IRQ then dead" happens after Win98 → MS-DOS mode

Win98's `yamahasm.vxd` uses the documented OPL3-SA3 **Sound-Blaster Internal-State Scan** protocol on shutdown (datasheet §9-1-5, Fig 9-1): assert `SBPDR=1`, scan out 218 bits of internal SB state via index 0x11, then leave the SB block in partial power-down (bit 1 of indices 0x12 and 0x13). When you "Restart in MS-DOS mode," that VxD is gone but it never gets a chance to *resume* the chip — so the SB front-end stays barely-alive: it answers DSP reset and the version byte from its surviving register state, but **`SBPDR=1` causes the chip to refuse further DRQs after the first DMA transfer completes**. Linux's kernel driver for OPL3-SA3 also configures the chip in a Linux-only mode (`IRQ_CONFIG=0x0d`, `DMA_CONFIG=0x21`) that **deliberately leaves SB IRQ and SB DMA un-routed** because Linux uses WSS, not SB-Pro emulation — confirming that the SB IRQ/DMA routing in `0x03`/`0x06` is independently maskable and a plausible second cause.

So three independent mechanisms can each kill DMA after one IRQ on this chip: (1) `SBPDR=1` at index 0x10, (2) SB digital/analog power-down bits at 0x12/0x13, (3) SB DMA bit clear in `DMA_CONFIG` at 0x06.

## Concrete DOS init sequence for the user's HEARO

After detecting `BLASTER=A220 I5 D1` (and ideally discovering the **CTRL base** — usually 0x370 on this laptop, but should be confirmed from PnP-BIOS or by trying 0x370/0x100/0x538 in turn), perform these writes via the index/data pair at `CTRL_BASE`:

```
; CTRL_BASE is typically 0x370 on the 320CDT.
; Each register: outb(CTRL_BASE, index); outb(CTRL_BASE+1, value);

  index 0x10 <- 0x00   ; clear SBPDR/SS/SM/SE — wake SB block from suspend
  index 0x01 <- 0x00   ; PM_CTRL = D0 (full power)
  index 0x12 <- 0x00   ; clear all digital power-down bits (incl. SB)
  index 0x13 <- 0x00   ; clear all analog power-down bits (incl. SBDAC)
  index 0x02 <- 0x00   ; SYS_CTRL: AT bus, DSP version 3.01 (SB Pro 2)
  index 0x03 <- 0x0A   ; route OPL3+SB to IRQ-A (matches IRQ 5)
  index 0x06 <- 0x04   ; route SB to DMA-A bit 2 (matches DMA 1)
  index 0x0A <- 0x83   ; MISC: VEN | YMF715B chip-id
  index 0x07 <- 0x00   ; master left = 0 dB, unmuted
  index 0x08 <- 0x00   ; master right = 0 dB, unmuted

; Then the standard SB-side init:
  outb(0x226, 1); delay; outb(0x226, 0)        ; DSP reset
  read 0xAA from 0x22A                          ; DSP ready
  outb(0x224, 0x00); outb(0x225, 0x00)          ; mixer reset
  ; ...your existing SB Pro 2 DMA setup
```

The exact `IRQ_CONFIG` and `DMA_CONFIG` nibbles depend on whether the user wants SB on IRQ-A or IRQ-B and which DMA channel; the bit positions above (IRQ-A low nibble = `{OPL3, MPU, SB, WSS}`, DMA-A low nibble = `{SB, WSSR, WSSP}`) are verified from the kernel driver and the datasheet. The user's `BLASTER` line shows DMA 1, IRQ 5 — so SB needs DMA-A bit 2 set and IRQ-A bit 1 set; the values `0x04` and `0x0A` above produce that.

## Pragmatic shortcut: try existing DOS tools first

Before adding code to HEARO, the user can validate the diagnosis in seconds:

1. **`SETUPSA.EXE`** — Yamaha's own DOS configurator, ships in the OPL3-SAx Win3.1/95 driver pack on TheRetroWeb. It uses PnP-BIOS, so it works on laptops where UNISOUND fails.
2. **`SETYMF.EXE`** by Tiido (`tmeeco.eu/TKAYBSC/`) — recommended on VOGONS specifically for BIOS-PnP Yamaha laptops.
3. **`UNISOUND.EXE`** — works on ISA-PnP desktop OPL3-SAx cards but per its author *"laptop integrated Yamaha chips are BIOS-PnP based, and those are not supported for now."*

If any of these restore SB Pro DAC playback in pure DOS, the diagnosis is confirmed. The user can then port the equivalent register pokes into HEARO.

## SBEMU does not help here

**SBEMU is strictly PCI-only** — it scans PCI configuration space, so the YMF715 (ISA) is invisible to it. The supported list (verified at `github.com/crazii/SBEMU` README, release 1.0.0-beta.5, Aug 2024) is Intel ICH AC'97, VIA AC'97, SB Live!/Audigy, ES1371, CMI8338/8738, HDA, plus jiyunomegami's ports for **YMF724/740/744/754** (the *PCI* DS-1 family — coincidentally the family the prior doc was actually describing), ALS4000, OXYGEN, ESS Allegro, Trident 4DWave, X-Fi. **Neither SBEMU nor JEMMEX has SB emulation for OPL3-SAx**; JEMMEX is a memory manager that hosts QPIEMU.DLL for SBEMU's port-trapping. For the 320CDT, the right path is the in-process register init above, not SBEMU.

## Verified vs. hallucinated: claim-by-claim

| Prior-doc claim | Verdict | Evidence |
|---|---|---|
| YMF7xx PCI cfg 0x44 has CRST (b0) and XRST (b1), defaults to 1 | **VERIFIED for YMF724/744/754, NOT YMF715** | `snd_ymfpci_aclink_reset()` reads/writes `PCIR_DSXG_CTRL` at config offset 0x44, masks `0x03`, pulses CRST/XRST. Bit names match Yamaha YMF7x4 datasheet. **Does not apply to YMF715** (no PCI cfg space). |
| YMF7xx PCI cfg 0x4A has PR0–PR3 power bits | **VERIFIED with caveat (DS-1 only)** | Driver writes 0 to **both** `PCIR_DSXG_PWRCTRL1` (0x48) **and** `PCIR_DSXG_PWRCTRL2` (0x4A) — there are two power-control words, not one. Again DS-1 family only; **not YMF715**. |
| AC'97 Global Control is at codec register 0x26, write 0x0002 | **LIKELY HALLUCINATED — wrong on three counts** | (a) Codec reg 0x26 is **`AC97_POWERDOWN`** (Powerdown Ctrl/Stat) per AC'97 r2.3 spec — verified in Linux `include/sound/ac97/regs.h`, AD1985/AD1986 datasheets, Haiku, Minix, VirtualBox. (b) "Global Control" lives in the **host controller's** MMIO, not codec space — it's at MMIO offset **0x2C** on Intel ICH (`https://wiki.osdev.org/AC97`), not 0x26. (c) Writing `0x0002` to codec 0x26 sets `PR1` = power-down PCM DAC — the *opposite* of waking. The doc cross-wired codec offset 0x26 with controller offset 0x2C and value 0x0002. |
| AC'97 Master Vol at codec 0x02 | **VERIFIED** | `AC97_MASTER 0x02` in Linux, Minix, Haiku, VirtualBox; default `0x8000` (muted, 0 dB attenuation). |
| AC'97 PCM Out Vol at codec 0x18 | **VERIFIED** | `AC97_PCM 0x18` in same sources; default `0x8808`. |
| Citation markers like 【69†L1189-L1196】 | **HALLUCINATED** | Format is characteristic of fabricated source markers from prior LLM output; no such citation system exists in any kernel or datasheet source. |

The pattern is consistent: the prior research correctly described the **YMF724/744/754 PCI DS-1** family (the chip on a Yamaha desktop sound card from 1998–2000), then mis-applied it to a **YMF715 laptop chip**, and on top of that confused AC'97 codec register space with AC'97 host-controller register space.

## What we could not definitively verify

A handful of details remain inferential rather than directly cited. We could not retrieve the literal `#define PCIR_DSXG_CTRL 0x44` line from `ymfpci.h` (Bootlin/Codebrowser fetches were blocked in this session); the offset is confirmed by the FreeBSD twin driver, multiple Linux mirrors, the Yamaha YMF7x4 datasheet, and a kernel patch quoting `pci_write_config_word(chip->pci, 0x40, ...)` for the adjacent `PCIR_DSXG_LEGACY` register, but a direct view of the header would close the loop. UNISOUND.EXE is closed-source assembly (binary only), so its exact register sequence is inferred from its observable behavior plus the datasheet, not read from source. The Win98 `yamahasm.vxd` shutdown sequence is not publicly disassembled — the `SBPDR`+scan-out hypothesis is the documented OPL3-SA3 suspend protocol and matches the user's symptom, but cannot be proven without VxD reverse-engineering. Whether the 320CDT's specific BIOS uses BIOS-PnP (very likely, per UNISOUND's note about laptops) versus ISA-PnP card isolation should be confirmed by whether `UNISOUND` finds the chip at all; if it doesn't, that confirms BIOS-PnP. Finally, the AC'97 r2.3 spec PDF itself was not locatable on Intel's site; codec datasheets (AD1985/AD1986) corroborate the codec register layout cited above and conform to that spec.

## Conclusion: the actionable next step

The 320CDT's audio is recoverable in pure DOS without external tools — but only with code that targets the **OPL3-SA3 control-port index/data pair**, not anything in PCI configuration space and not anything on an AC-link. The user should (1) discover or hard-code `CTRL_BASE` (try 0x370 first), (2) issue the ten-write sequence above before SB DSP reset, (3) verify by also running SETUPSA or SETYMF as a sanity check, and (4) delete the prior research doc's PCI/AC'97 advice from the codebase entirely — it was about a completely different chip. The HEARO project's vendor-init layer should treat OPL3-SAx as its own back-end, distinct from the YMF724/744/754 PCI back-end SBEMU already covers, with `SBPDR`/`DGTL_DOWN`/`ANLG_DOWN` clearing as the distinguishing post-VxD recovery move.