# NetISA

**Bringing TLS 1.3 and the modern internet to vintage ISA PCs.**

NetISA is an open-source 8/16-bit ISA expansion card that gives IBM PC/XT/AT and 386/486 systems a first-class path to the modern internet. A Microchip ATF1508AS CPLD handles ISA bus timing deterministically; an Espressif ESP32-S3 handles WiFi, TLS 1.3, and the full TCP/IP stack using hardware-accelerated AES, SHA, RSA, and ECC. The host PC sees a register-mapped coprocessor and talks to it through a small DOS TSR — no proxy box, no serial bottleneck, no software crypto on the retro CPU.

## What this unlocks

A retro PC with a NetISA card can, without a modern computer babysitting it:

- **Browse the modern HTTPS web** from Cathode (our built-in text-mode browser), Lynx, Links, or Arachne — real TLS 1.3 sites including Wikipedia, GitHub, and news sites.
- **`git clone` over HTTPS** on a 486 against GitHub or any modern git host.
- **Read Gmail / Outlook / Fastmail** from Pine, Pegasus, or mTCP mail clients (IMAPS, SMTPS).
- **Post to Discord, Mastodon, Bluesky, Matrix, or IRC-over-TLS** (Libera.Chat and friends) via REST APIs or IRCS.
- **Sync files to Dropbox, OneDrive, or Nextcloud** via WebDAV-over-HTTPS.
- **Publish MQTT-over-TLS** to smart home brokers from an 8088.
- **Fetch software and packages** from modern HTTPS-only archives with integrity checking.

What NetISA does **not** unlock: modern graphical web browsing (CPU-bound regardless of transport), streaming video, or anything that requires modern client compute. TLS was never the bottleneck for those.

## How this differs from existing solutions

- **Serial WiFi modems** (WiFi232, RetroWiFi, zimodem) already do TLS offload, but over RS-232 — throughput tops out around 11–20 KB/s, one socket at a time, with AT-command latency on every operation. NetISA is register-mapped on the ISA bus itself, supports multiple concurrent sessions, and doesn't tie up a COM port.
- **stunnel / HTTPS proxies** work, but require a second modern machine running alongside the retro PC. NetISA removes that dependency.
- **ESP32-based NE2000 emulators** put networking on ISA, but the retro CPU still has to run TLS in software — impractical on anything below a fast 486. NetISA offloads the crypto to dedicated silicon.

The specific thing that is new: an open-source, register-mapped ISA coprocessor that terminates TLS 1.3 in hardware-accelerated silicon, with deterministic bus timing on a CPLD so the crypto engine never fights the bus cycle.

## Status

**Phase 0:** Parts ordered, awaiting hardware. All build artifacts ready to flash.
**Phase 1:** DOS software stack complete and tested in DOSBox-X.

### What's built

| Component | Status | Description |
|-----------|--------|-------------|
| Architecture spec | Complete | 2,800+ line specification covering hardware, firmware, INT 63h API, security |
| CPLD logic (Verilog) | Complete | 95/128 macrocells, full 16-bit I/O decode, JEDEC ready to program |
| Verilog testbench | **160/160 passing** | Address decode, IOCHRDY, watchdog, IRQ, alias rejection, back-to-back cycles, and more |
| ESP32-S3 firmware | Complete | Builds clean on ESP-IDF v5.5.4, parallel bus handler with ISR on Core 0 |
| DOS TSR (NETISA.COM) | Complete | 678 bytes, hooks INT 63h, presence check, stub handlers |
| Screen library | Complete | Direct VGA text buffer rendering, CP437 box drawing, shared across all apps |
| INT 63h API (netisa.h) | Complete | Full API definition matching spec Section 4, C wrappers with inline INT 63h |
| Stub layer | Complete | Fake data for DOSBox-X testing without hardware |
| Launcher (NETISA.EXE) | Complete | WiFi setup, card status, system info, full-screen CP437 UI |
| **Cathode browser** | **v0.1** | Text-mode web browser: scrolling, link navigation, URL bar, stub pages |
| DOS loopback test | Complete | 256-byte bus validation (NISATEST.COM) |

### Next steps

1. Solder breakout boards, wire prototype, walk the 9-gate validation checklist
2. Connect stub layer to real card I/O ports after Phase 0 hardware passes
3. Build ESP32-side HTML-to-cell-stream renderer for Cathode
4. Implement real TLS session management in TSR

## Cathode: Text-Mode Web Browser

Cathode is a built-in ANSI text-mode web browser that renders modern HTTPS websites in CP437 on any DOS machine from 8088 to Pentium. The browser is split between DOS and ESP32:

- **DOS side** (built): receives a cell stream, manages scrollback, renders to VGA text buffer, handles keyboard navigation
- **ESP32 side** (planned): fetches URLs over HTTPS, parses HTML, converts to (char, attr) cell stream

Currently runs with stub pages for testing. Key features:
- 200-row scrollback buffer on far heap (~80KB)
- Link navigation via Tab/Shift-Tab with visual highlighting
- URL bar editing (F6)
- Back/forward history (20 entries)
- CP437 box-drawing tables, bullet lists, headings, horizontal rules
- Quality-gated: 5 rounds of adversarial review, all Fatal/Significant bugs fixed

```
CATHODE.EXE                     Start page
CATHODE.EXE about:test          Feature test page
CATHODE.EXE about:help          Keyboard shortcuts
```

## Roadmap beyond DOS

v1 ships as a DOS/Windows 3.x peripheral. The firmware and CPLD are deliberately architected so that native drivers for modern retro operating systems can land in future releases **without any hardware changes** — the ISA interface is a mode-agnostic byte shuttle, and new behavior lives entirely in ESP32 firmware and host-OS drivers.

- **v1.0** — MS-DOS / FreeDOS / Windows 3.x, Session Mode: card owns TCP/IP and TLS, host talks at the session level via INT 63h.
- **v2.0** — Windows 95/98/NT and Linux/BSD kernel drivers, **NIC Mode**: card presents as a raw Ethernet adapter, host OS runs its own TCP/IP stack. Enables NDIS miniports for Win9x/NT, `net_device` drivers for Linux, and native drivers for NetBSD and FreeBSD. Legacy Winsock applications (Netscape 4, IE3/4, mIRC, ICQ) gain TLS 1.3 through an optional Winsock LSP that routes port-443/993/465 traffic through the card's session-mode engine.
- **v2.5** — **NIC + kTLS Offload Mode**: Linux kTLS and FreeBSD kTLS integration. Host kernel does TCP/IP and the one-shot TLS handshake in software; the card transparently handles per-packet AES-GCM record framing on established sessions. This is the same architecture Mellanox ConnectX, Chelsio T6, and Intel E810 use for kTLS-capable datacenter NICs — brought to a 486.

The three driver modes are specified in [docs/netisa-architecture-spec.md](docs/netisa-architecture-spec.md) section 2.6.1. v1 firmware already recognizes the `CMD_SET_MODE` opcode so that future host drivers probing for advanced-mode support receive a clean, defined error response rather than silent failure. The register map, CPLD logic, and electrical interface are forward-compatible with all three modes on day one.

## Hardware

- **Bus logic:** Microchip ATF1508AS CPLD (TQFP-100, 128 macrocells, 5V native, 10 ns)
- **MCU:** Espressif ESP32-S3-WROOM-1U-N8R8 (WiFi, hardware AES/SHA/RSA/ECC, 8 MB flash, 8 MB PSRAM)
- **Ethernet:** Wiznet W5500 (v1.5, optional)
- **Antenna:** External U.FL to bracket-mount RP-SMA (required for metal PC cases)

## Software

- **DOS TSR** (NETISA.COM) — 678-byte INT 63h handler, under 2KB resident target
- **Launcher** (NETISA.EXE) — Full-screen card configuration UI
- **Cathode** (CATHODE.EXE) — Text-mode web browser
- **Screen library** (screen.h/screen.c) — Shared VGA rendering engine
- **INT 63h API** (netisa.h) — C wrappers for all API groups (0x00-0x07)
- **Stub layer** (netisa_stub.c) — Fake data for DOSBox-X testing

All DOS code targets 8088 real mode, compiled with OpenWatcom 2.0.

## Repository Structure

```
docs/
  netisa-architecture-spec.md      Full architecture specification (2,800+ lines)

dos/
  lib/
    screen.h, screen.c             VGA text buffer rendering library
    netisa.h, netisa.c             INT 63h API definition and C wrappers
    netisa_stub.c                  Stub implementation for testing
  tsr/
    netisa_tsr.asm                 TSR skeleton (NASM, INT 63h handler)
  launcher/
    main.c, menu.c, wifi.c,       NETISA.EXE launcher application
    status.c, menu.h
  cathode/
    main.c                         Cathode browser entry point
    browser.c, browser.h           Navigation state machine, history
    render.c, render.h             Page renderer (cells to VGA)
    page.c, page.h                 Page buffer (far heap, 200 rows)
    input.c, input.h               Keyboard handler
    urlbar.c, urlbar.h             URL bar editor
    stub_pages.c, stub_pages.h     Hardcoded test pages
  Makefile                         Builds all DOS software

phase0/
  cpld/
    netisa.v                       Verilog source (Quartus II, recommended)
    netisa_tb.v                    Verilog testbench (160 tests)
    netisa.pld                     CUPL source (DEPRECATED, historical reference)
  firmware/
    main/main.c                    ESP32-S3 Phase 0 loopback firmware
  dos/
    nisatest.asm                   DOS loopback test (NASM)
  WIRING.md                        Signal-by-signal wiring guide
  BRINGUP.md                       Bring-up playbook with logic analyzer captures
  BUILDLOG.md                      Build log and toolchain notes
  README.md                        Phase 0 overview and validation checklist

hardware/
  kicad/NetISA_RevA/               KiCad schematic and PCB layout

Makefile                           Top-level build: make dos, make sim, make test
```

## Building

### Prerequisites

- **OpenWatcom 2.0** (C:\WATCOM) — DOS C compiler
- **NASM** — Netwide Assembler for TSR and test programs
- **Icarus Verilog** (iverilog) — for running the testbench
- **DOSBox-X** — for testing DOS software

### Build targets

```bash
make all          # Build everything (DOS software + loopback test)
make dos          # Build TSR, launcher, and Cathode
make tsr          # Build NETISA.COM only
make launcher     # Build NETISA.EXE only
make cathode      # Build CATHODE.EXE only
make test         # Build phase0/dos/nisatest.com
make sim          # Run iverilog testbench (160 tests)
```

### Testing in DOSBox-X

```
MOUNT C C:\Development\NetISA
C:
CD DOS
NETISA.COM              (loads TSR, prints banner)
NETISA.COM              (prints "already resident")
NETISA.EXE              (launches card control panel)
cathode\CATHODE.EXE     (launches text-mode browser)
```

See [Phase 0 README](phase0/README.md) for hardware build instructions and wiring guide.

## License

MIT (software) / CERN-OHL-P (hardware). See [LICENSE](LICENSE).

## Author

Tony Atkins ([@tonyuatkins-afk](https://github.com/tonyuatkins-afk))
