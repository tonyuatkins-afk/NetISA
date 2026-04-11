# NetISA

**Bringing TLS 1.3 and the modern internet to vintage ISA PCs.**

NetISA is an open-source 8/16-bit ISA expansion card that gives IBM PC/XT/AT and 386/486 systems a first-class path to the modern internet. A Microchip ATF1508AS CPLD handles ISA bus timing deterministically; an Espressif ESP32-S3 handles WiFi, TLS 1.3, and the full TCP/IP stack using hardware-accelerated AES, SHA, RSA, and ECC. The host PC sees a register-mapped coprocessor and talks to it through a small DOS TSR — no proxy box, no serial bottleneck, no software crypto on the retro CPU.

## What this unlocks

A retro PC with a NetISA card can, without a modern computer babysitting it:

- **Browse the modern HTTPS web** from Lynx, Links, or Arachne — real TLS 1.3 sites including Wikipedia, GitHub, and news sites.
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

## Roadmap beyond DOS

v1 ships as a DOS/Windows 3.x peripheral. The firmware and CPLD are deliberately architected so that native drivers for modern retro operating systems can land in future releases **without any hardware changes** — the ISA interface is a mode-agnostic byte shuttle, and new behavior lives entirely in ESP32 firmware and host-OS drivers.

- **v1.0** — MS-DOS / FreeDOS / Windows 3.x, Session Mode: card owns TCP/IP and TLS, host talks at the session level via INT 63h.
- **v2.0** — Windows 95/98/NT and Linux/BSD kernel drivers, **NIC Mode**: card presents as a raw Ethernet adapter, host OS runs its own TCP/IP stack. Enables NDIS miniports for Win9x/NT, `net_device` drivers for Linux, and native drivers for NetBSD and FreeBSD. Legacy Winsock applications (Netscape 4, IE3/4, mIRC, ICQ) gain TLS 1.3 through an optional Winsock LSP that routes port-443/993/465 traffic through the card's session-mode engine.
- **v2.5** — **NIC + kTLS Offload Mode**: Linux kTLS and FreeBSD kTLS integration. Host kernel does TCP/IP and the one-shot TLS handshake in software; the card transparently handles per-packet AES-GCM record framing on established sessions. This is the same architecture Mellanox ConnectX, Chelsio T6, and Intel E810 use for kTLS-capable datacenter NICs — brought to a 486.

The three driver modes are specified in [docs/netisa-architecture-spec.md](docs/netisa-architecture-spec.md) section 2.6.1. v1 firmware already recognizes the `CMD_SET_MODE` opcode so that future host drivers probing for advanced-mode support receive a clean, defined error response rather than silent failure. The register map, CPLD logic, and electrical interface are forward-compatible with all three modes on day one.

## Status

**Phase 0: Parts ordered, awaiting hardware.**

- Architecture specification complete (2,800+ lines)
- CPLD logic: **94/128 macrocells (73%)**, fits EPM7128STC100-15 clean on Quartus II 13.0sp1
- Verilog testbench: **61/61 passing** (iverilog) — covers address decode, IOCHRDY wait states, watchdog timeout, IRQ state machine, register window
- JEDEC file generated via POF2JED, ready to program
- ESP32-S3 firmware builds clean on ESP-IDF v5.5.4
- DOS loopback test assembled
- Reviewed by five AI reviewers
- All build artifacts ready to flash

Next step: solder breakout boards, wire prototype, walk the 9-gate validation checklist.

## Hardware

- **Bus logic:** Microchip ATF1508AS CPLD (TQFP-100, 128 macrocells, 5V native, 10 ns)
- **MCU:** Espressif ESP32-S3-WROOM-1U-N8R8 (WiFi, hardware AES/SHA/RSA/ECC, 8 MB flash, 8 MB PSRAM)
- **Ethernet:** Wiznet W5500 (v1.5, optional)
- **Antenna:** External U.FL to bracket-mount RP-SMA (required for metal PC cases)

## Software

- DOS TSR driver (~2 KB resident) providing INT 63h API
- SDK: NETISA.H + NETISA.LIB (OpenWatcom) + NETISA_TC.LIB (Turbo C)
- PC/TCP Packet Driver for mTCP/WATTCP compatibility (v1.5)

## Repository Structure

```
docs/
  netisa-architecture-spec.md    Full architecture specification
phase0/
  cpld/
    netisa.v                     Verilog source (Quartus II path, recommended)
    netisa_tb.v                  Verilog testbench (61 tests)
    netisa.pld                   CUPL source (WinCUPL path, alternative)
  firmware/
    main/main.c                  ESP32-S3 Phase 0 loopback firmware
  dos/
    nisatest.asm                 DOS loopback test program (NASM)
  README.md                      Phase 0 wiring guide and validation checklist
```

## Building

See [Phase 0 README](phase0/README.md) for build instructions and wiring guide.

## License

MIT (software) / CERN-OHL-P (hardware). See [LICENSE](LICENSE).

## Author

Tony Atkins ([@tonyuatkins-afk](https://github.com/tonyuatkins-afk))
