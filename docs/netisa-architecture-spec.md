# NetISA Architecture Specification

**Version:** 0.1 DRAFT
**Date:** 2026-04-06
**Author:** Tony (AEUS / Barely Booting)
**Status:** Pre-release, community feedback requested

---

## 1. Project Summary

NetISA is an open-source 8/16-bit ISA expansion card that provides cryptographic offloading and secure network connectivity for IBM PC/XT, AT, and compatible systems running MS-DOS, FreeDOS, Windows 3.x, and Windows 9x.

The card handles all TLS 1.3 negotiation, certificate validation, symmetric encryption/decryption, TCP/IP socket management, DNS resolution, and network transport (WiFi and/or Ethernet). The host PC sees only cleartext data through a simple software interrupt (INT) API. No cryptographic code runs on the host.

NetISA is to modern cryptography what the 8087 was to floating-point math: a coprocessor that enables capabilities the host CPU cannot practically perform, while leaving all application logic on the original hardware.

### 1.1 Design Philosophy

1. **The host does the work.** NetISA is a transport-layer coprocessor, not an application platform. It provides defined services below the application layer: DNS resolution, TCP sockets, TLS encryption, and cryptographic primitives. Application logic, UI rendering, protocol parsing (HTTP, IMAP, IRC, JSON, etc.), and user interaction all run on the host CPU. The card never knows what protocol the application speaks. The boundary is explicit: if it requires understanding the content of the byte stream, it belongs on the host. If it requires encrypting, transporting, or authenticating the byte stream, it belongs on the card.

2. **The API is the product.** Hardware can be revised, firmware rewritten, processors swapped. The INT interface is the social contract with developers. It must be stable, versioned, and correct from v1. See Section 4.5 for API stability tiers.

3. **8-bit first, 16-bit bonus.** Every feature must work on an 8088 with 640K. 16-bit ISA transfers are a performance optimization for 286+ systems, never a requirement.

4. **Open everything.** KiCad schematics, Gerber files, firmware source, DOS drivers, and this specification are all released under a permissive open-source license (MIT or BSD, TBD).

### 1.2 Document Organization

This specification is a single comprehensive document covering hardware, firmware, software, security, use cases, development plan, and lessons learned. At v1 release, it should be split into:

- **Core Architecture Spec** (Sections 1-5): Hardware and firmware design decisions
- **INT API Reference** (Section 4, extracted): Standalone developer reference
- **Bring-Up and Validation Plan** (Section 10.1, extracted): Phase 0-6 procedures
- **Developer SDK Guide** (Section 12, extracted): Language choices, build instructions, examples
- **Security Model** (Section 8, extracted): For community review and trust-building

During pre-prototyping and Phase 0, the single-document format is intentional. Splitting prematurely creates synchronization problems between files. The split happens when the audience changes from "one developer building a prototype" to "a community building applications."

---

## 2. Hardware Architecture

### 2.1 Block Diagram

```
+------------------------------------------------------------------+
|  NetISA Card                                                    |
|                                                                   |
|  +----------+     +------------------+     +---------+            |
|  | ISA Bus   |<--->| ATF1508AS CPLD   |<--->| 16 MHz  |           |
|  | Connector |     | (5V native)      |     | OSC     |           |
|  | 8/16-bit  |     | Addr decode,     |     +---------+           |
|  +----------+     | data latch,      |                            |
|                    | IOCHRDY, IRQ,    |                            |
|                    | parallel bridge  |                            |
|                    +--------+---------+                            |
|                             | 16-pin parallel bus                  |
|                       +-----+------+                               |
|                       | Main MCU   |                               |
|                       | ESP32-S3   |                               |
|                       | (WiFi+TLS) |                               |
|                       +-----+------+                               |
|                             |                                      |
|               +-------------+-------------+                        |
|               |                           |                        |
|         +-----+------+          +---------+--+                     |
|         | WiFi       |          | Ethernet   |                     |
|         | (onboard)  |          | W5500      |                     |
|         | 802.11b/   |          | (optional) |                     |
|         |  g/n       |          | 10/100     |                     |
|         +------------+          +-----+------+                     |
|                                       |                            |
|  +-------------+  +------------+ +----+-------+                    |
|  | MicroSD     |  | Status     | | RJ45       |                    |
|  | (cert store |  | LEDs       | | (bracket)  |                    |
|  |  + firmware)|  | (bracket)  | +------------+                    |
|  +-------------+  +------------+                                   |
+------------------------------------------------------------------+
```

### 2.2 ISA Bus Interface

**Connector:** Full 16-bit ISA (98-pin: 62-pin C/D + 36-pin A/B extension). Functions correctly in both 8-bit (62-pin) and 16-bit (98-pin) slots.

**Bus Logic (reference implementation uses Microchip ATF1508AS CPLD):**

| Option | Part Example | Pros | Cons |
|--------|-------------|------|------|
| CPLD (recommended) | Microchip ATF1508AS (TQFP-100) | 128 macrocells, native 5V, 68 I/O pins, eliminates shared bus multiplex | SMD package requires reflow or hand soldering; PLCC-84 variant is obsolete |
| CPLD (space-constrained) | Microchip ATF1504AS (PLCC-44) | 64 macrocells, native 5V, smaller footprint | Requires external latches (2x 74HCT574) to avoid bus multiplex; tight on macrocells |
| CPLD (alternate) | Lattice LC4032V (TQFP-48) | 32 macrocells, 5V input tolerant, 3.3V output | Needs pull-ups for full 5V output swing; tight on macrocells for 16-bit support |
| GAL | Microchip ATF22V10C (DIP-24) | Period-correct, through-hole, still in production, ~$1.50 | Only 10 macrocells; would need 2-3 chips for full bus logic |
| Discrete 74-series | 74HCT138 + 74HCT245 + 74HCT574 | No programmer needed, fully transparent, DIP | 5-7 chips, more board space, no IOCHRDY watchdog logic |
| FPGA (future) | Lattice iCE40UP5K (QFN-48) | Open-source toolchain, internal FIFO memory, flexible | Not 5V tolerant (needs level shifters), external config flash, ~$6-8 |

**Address Decode:**

The CPLD monitors address lines A0-A9 plus AEN (Address Enable). When AEN is low (CPU-initiated I/O cycle, not DMA) and A3-A9 match the jumper-configured base address, the CPLD asserts an internal chip-select. A0-A3 then select the individual register within the card's 16-port window. The decode equation is:

```
CHIP_SELECT = !AEN & (A9..A4 == JUMPER_PATTERN) & (!IOR | !IOW)
```

No memory-mapped I/O in v1 (avoids UMB/adapter segment conflicts with EMS, VGA, ROM BIOS, and network boot ROMs).

### 2.3 Jumper Configuration

All configuration is via physical jumpers. ISA Plug-and-Play is explicitly not supported: PnP requires BIOS cooperation that is absent on 8088/8086 systems and unreliable on many 286/386 boards. Jumpers provide deterministic, visible, user-controlled configuration that works identically on every ISA-bus machine ever built.

#### 2.3.1 Base I/O Address (J1: 3-position DIP switch)

A 3-position DIP switch selects from 8 base addresses in the 0x200-0x3FF prototype/expansion range. Each address provides a 16-port window (base+0x00 through base+0x0F).

| DIP 1-2-3 | Base Address | Default? | Potential Conflicts |
|-----------|-------------|----------|-------------------|
| OFF-OFF-OFF | 0x280 | YES | Rare conflicts |
| OFF-OFF-ON | 0x290 | | Rare conflicts |
| OFF-ON-OFF | 0x2A0 | | Rare conflicts |
| OFF-ON-ON | 0x2C0 | | Rare conflicts |
| ON-OFF-OFF | 0x300 | | NE2000 default; avoid if NE2000 present |
| ON-OFF-ON | 0x310 | | Rare conflicts |
| ON-ON-OFF | 0x320 | | Rare conflicts |
| ON-ON-ON | 0x340 | | Rare conflicts |

**Addresses deliberately avoided:** 0x200-0x20F (game port), 0x220-0x22F (Sound Blaster), 0x330-0x331 (MPU-401 MIDI), 0x370-0x377 (FDC secondary/LPT), 0x378-0x37F (LPT1), 0x388-0x38B (AdLib/OPL), 0x3B0-0x3DF (VGA/MDA/CGA), 0x3F0-0x3F7 (FDC primary), 0x3F8-0x3FF (COM1), 0x2F8-0x2FF (COM2), 0x2E8-0x2EF (COM4), 0x3E8-0x3EF (COM3), 0x360-0x36F (common NIC alternate).

#### 2.3.2 IRQ Selection (J2: 6-position jumper block, single jumper)

Only one position should be jumpered at a time. The CPLD drives the selected ISA IRQ line through a tri-state output gated by the jumper position. The selected IRQ line is active-high, active only when the card needs to signal the host.

| Position | IRQ Line | Default? | Notes |
|----------|---------|----------|-------|
| 1 | IRQ 3 | | Conflicts with COM2 if present |
| 2 | IRQ 5 | YES | Free unless second LPT is configured for IRQ 5 |
| 3 | IRQ 7 | | Conflicts with LPT1; prone to spurious interrupts on some chipsets |
| 4 | IRQ 9 | | AT-class and above only (cascaded from IRQ 2). Safest pick for 286+ |
| 5 | IRQ 10 | | AT-class and above only. Commonly free |
| 6 | IRQ 11 | | AT-class and above only. Commonly free |

**XT compatibility note:** 8088/8086 systems only have IRQ 0-7 (single 8259 PIC). IRQ 9/10/11 are unavailable. The card MUST work with IRQ 3, 5, or 7 on XT-class machines. The TSR must also support a pure polling mode (no IRQ) for systems where no IRQ line is available, controlled by a /POLL command-line switch.

#### 2.3.3 Safe Mode (J3: 2-pin jumper)

When jumpered, the card boots with WiFi/Ethernet disabled and responds only to diagnostic commands (Group 0x00 and 0x07 in the INT API). The CPLD reads this pin at power-on and passes the state to the ESP32-S3 during initialization.

Use cases: debugging address conflicts, verifying the card is physically responding without network activity, firmware recovery.

#### 2.3.4 Jumper Physical Layout

All jumpers are grouped together near the top edge of the card, visible and accessible without removing the card from the slot. Each jumper block is silk-screened with its function, position labels, and default setting. A printed reference card is included with the board showing the full configuration matrix.

### 2.4 ISA Bus Signal Usage

#### 2.4.1 8-bit Mode (XT/8088 Compatible, 62-pin connector)

Directly active signals (directly active means these connect directly to the CPLD):

| Signal | Direction | Purpose |
|--------|-----------|---------|
| A0-A9 | Input | I/O address decode (10 bits = 1024 port range) |
| D0-D7 | Bidirectional | 8-bit data transfer |
| AEN | Input | Address Enable; low = CPU cycle, high = DMA (ignore) |
| IOR# | Input | I/O Read strobe; active low |
| IOW# | Input | I/O Write strobe; active low |
| IOCHRDY | Output (open-drain) | I/O Channel Ready; pull low to insert wait states |
| IRQ 3/5/7 | Output | Interrupt request (selected by jumper J2) |
| RESET DRV | Input | System reset; filtered before use (see below) |

**RESET DRV Filtering:** PicoGUS discovered that RESET DRV is noisy on several chipsets (notably ALi M6117D in the Hand386, and AMD Slot-A boards that toggle RESET on Ctrl-Alt-Del). Raw RESET DRV connected directly to the CPLD or ESP32 EN pin causes spurious resets that crash the card mid-operation. NetISA adds an **RC filter** (10K resistor + 100nF capacitor, time constant ~1ms) on the RESET DRV line before it reaches the CPLD. This rejects glitches shorter than ~1ms while still responding to genuine system resets (which hold RESET DRV active for 1-4ms on most systems). A 74AHC14 Schmitt trigger inverter (single gate from the 74AHC package) after the RC filter provides clean edges to the CPLD. The CPLD additionally requires RESET DRV to be active for a minimum of 2ms before initiating a reset sequence, providing a second layer of glitch rejection in logic.

**Out-of-machine operation:** When the card is not installed in a PC, RESET DRV floats. PicoGUS found this prevents firmware programming via USB/JTAG because the MCU stays in perpetual reset. NetISA adds a **weak pull-down (100K) on RESET DRV** so the signal defaults to "not reset" when the card is outside a slot. This allows the ESP32-S3 to boot and accept firmware updates via USB-JTAG without being installed in a PC.

#### 2.4.2 16-bit Mode (AT/286+ Compatible, 98-pin connector)

Additional signals on the 36-pin extension connector:

| Signal | Direction | Purpose |
|--------|-----------|---------|
| D8-D15 | Bidirectional | Upper data byte for 16-bit transfers |
| IOCS16# | Output | Asserted by card to indicate 16-bit I/O capability |
| IRQ 9/10/11 | Output | Higher interrupt lines (selected by jumper J2) |

IOCS16# assertion logic: The CPLD asserts IOCS16# when it detects its own address AND the 16-bit extension connector pins are physically present (detected via a pull-up/pull-down sense on an unused extension pin). This ensures the card never attempts 16-bit signaling when installed in an 8-bit slot.

#### 2.4.3 IOCHRDY Wait State Management

IOCHRDY is critical for reliable operation across all CPU speeds. When the host CPU initiates an I/O read and the CPLD does not yet have data ready from the ESP32-S3, the CPLD pulls IOCHRDY low to insert wait states. This pauses the CPU until the data is ready.

**Hard constraint:** IOCHRDY must not be held low for more than approximately 15.6 us (the DRAM refresh interval on most systems). Holding it longer risks DRAM data loss. The CPLD implements a watchdog counter that releases IOCHRDY and returns a timeout status byte if the ESP32-S3 does not respond within 10 us. The TSR detects this condition and retries.

On 8088 systems, each wait state adds one clock cycle (~210ns at 4.77 MHz). On faster 386/486 systems, wait states are shorter but more of them may be inserted. The CPLD's response time is constant regardless of host CPU speed because it is clocked by the ISA bus's own BCLK signal.

### 2.5 16-bit Transfer Details

The card asserts IOCS16# when installed in a 16-bit slot, enabling word-width data transfers on bulk read/write ports (0x04-0x05). When installed in an 8-bit slot, IOCS16# is not connected; the bus controller automatically performs two 8-bit cycles. The CPLD handles both paths transparently: in 16-bit mode, it latches D0-D15 simultaneously; in 8-bit mode, it latches D0-D7 per cycle and assembles/disassembles words internally.

### 2.6 I/O Port Register Map

| Offset | Read | Write | Width |
|--------|------|-------|-------|
| 0x00 | Status Register | Command Register | 8-bit |
| 0x01 | Response Length (low) | Command Length (low) | 8-bit |
| 0x02 | Response Length (high) | Command Length (high) | 8-bit |
| 0x03 | Reserved | Reserved | 8-bit |
| 0x04-0x05 | Data Out (bulk read) | Data In (bulk write) | 8/16-bit* |
| 0x06 | Error Code | Error Clear | 8-bit |
| 0x07 | Firmware Version (major) | Reset | 8-bit |
| 0x08 | Firmware Version (minor) | Reserved | 8-bit |
| 0x09 | Firmware Version (patch) | Reserved | 8-bit |
| 0x0A | Session Count (active) | Reserved | 8-bit |
| 0x0B | Session Capacity (max) | Reserved | 8-bit |
| 0x0C | Network Status | Reserved | 8-bit |
| 0x0D | Signal Quality (WiFi) | Reserved | 8-bit |
| 0x0E-0x0F | Reserved | Reserved | 8-bit |

*Ports 0x04-0x05 support 16-bit transfers when IOCS16# is asserted (card in 16-bit slot). In an 8-bit slot, only port 0x04 is used for data, one byte at a time.

**Status Register (0x00 read) bit map (authoritative definition):**

| Bit | Name | Set By | Cleared By | Description |
|-----|------|--------|-----------|-------------|
| 0 | CMD_READY | ESP32 (via cache push) | ESP32 (while processing) | Card is ready to accept a new command |
| 1 | RESP_READY | ESP32 (when response complete) | Host (reading response) | Response data is available for reading |
| 2 | ASYNC_DATA | ESP32 (incoming session data) | Host (reading events via 06/00) | Asynchronous event pending |
| 3 | SAFE_MODE | CPLD (from J3 jumper) | Hardware only | Safe mode jumper is installed |
| 4 | Reserved | | | Always 0 in v1 |
| 5 | XFER_TIMEOUT | CPLD (on IOCHRDY watchdog expiry) | Host (write 0x20 to cmd reg) | A bulk transfer byte timed out; block is invalid |
| 6 | BOOT_COMPLETE | CPLD (when PBOOT asserted by ESP32) | Reset only | ESP32 firmware has completed initialization |
| 7 | Reserved | | | Always 0 in v1 (was ESP32_ALIVE in early drafts, removed) |

Bits 0-2 are managed by the ESP32 and pushed to the CPLD cache. Bits 3, 5, 6 are managed by the CPLD hardware. This register is cached in the CPLD and reads with zero wait states.

### 2.7 Data Flow Model

Communication between host and card uses a command/response mailbox model:

1. Host checks Status Register bit 0 (CMD_READY). If set, card is ready for a command.
2. Host writes command length to ports 0x01-0x02.
3. Host writes command payload to Data In port (0x04), byte or word at a time.
4. Host writes command opcode to Command Register (0x00). This triggers execution.
5. Card asserts IRQ when response is ready (Status Register bit 1 = RESP_READY).
6. Host reads response length from ports 0x01-0x02.
7. Host reads response payload from Data Out port (0x04).
8. Host reads status/error from ports 0x00/0x06.

For asynchronous events (incoming data on an open session), the card asserts IRQ and sets Status Register bit 2 (ASYNC_DATA). The host can poll for this or handle it in an ISR.

### 2.8 Main Processor

**Reference implementation:** ESP32-S3-WROOM-1U-N8R8 (Espressif)

Rationale:
- Dual-core Xtensa LX7 at 240 MHz (massive headroom for TLS)
- Hardware accelerators: AES-128/256 (7.3 MB/s), SHA-256/384/512 (26-29 MB/s), RSA up to 4096-bit (118ms for RSA-2048 handshake), ECC (secp256r1/384r1/521r1), HMAC, Digital Signature, hardware RNG
- Built-in WiFi 802.11 b/g/n
- 512KB SRAM + 8MB OPI PSRAM, 16MB flash
- Mature TLS 1.3 support via mbedTLS 3.x in ESP-IDF, all crypto operations hardware-accelerated
- U.FL antenna connector (essential for ISA card inside metal PC case; external antenna on bracket)
- Large community, excellent documentation
- ~$6-7 in single quantity (Mouser/DigiKey)
- 3.3V native; ISA bus voltage bridging handled by the ATF1508AS CPLD

### 2.9 Ethernet (Optional)

**Part:** WIZnet W5500 (SPI interface to ESP32-S3)

- Hardware TCP/IP offload with 8 simultaneous sockets, 32KB buffer
- 10/100 Ethernet MAC+PHY integrated
- SPI up to 80 MHz; ESP-IDF native support with TCP Offload Engine (34.8 Mbps throughput on ESP32-S3)
- ~$1.80 (LCSC) to $3.50 (DigiKey/Mouser)
- RJ45 MagJack with integrated magnetics on bracket
- 25 MHz crystal required
- PCB footprint populated in v1.5; v1 ships WiFi-only with pads, connector footprint, and crystal footprint present

Note: The ESP32-S3 does NOT have an RMII interface or built-in Ethernet MAC. LAN8720 PHY is not an option. All Ethernet must go through SPI via W5500.

**TLS and W5500 TCP offload:** The W5500 provides hardware TCP/IP with 8 sockets, but mbedTLS requires raw socket access to manage the TLS state machine (it must read and write individual TLS records, handle retransmission, and manage the handshake protocol). For TLS sessions, the ESP32 firmware will operate the W5500 in **MACRAW mode**, bypassing its hardware TCP and running lwIP on the ESP32 instead. The W5500's hardware TCP sockets remain available for non-TLS use cases (e.g., plaintext sessions via INT API function 03/07), but TLS traffic always goes through lwIP. This is a known architectural tradeoff, not a bug.

### 2.10 Level Shifting

The ATF1508AS CPLD operates at native 5V, directly interfacing with the ISA bus without separate level shifters on the bus side. The CPLD communicates with the ESP32-S3 via a dedicated parallel bus with separate data pins (see Section 5.3).

The default BOM includes an SN74LVC8T245 dual-supply translating transceiver on the parallel bus between the CPLD's ESP32-facing pins and the ESP32-S3's GPIO. The A-side operates at 5V (CPLD domain), the B-side at 3.3V (ESP32 domain), providing clean, fast voltage translation at up to 420 Mbps with deterministic timing. This is standard equipment, not a fallback. A 74HCT245 bus buffer is also included on the ISA data bus for robust drive on heavily loaded backplanes.

**Buffer /OE pull-ups (mandatory):** All bus buffer and level shifter /OE (output enable) pins must have 10K pull-up resistors to their respective VCC rails. When the CPLD is unprogrammed, being JTAG-programmed, or in an undefined state after power-on, all CPLD outputs float. Without pull-ups, the buffer direction and enable pins are indeterminate, potentially allowing the buffer to drive the ISA bus and ESP32 GPIO simultaneously in random directions. The pull-ups hold all buffers in the disabled (high-Z) state until the CPLD actively drives /OE low during normal operation. This costs $0.05 in resistors and prevents bus damage during development.

**Logic family selection:** Use **74HCT** series exclusively for ISA bus-facing logic. Do NOT substitute 74ALS (rise times ~2ns cause ringing on older motherboard buses; XT-IDE documented boot failures on IBM 5150) or 74F (similar fast-edge issues). 74LS is acceptable but slower. 74HCT provides the correct voltage thresholds (TTL-compatible inputs, CMOS outputs) and moderate edge rates that work across all ISA-era systems.

### 2.11 External Storage and Credential Management

**MicroSD card slot** accessible from the bracket. Contains:

- Root CA certificate bundle (PEM format, updatable by user)
- Firmware update image (card checks on boot)
- Optional: session logs, diagnostic dumps
- Optional: WiFi configuration file (see below)

**WiFi credential storage:** WiFi SSID and password are stored in the ESP32-S3's NVS (Non-Volatile Storage) partition in flash by default. This means the MicroSD card can be removed without losing WiFi connectivity, and credentials are not exposed on removable media. The CISAWIFI.EXE utility writes credentials to NVS via the INT API (Group 0x01).

A MicroSD-based configuration file (wifi.json) is supported as an alternative for initial provisioning or headless setups. If wifi.json is present on the MicroSD at boot, the ESP32 reads it and copies credentials to NVS, then optionally deletes the file from the SD card (configurable behavior). This allows provisioning by inserting a pre-configured SD card and rebooting.

**Security note:** Credentials in NVS are stored in the ESP32-S3's internal flash, which is not removable. They are not encrypted (the ESP32-S3's flash encryption feature could be enabled but adds complexity and is deferred to v2). Physical access to the card's JTAG port could theoretically extract NVS contents. For the target audience (hobbyist retro computing), this is an acceptable risk.

### 2.12 Status LEDs (Bracket-Mounted)

| LED | Color | Meaning |
|-----|-------|---------|
| PWR | Green | Card powered and CPLD running (driven by CPLD, independent of ESP32) |
| NET | Blue | Network connected (solid) / connecting (blink) / no network (off) |
| TLS | Amber | TLS session active (solid) / handshake in progress (blink) |
| ERR | Red | ESP32 firmware crash detected (solid) / error condition (blink) / normal (off) |

### 2.13 Power Integrity, Noise, and EMC

This card combines four historically incompatible things on a single PCB: a 5V TTL bus from 1981, a 500 KHz switching regulator, a 240 MHz microcontroller, and a 2.4 GHz WiFi radio transmitting at 100mW. All powered by a PSU that might be 40 years old. Power integrity and EMC must be designed in from the start, not debugged after fabrication.

#### 2.13.1 ISA Bus Power Quality

The ISA bus +5V rail is the card's sole power source. Its quality varies enormously across the installed base:

| PSU Era | Typical +5V Ripple | Voltage Range | Regulation |
|---------|-------------------|---------------|------------|
| IBM 5150/5160 (1981-83) | 50-100mV | 4.75-5.25V | Decent (original IBM PSU) |
| XT clones (1984-88) | 100-500mV | 4.5-5.5V | Highly variable. Some clone PSUs are terrible. |
| AT/286 (1984-90) | 30-100mV | 4.85-5.15V | Generally good (switching regulators matured) |
| 386/486 tower (1990-97) | 20-50mV | 4.9-5.1V | Good. Higher wattage, better filtering. |
| Aged PSU (any era, 2026) | 200mV-1V+ | 4.2-5.8V | Degraded. Old electrolytics dry out, ESR rises, filtering collapses. |

**The worst case is a 1986 XT clone with a no-name PSU whose capacitors have dried out over 40 years.** The +5V rail on such a system can have 500mV+ ripple, sag to 4.5V under load, and spike to 5.5V during load transients. Every component on the card must tolerate this.

**Design requirements:**

- The TPS563200 DC-DC converter accepts 4.5V-17V input, so it handles the full range of degraded ISA PSUs. At 4.5V input with 3.3V output, the converter is operating at minimum headroom but within specification.
- A 470uF low-ESR electrolytic capacitor at the ISA +5V input provides local energy storage, decoupling the card from bus-level voltage sags during WiFi TX bursts. This is the single most important passive component on the board.
- A 100uF electrolytic at the 3.3V DC-DC output provides additional buffering.
- A TVS diode (SMBJ5.0A or similar, ~$0.20) across the +5V input clamps voltage spikes above 6.4V. This protects against PSU transients during power-on, hot-insertion of other ISA cards, or load dumps when other cards are removed.

#### 2.13.2 WiFi TX Current Transients

The ESP32-S3 draws 80-100mA idle, but WiFi TX bursts pull 350-500mA at 3.3V for approximately 1-4ms per packet. These transients propagate backward through the DC-DC converter and appear on the ISA +5V rail as current spikes, potentially disturbing other ISA cards and the motherboard.

**Mitigations:**

- The 470uF input electrolytic absorbs the transient current locally on-card. The ISA bus sees a relatively constant ~200mA average draw rather than 100-500mA pulses.
- The DC-DC converter's control loop responds to load transients within ~50us (TPS563200 transient response). During the ~50us response time, the output capacitors (100uF electrolytic + 2x22uF ceramic) supply the burst current.
- An additional 47uF ceramic (X5R, 6.3V) placed within 5mm of the ESP32-S3's VDD3P3 power pins provides the first ~100ns of surge current directly to the module, before the bulk capacitors respond.
- The ferrite bead on the ESP32-S3's analog VDD3P3 supply (specified in Section 2.13.6, Decoupling Strategy) isolates the RF power amplifier's switching noise from the digital 3.3V rail.

**Validation test:** With a current probe on the ISA +5V input, verify that the card's current draw during WiFi TX bursts never exceeds 500mA instantaneous and that the +5V rail does not sag more than 200mV during bursts. Test on the weakest PSU available (typically an XT clone).

#### 2.13.3 Switching Regulator Noise

The TPS563200 operates at approximately 560 KHz switching frequency. This generates conducted and radiated emissions at 560 KHz and its harmonics (1.12 MHz, 1.68 MHz, 2.24 MHz, etc.). While these frequencies are well below the ISA bus's operating frequencies (4.77-12 MHz), they can couple into analog signals and create audible interference on sound cards in adjacent slots.

**Mitigations:**

- Use a shielded inductor (Wurth 744043004.7 or equivalent with integrated magnetic shield) to contain radiated emissions from the inductor, which is the primary EMI source.
- Place input capacitors (2x10uF ceramic + 470uF electrolytic) as close as physically possible to the TPS563200's VIN pin. Short, wide traces minimize parasitic inductance in the input current loop.
- Route the switching node (SW pin to inductor to output capacitors) as a compact polygon pour, not a long trace. This is the highest-dI/dt net on the board and the primary source of radiated emissions.
- Use a continuous ground plane on the PCB inner layers. Do not break the ground plane under the switching regulator. The return currents must have an uninterrupted path.
- Place the switching regulator on the opposite end of the PCB from the ISA edge connector, maximizing physical distance between the noise source and the bus interface.

**Sound card interference test:** Install the NetISA card adjacent to a Sound Blaster or AdLib card. Play audio while the NetISA card performs WiFi TX. Listen for buzzing, whining, or clicking that correlates with network activity. If present, add a second-stage LC filter (100uH inductor + 100uF capacitor) between the ISA +5V input and the TPS563200's VIN.

#### 2.13.4 RF Emissions and ISA Bus Coupling

The ESP32-S3's WiFi radio transmits at 2.4 GHz with up to 20 dBm (100mW) output power. The ISA bus edge connector and its backplane traces are physically large enough to act as inadvertent antennas at this frequency (a quarter-wave antenna at 2.4 GHz is 31mm, which is shorter than most ISA bus traces).

**Risk:** WiFi RF energy could couple into ISA bus data or address lines, causing bit errors on the bus. This is unlikely at the ISA bus's operating frequencies (the bus signals are DC to ~20 MHz, while WiFi is at 2.4 GHz, a 100x frequency separation), but harmonic interactions and demodulation effects in TTL gate inputs are possible.

**Mitigations:**

- The U.FL antenna connector routes the RF energy to an external antenna mounted on the bracket, away from the PCB and ISA bus traces. The on-card RF path between the ESP32-S3's antenna pad and the U.FL connector should be as short as possible (under 20mm), with a proper 50-ohm microstrip impedance-matched trace on the PCB.
- Place the ESP32-S3 module on the opposite end of the PCB from the ISA edge connector. The ISA bus signals are at one end; the RF section is at the other, with the DC-DC converter and CPLD forming a physical buffer zone between them.
- A ground plane under the entire PCB provides an RF shield between the component side (with the ESP32) and the solder side (which faces into the slot cavity).
- If testing reveals RF coupling into ISA bus signals, add 47pF capacitors in series with the ISA data lines between the CPLD and the edge connector. These form low-pass filters with the bus impedance, attenuating 2.4 GHz signals while passing ISA bus frequencies (DC to ~20 MHz) unaffected.

#### 2.13.5 Ground Integrity

**Use a single, continuous ground plane. Do not split it.** This is the most common PCB design mistake in mixed-signal boards. The ground plane provides the return path for all signals (digital, analog, and RF). Splitting it forces return currents to detour around the split, creating inductance and antenna loops.

On a 4-layer board, the recommended stackup is:

```
Layer 1 (Top):      Signal + components (ISA bus traces, CPLD, ESP32, passives)
Layer 2 (Inner 1):  Continuous ground plane (unbroken, no cuts, no gaps)
Layer 3 (Inner 2):  Power plane (split: 5V region near ISA connector, 3.3V region near ESP32)
Layer 4 (Bottom):   Signal + components (parallel bus traces, MicroSD, LEDs)
```

The ground plane on Layer 2 must have no slots, cuts, or gaps under any signal trace. Every IC has at least one ground via connecting to this plane within 3mm of its ground pin.

#### 2.13.6 Decoupling Strategy (Revised)

| Component | Decoupling | Placement | Purpose |
|-----------|-----------|-----------|---------|
| ISA +5V input | 470uF electrolytic + TVS diode | Within 15mm of edge connector power pins | Bulk energy storage, transient clamping |

**Redundant power pin connections:** The ISA edge connector provides multiple +5V and GND pins. The PCB layout MUST connect to ALL available +5V and GND pins on the edge connector, not just one pair. 40-year-old ISA slot fingers are often heavily oxidized, and contact resistance on a single pin can be significantly higher than specification. During 500mA WiFi TX transients, this contact resistance causes localized voltage droop at the connector before current reaches the 470uF bulk capacitor. Using redundant pins reduces total contact resistance by paralleling multiple contacts. All +5V pins should be tied together with wide traces (40mil minimum) and vias immediately at the connector edge. Same for GND.
| TPS563200 input | 2x10uF X5R ceramic | Within 5mm of VIN pin | Switching converter input filtering |
| TPS563200 output | 2x22uF X5R ceramic + 100uF electrolytic | Within 5mm of VOUT pin | Output ripple filtering, load transient response |
| ESP32-S3 VDD3P3 (digital) | 100nF X7R ceramic | Within 3mm of each VDD pin | High-frequency bypass |
| ESP32-S3 VDD3P3 (analog) | 100nF X7R + ferrite bead from digital 3.3V | Within 3mm of analog VDD pin | RF PA isolation from digital noise |
| ESP32-S3 surge reserve | 47uF X5R ceramic (6.3V) | Within 5mm of module | WiFi TX burst current, first 100ns response |
| ATF1508AS VCC | 100nF X7R ceramic | Within 3mm of each VCC pin | High-frequency bypass |
| W5500 (when populated) | 100nF X7R + 10uF X5R | Within 3mm of power pins | Standard Ethernet controller decoupling |
| 74HCT245 | 100nF X7R ceramic | Within 5mm of VCC pin | Bus transceiver bypass |
| SN74LVC8T245 | 100nF X7R on both VCCA and VCCB | Within 3mm of each supply pin | Dual-supply level shifter bypass |

Total decoupling capacitor count: approximately 18-22 capacitors. This is normal for a mixed-signal board with RF.

#### 2.13.7 Power-On Sequencing and Brownout

When the PC powers on, the ISA +5V rail ramps from 0V to 5V over approximately 10-50ms (PSU-dependent). The TPS563200 has a built-in soft-start that ramps the 3.3V output over ~2ms, preventing inrush current spikes. The ESP32-S3 has its own internal power-on reset (POR) that holds the chip in reset until VDD3P3 is stable above 2.7V.

**Brownout scenario:** If the ISA +5V rail sags below 4.5V (possible on a loaded XT system when a floppy drive spins up), the TPS563200 may lose regulation and the 3.3V output will droop. The ESP32-S3's brownout detector (configurable in ESP-IDF) triggers a controlled reset if VDD3P3 drops below 3.0V. After the brownout condition clears, the ESP32 reboots, re-asserts PBOOT after initialization, and the CPLD updates the Status Register accordingly.

**The ISA bus RESET DRV signal** is active during system power-on and reset. The CPLD uses this to initialize its own state and hold the ESP32-S3's EN pin low until RESET DRV deasserts, ensuring the ESP32 does not attempt to boot during an unstable power period.

#### 2.13.8 EMC Testing Recommendations

Before community release, the card should undergo informal EMC testing:

- **Conducted emissions:** Measure ripple current on the ISA +5V rail with a current probe during WiFi TX. Target: under 100mA peak-to-peak ripple reflected back to the bus.
- **Adjacent slot interference:** Test with cards in adjacent ISA slots: Sound Blaster (audio), VGA card (video), NE2000 (network). Verify no visual artifacts, audio noise, or network errors correlate with NetISA WiFi activity.
- **Immunity:** Test NetISA card operation while another ISA card generates noise (e.g., Sound Blaster playing audio with DMA). Verify no data corruption on the parallel bus or ISA I/O ports.
- **WiFi performance inside cases:** Measure WiFi RSSI (signal strength) with the card installed in at least 3 different PC cases. Document minimum acceptable RSSI for reliable TLS handshakes.

Formal FCC testing is covered by the ESP32-S3-WROOM-1U module's pre-certification (FCC ID: 2AC7Z-ESPS3WROOM1). The card itself only requires an SDoC for unintentional emissions, which is straightforward for a digital device operating below 1 GHz on the bus side.

#### 2.13.9 Fail-Safe Electrical States

During power-up, reset, CPLD programming, ESP32 boot, and brownout, no signal should be in an undefined state that could damage hardware or drive the ISA bus. This section defines the guaranteed electrical state of every external signal during each hazardous condition.

**CPLD unprogrammed or being JTAG-programmed:**

| Signal | State | Guaranteed by |
|--------|-------|---------------|
| D0-D7 (ISA data) | High-Z | 74HCT245 /OE pulled HIGH by 10K pull-up |
| IOCHRDY | Not driven (HIGH via motherboard pull-up) | CPLD output floating, ISA bus has internal pull-up |
| IRQ_OUT | Not driven | CPLD output floating, no external pull |
| IOCS16# | Not driven | CPLD output floating |
| PD0-PD7 (ESP32 data) | High-Z | SN74LVC8T245 /OE pulled HIGH by 10K pull-up |
| PA0-PA3, PRW, PSTROBE | Floating | ESP32 GPIO has internal pull-downs (configurable). PSTROBE idle HIGH (active-low) requires 10K pull-up on PSTROBE. |

**System reset active (RESET DRV asserted):**

| Signal | State | Guaranteed by |
|--------|-------|---------------|
| D0-D7 | High-Z | CPLD resets: chip_sel = 0, OE disabled |
| IOCHRDY | HIGH (not driven) | iochrdy_hold.ar = RESET clears hold |
| IRQ_OUT | LOW (not driven) | irq_state.ar = RESET clears to IDLE |
| PD0-PD7 | High-Z | pd_drive = 0 when chip_sel = 0 |
| PSTROBE | HIGH (idle) | strobe_req.ar = RESET clears strobe |
| ESP32 EN | LOW (held in reset) | RESET DRV drives ESP32 EN low via RC filter |

**ESP32 booting (CPLD running, ESP32 firmware not yet initialized):**

| Signal | State | Guaranteed by |
|--------|-------|---------------|
| PREADY | LOW | ESP32 GPIO default state before firmware configures it |
| PIRQ | LOW | ESP32 GPIO default state |
| PBOOT | LOW | ESP32 GPIO default state; firmware asserts HIGH when ready |
| Status Register bit 6 | 0 (BOOT_COMPLETE not set) | pboot_sync remains low until ESP32 asserts PBOOT |

**ESP32 crash (firmware panic, watchdog reset):**

| Signal | State | Effect |
|--------|-------|--------|
| PREADY | LOW (GPIO resets) | All non-cached reads timeout via IOCHRDY watchdog. TSR detects N consecutive timeouts, reports "Card firmware unresponsive" |
| PIRQ | LOW (GPIO resets) | No spurious interrupts. IRQ state machine stays in IDLE |
| PBOOT | LOW (GPIO resets) | Status Register bit 6 clears on next ESP32 reboot cycle |

**Action item for PCB layout:** Add 10K pull-up on PSTROBE line (ESP32-to-CPLD direction) to ensure idle-HIGH during CPLD programming. All other signals are safe via the /OE pull-ups on bus buffers or via motherboard-side pull-ups on ISA control lines.

---

## 3. Cross-Era Compatibility

NetISA must work reliably across four generations of PC hardware spanning 15 years of evolution (1981-1995). This section documents the specific technical challenges and design decisions that ensure compatibility from an original IBM PC 5150 through a late-model 486 DX4.

### 3.1 Machine Classes and Their Constraints

| Era | CPU | Bus Speed | ISA Width | PIC | DRAM Refresh | Typical PSU +5V |
|-----|-----|-----------|-----------|-----|-------------|-----------------|
| XT (1981-86) | 8088/8086 | 4.77 MHz | 8-bit only | Single 8259A (IRQ 0-7) | ~15.6 us | 2A shared |
| AT (1984-90) | 80286 | 6-8 MHz | 8 or 16-bit | Dual 8259A (IRQ 0-15) | ~15.6 us | 5-10A |
| 386 (1986-94) | 80386 SX/DX | 8 MHz (ISA decoupled) | 8 or 16-bit | Dual 8259A | ~15.6 us | 10-15A |
| 486 (1989-97) | 80486 SX/DX/DX2/DX4 | 8 MHz (ISA decoupled) | 8 or 16-bit | Dual 8259A | ~15.6 us | 15-20A |

Starting with 386-class machines, the ISA bus is typically decoupled from the CPU clock via a bus controller chipset, running at a fixed 8 MHz regardless of CPU speed. This decoupling means ISA bus timing is more consistent on 386/486 systems than on XT/AT machines where the bus clock derives directly from the CPU crystal.

### 3.2 XT-Class Specific Requirements

The IBM PC/XT (5150/5160) and clones are the most constrained target. The card MUST handle:

**Single 8259A PIC (IRQ 0-7 only).** IRQ 9-11 do not exist. The card must function on IRQ 3, 5, or 7, all of which have potential conflicts. The TSR must also support a pure polling mode (/POLL) for systems where no IRQ is available. In polling mode, the TSR checks the card's status register on a timer tick (INT 08h hook) or before each API call.

**4.77 MHz bus speed.** I/O cycles are approximately 1.0-1.2 us. This is actually more relaxed timing than faster machines, giving the CPLD more time to respond. No special accommodation needed.

**No IOCS16# support.** The 62-pin XT bus connector does not carry IOCS16# or any 16-bit signals. The card must detect the absence of 16-bit slot signals and operate in 8-bit mode exclusively. The CPLD senses this via a pull-down resistor on the IOCS16# pin: if the pin reads low at power-on (no pull-up from the slot), the card locks to 8-bit mode.

**Limited conventional memory.** XT systems typically have 640KB or less. The TSR must be as small as possible (target: under 2KB resident). It must not require EMS, XMS, or any extended/expanded memory. It must load and run with only the base 640KB available, and it should be loadable high (via DEVICEHIGH or LOADHIGH) on systems that support it, but not require it.

**Slow CPU.** An 8088 at 4.77 MHz executes approximately 330,000 instructions per second. Application-level protocol parsing (HTTP headers, JSON, etc.) will be the bottleneck, not the card itself. The INT API must minimize overhead: no complex data structures that require extensive processing to interpret.

**No BIOS support for PnP or ACPI.** Configuration is purely via jumpers and the TSR command line.

### 3.3 AT-Class Specific Requirements

The IBM PC/AT (5170) introduced the 16-bit ISA bus extension and the dual PIC (IRQ 0-15).

**IRQ 2/9 cascade.** On AT-class machines, IRQ 2 on the first PIC is internally cascaded to IRQ 9 on the second PIC. Software that installs an ISR on IRQ 2 actually receives IRQ 9. The card's IRQ 9 jumper position accounts for this: the CPLD drives the physical IRQ 9 line, and the TSR installs its handler at the IRQ 2 vector (INT 0Ah) with proper cascade acknowledgment (EOI to both PICs).

**6-8 MHz bus speed.** I/O cycles are faster (~500-625ns). IOCHRDY wait states become important if the CPLD cannot respond within one cycle. The CPLD defaults to asserting IOCHRDY low for 2-3 wait states on every I/O read, ensuring data is valid regardless of bus speed.

**16-bit mode activation.** When the card detects a 16-bit slot (via the extension connector), it can assert IOCS16# to enable word-width transfers. This doubles bulk data throughput on ports 0x04-0x05.

### 3.4 386/486 Chipset Considerations

These machines introduced ISA bus controllers (chipsets) that decouple the ISA bus from the CPU clock. This creates a few specific challenges:

**ISA bus speed is chipset-dependent.** Most run at 8 MHz, but some chipsets (especially on 386 boards) allow the ISA bus speed to be set in BIOS setup: 8 MHz, 10 MHz, 12 MHz, or even "synchronous" with the CPU bus. Higher ISA speeds reduce I/O cycle time, making IOCHRDY wait state management even more critical. The CPLD's wait state logic must be tested at 12 MHz (the practical upper limit before most ISA cards fail).

**I/O recovery time.** Some 386/486 chipsets insert automatic recovery time (one or more bus clocks) between back-to-back I/O cycles to the same or adjacent addresses. This is configurable in some BIOSes. The card does not depend on back-to-back I/O timing and works correctly regardless of the I/O recovery setting.

**Shadow RAM and adapter segment conflicts.** Some BIOSes shadow the ROM BIOS and adapter ROMs into RAM in the C000-EFFF range for faster access. NetISA does not use memory-mapped I/O or ROM, so shadow RAM settings do not affect it. This is a deliberate design choice to avoid this entire category of compatibility problems.

**Cache controllers and write-back modes.** 486 systems with external or internal L1/L2 caches may have cache coherency issues with memory-mapped I/O devices. Again, NetISA avoids this by using only port-mapped I/O, which is inherently non-cacheable on x86.

### 3.5 Clone and Chipset Compatibility

Beyond IBM machines, the card must work with the vast ecosystem of clones and their diverse chipsets:

**Turbo XT clones (NEC V20, 8088-2 at 8-10 MHz).** These run the ISA bus faster than 4.77 MHz. The CPLD's timing is validated for bus speeds up to 12 MHz.

**NEAT/SCAT/SARC chipsets (286-era).** Some early AT chipsets have aggressive bus timing or incomplete IOCHRDY implementation. The card's IOCHRDY assertion is designed to be conservative (asserted early, released cleanly) to accommodate these chipsets.

**VLB (VESA Local Bus) and EISA machines.** These buses coexist with ISA slots on the same motherboard. NetISA uses only ISA bus signals and is unaffected by VLB or EISA presence. VLB and EISA slots have ISA-compatible pinouts in the base connector section.

**PS/2 Model 25/30 (8-bit ISA slots in PS/2 form factor).** These use standard 8-bit ISA electrically despite the PS/2 branding. NetISA works in these slots. PS/2 machines with MCA (Models 50-95) use a completely different bus and are NOT compatible.

### 3.6 Power Supply Aging and Reliability

Every machine that NetISA targets is at least 28 years old (the newest 486 systems were manufactured around 1997). The most common failure mode in aged PCs is power supply degradation:

**Electrolytic capacitor aging.** Electrolytic capacitors have a finite lifespan (typically 2,000-10,000 hours at rated temperature). After 30+ years, even caps rated for 10,000 hours at 85C have exceeded their useful life. The electrolyte dries out, ESR (Equivalent Series Resistance) rises, and filtering capacity drops. A PSU that produced clean 5V with 30mV ripple in 1990 may now produce 200-500mV ripple or worse.

**Voltage regulation drift.** Feedback resistor networks in old switching PSUs can drift, causing the +5V rail to sit at 4.7V or 5.3V rather than 5.0V. Linear PSUs in early XTs are even more susceptible to component drift.

**Tantalum capacitor failure.** Some vintage PSUs and motherboards used tantalum capacitors, which can fail short-circuit under voltage stress, potentially causing smoke or fire. This is a pre-existing condition in the machine, not caused by NetISA, but the additional current draw of the card could push a marginal tantalum cap over the edge.

**Design response:** NetISA is designed to operate on +5V supplies ranging from 4.5V to 5.5V with up to 500mV of ripple (see Section 2.13). The documentation should recommend that users of XT and early AT systems consider recapping their PSU before adding any modern expansion card. A brief PSU health check procedure (measure +5V under load with a multimeter; if it reads below 4.7V or above 5.3V, the PSU needs service) should be included in the quick-start guide.

### 3.7 Bus Loading and Electrical Considerations

**Drive strength.** The ISA bus was designed for TTL loading. Each slot presents one TTL load (1.6mA sink / 40uA source for LS-TTL). A fully populated 8-slot motherboard expects cards to drive up to 8 loads. The ATF1508AS's output drivers can sink 24mA / source 4mA per pin, sufficient for heavily loaded buses. The 74HCT245 bus buffer (included in the default BOM) provides additional drive strength on the ISA data bus for worst-case backplane loading.

**Rise/fall time.** ISA bus signals should have rise/fall times under 20ns to avoid crosstalk and ringing on the bus backplane. The ATF1508AS's 10ns propagation delay and CMOS output drivers produce clean edges. If ringing is observed on long backplanes, 33-ohm series resistors on data lines provide damping.

**Decoupling and power integrity.** See Section 2.13 for the complete decoupling strategy, power filtering, and EMC design. The card uses a 4-layer PCB with a continuous ground plane, a 470uF bulk input capacitor, a TVS diode for overvoltage clamping, and a shielded inductor switching regulator positioned away from the ISA edge connector.

### 3.8 Tested Configuration Matrix

Before v1 release, the card must be validated on representative hardware from each era. Minimum test matrix:

| Machine | CPU | Bus | Test Focus |
|---------|-----|-----|-----------|
| IBM 5150 or XT clone | 8088 @ 4.77 MHz | 8-bit ISA | Baseline XT compatibility, polling mode, IRQ 5 |
| Turbo XT clone | 8088-2 or V20 @ 8-10 MHz | 8-bit ISA | Fast XT timing |
| IBM 5170 or AT clone | 286 @ 8-12 MHz | 16-bit ISA | 16-bit transfers, IRQ 9 |
| 386SX system | 386SX @ 16-25 MHz | 16-bit ISA | Decoupled bus, chipset-specific IOCHRDY |
| 386DX system | 386DX @ 25-40 MHz | 16-bit ISA | Higher-speed bus controller |
| 486 system | 486DX2/DX4 @ 66-100 MHz | 16-bit ISA | Fast CPU, slow bus, wait state handling |
| VLB 486 system | 486 with VLB slots | 16-bit ISA (coexisting) | ISA slot adjacent to VLB, no interference |

Each test validates: card detection, IRQ delivery, data transfer integrity (loopback test), WiFi connection, TLS handshake, and sustained data transfer under load.

---

## 4. Software Interrupt (INT) API

### 4.1 Overview

NetISA installs a Terminate-and-Stay-Resident (TSR) program that hooks a software interrupt vector to provide the application programming interface. The TSR is small (under 2KB resident) and acts as a thin shim between DOS applications and the card's I/O ports.

**Interrupt Vector:** INT 63h (0x63). Fallback: INT 6Bh. Configurable via command-line parameter to TSR.

**Calling Convention:**

- AH = Function group
- AL = Function number
- Other registers as specified per function
- Returns: CF (carry flag) clear on success, set on error. AX = error code on failure.

### 4.2 Function Reference

#### Group 0x00: System

| AH | AL | Function | Input | Output |
|----|-----|----------|-------|--------|
| 00 | 00 | NOP / Presence Check | None | AX=4352h ('CR' for NetISA), BH=major ver, BL=minor ver, CH=patch ver |
| 00 | 01 | Get Card Status | None | AL=status flags, AH=active sessions, BL=max sessions |
| 00 | 02 | Reset Card | None | CF=0 on success |
| 00 | 03 | Get Network Status | None | AL=net status (0=disconnected, 1=connecting, 2=connected), AH=signal% |
| 00 | 04 | Get Error Detail | None | AX=last error code, DS:SI=pointer to error string (ASCIIZ) |
| 00 | 05 | Get Firmware Version | None | BH=major, BL=minor, CH=patch, DS:SI=pointer to build string |
| 00 | 06 | Get MAC Address | ES:DI=6-byte buffer | Buffer filled with MAC address |
| 00 | 07 | Get IP Address | ES:DI=4-byte buffer | Buffer filled with current IPv4 address |

#### Group 0x01: Network Configuration

| AH | AL | Function | Input | Output |
|----|-----|----------|-------|--------|
| 01 | 00 | Set WiFi SSID | DS:SI=ASCIIZ string | CF=0 on success |
| 01 | 01 | Set WiFi Password | DS:SI=ASCIIZ string | CF=0 on success |
| 01 | 02 | Connect WiFi | None | CF=0 on success (async; poll status) |
| 01 | 03 | Disconnect WiFi | None | CF=0 on success |
| 01 | 04 | Scan WiFi Networks | ES:DI=buffer, CX=buffer size | AX=networks found, buffer filled |
| 01 | 05 | Set Static IP | DS:SI=IP (4 bytes), ES:DI=subnet (4 bytes), BX:CX=gateway | CF=0 on success |
| 01 | 06 | Set DHCP | None (enables DHCP) | CF=0 on success |
| 01 | 07 | Set DNS Server | DS:SI=IP (4 bytes), AL=0 primary / 1 secondary | CF=0 on success |
| 01 | 08 | Save Config to SD | None | CF=0 on success |
| 01 | 09 | Load Config from SD | None | CF=0 on success |
| 01 | 10 | Select Interface | AL=0 WiFi / 1 Ethernet | CF=0 on success |

#### Group 0x02: DNS

| AH | AL | Function | Input | Output |
|----|-----|----------|-------|--------|
| 02 | 00 | Resolve Hostname | DS:SI=ASCIIZ hostname, ES:DI=4-byte buffer | Buffer filled with IPv4, CF=0 |
| 02 | 01 | Resolve Hostname (IPv6) | DS:SI=ASCIIZ hostname, ES:DI=16-byte buffer | Buffer filled with IPv6, CF=0 |
| 02 | 02 | Set DNS Mode | AL=0 plain / 1 DoT / 2 DoH | CF=0 on success |

#### Group 0x03: TLS Sessions

| AH | AL | Function | Input | Output |
|----|-----|----------|-------|--------|
| 03 | 00 | Open Session | DS:SI=ASCIIZ hostname, BX=port | AL=session handle (0-3 in v1, 0-7 reserved), CF=0 |
| 03 | 01 | Open Session (IP) | DS:SI=4-byte IPv4, BX=port, ES:DI=ASCIIZ SNI hostname | AL=session handle, CF=0 |
| 03 | 02 | Close Session | AL=session handle | CF=0 on success |
| 03 | 03 | Send Data | AL=session handle, DS:SI=buffer, CX=length | AX=bytes sent, CF=0 |
| 03 | 04 | Receive Data | AL=session handle, ES:DI=buffer, CX=buffer size | AX=bytes received, CF=0 |
| 03 | 05 | Get Session Status | AL=session handle | AH=state, BX=bytes available |
| 03 | 06 | Set Session Option | AL=handle, BL=option, CX=value | CF=0 on success |
| 03 | 07 | Open Plaintext Session | DS:SI=ASCIIZ hostname, BX=port | AL=session handle, CF=0 |

**Session States (AH from function 03/05):**

| Value | State |
|-------|-------|
| 0x00 | CLOSED |
| 0x01 | DNS_RESOLVING |
| 0x02 | TCP_CONNECTING |
| 0x03 | TLS_HANDSHAKE |
| 0x04 | ESTABLISHED |
| 0x05 | CLOSING |
| 0x06 | ERROR |

**Session Options (BL from function 03/06):**

| Value | Option | CX Value |
|-------|--------|----------|
| 0x00 | TLS version minimum | 0x0303=TLS1.2, 0x0304=TLS1.3 |
| 0x01 | Certificate validation | 0=strict, 1=warn, 2=skip |
| 0x02 | Receive timeout (ms) | 0-65535 |
| 0x03 | Send timeout (ms) | 0-65535 |
| 0x04 | SNI override | CX=0 disable, 1=use value from 03/01 |

#### Group 0x04: Certificate Management

| AH | AL | Function | Input | Output |
|----|-----|----------|-------|--------|
| 04 | 00 | Get Cert Store Info | None | AX=cert count, BX=store size (KB), CX=free space (KB) |
| 04 | 01 | Reload Certs from SD | None | CF=0, AX=certs loaded |
| 04 | 02 | Get Last Cert Error | AL=session handle | AX=cert error code, DS:SI=error description |
| 04 | 03 | Get Peer Cert Info | AL=session handle, ES:DI=buffer, CX=buf size | Buffer filled with cert subject/issuer/expiry |

#### Group 0x05: Raw Crypto (Coprocessor Mode)

These functions expose the ESP32-S3's hardware crypto accelerator directly, independent of any network session. No network connection is required. The card operates purely as a cryptographic coprocessor, analogous to how the 8087 accelerated floating-point math.

**Local acceleration use cases (no network required):**

- **File encryption/decryption.** Encrypt files on local storage using AES-256-GCM before writing to disk. A DOS utility could provide transparent file-level encryption for sensitive data on hard drives or floppies.
- **Secure file hashing.** Compute SHA-256 checksums of files for integrity verification, software distribution validation, or forensic purposes. A DOS equivalent of sha256sum.
- **Password hashing and key derivation.** Use HKDF-SHA256 to derive strong encryption keys from user passwords. Useful for building password managers or encrypted archives.
- **Digital signatures.** Sign files or messages with Ed25519 or RSA to prove authenticity. Verify signatures on downloaded software or received messages.
- **Cryptographically secure random number generation.** The ESP32-S3's hardware RNG provides true random bytes, far superior to any PRNG available in DOS. Useful for key generation, nonce creation, secure token generation, or even fair dice rolling in games.
- **Encrypted communications (non-TLS).** Applications can implement custom encrypted protocols over serial ports, IPX, or other transports using the raw AES or ChaCha20 primitives, without needing the full TLS stack.
- **PGP-like workflows.** Combine RSA key exchange with AES bulk encryption to implement public-key encrypted messaging between retro PCs, each with their own NetISA card.
- **Disk-level encryption helpers.** While full disk encryption is impractical on DOS (no block device filter driver), individual file or partition encryption tools can use the card for the heavy crypto lifting.
- **Software licensing and copy protection.** Developers of retro software could use Ed25519 signature verification to validate license keys, with the public key embedded in the application and signatures generated offline.

| AH | AL | Function | Input | Output |
|----|-----|----------|-------|--------|
| 05 | 00 | SHA-256 Hash | DS:SI=data, CX=length, ES:DI=32-byte output | CF=0 on success |
| 05 | 01 | SHA-384 Hash | DS:SI=data, CX=length, ES:DI=48-byte output | CF=0 on success |
| 05 | 02 | SHA-512 Hash | DS:SI=data, CX=length, ES:DI=64-byte output | CF=0 on success |
| 05 | 03 | AES-128-GCM Encrypt | DS:SI=params struct, ES:DI=output | CF=0 on success |
| 05 | 04 | AES-128-GCM Decrypt | DS:SI=params struct, ES:DI=output | CF=0 on success |
| 05 | 05 | AES-256-GCM Encrypt | DS:SI=params struct, ES:DI=output | CF=0 on success |
| 05 | 06 | AES-256-GCM Decrypt | DS:SI=params struct, ES:DI=output | CF=0 on success |
| 05 | 07 | ChaCha20-Poly1305 Encrypt | DS:SI=params struct, ES:DI=output | CF=0 on success |
| 05 | 08 | ChaCha20-Poly1305 Decrypt | DS:SI=params struct, ES:DI=output | CF=0 on success |
| 05 | 09 | X25519 Key Exchange | DS:SI=private key (32B), ES:DI=output public key | CF=0 |
| 05 | 0A | X25519 Shared Secret | DS:SI=private (32B), ES:DI=peer public (32B), BX:CX=output | CF=0 |
| 05 | 0B | Random Bytes | ES:DI=buffer, CX=count | Buffer filled with CSPRNG output |
| 05 | 0C | HMAC-SHA256 | DS:SI=params struct, ES:DI=32-byte output | CF=0 on success |
| 05 | 0D | HKDF-SHA256 | DS:SI=params struct, ES:DI=output | CF=0 on success |
| 05 | 0E | Ed25519 Sign | DS:SI=params struct, ES:DI=64-byte signature | CF=0 on success |
| 05 | 0F | Ed25519 Verify | DS:SI=params struct | CF=0 if valid, CF=1 if invalid |
| 05 | 10 | RSA-2048 Encrypt (OAEP) | DS:SI=params struct, ES:DI=output | CF=0 on success |
| 05 | 11 | RSA-2048 Decrypt (OAEP) | DS:SI=params struct, ES:DI=output | CF=0 on success |
| 05 | 12 | RSA-2048 Sign (PSS) | DS:SI=params struct, ES:DI=output | CF=0 on success |
| 05 | 13 | RSA-2048 Verify (PSS) | DS:SI=params struct | CF=0 if valid, CF=1 if invalid |
| 05 | 14 | RSA-4096 Encrypt (OAEP) | DS:SI=params struct, ES:DI=output | CF=0 on success |
| 05 | 15 | RSA-4096 Decrypt (OAEP) | DS:SI=params struct, ES:DI=output | CF=0 on success |
| 05 | 16 | RSA-4096 Sign (PSS) | DS:SI=params struct, ES:DI=output | CF=0 on success |
| 05 | 17 | RSA-4096 Verify (PSS) | DS:SI=params struct | CF=0 if valid, CF=1 if invalid |
| 05 | 18 | SHA-256 Init Streaming | None | AL=hash context handle |
| 05 | 19 | SHA-256 Update | AL=handle, DS:SI=data, CX=length | CF=0 on success |
| 05 | 1A | SHA-256 Final | AL=handle, ES:DI=32-byte output | CF=0, handle freed |

**AES Params Struct:**
```
Offset  Size  Field
0x00    4     Pointer to key (16 or 32 bytes)
0x04    4     Pointer to IV/nonce (12 bytes)
0x08    4     Pointer to input data
0x0C    2     Input data length
0x0E    4     Pointer to AAD (additional authenticated data)
0x12    2     AAD length
0x14    4     Pointer to auth tag (16 bytes, output for encrypt, input for decrypt)
```

All pointers are far pointers (segment:offset, 4 bytes). The ES:DI output parameter points to a host memory buffer that receives the encrypted/decrypted data.

**Output buffer sizing for crypto operations:**

| Operation | Output Size | Notes |
|-----------|------------|-------|
| AES-GCM Encrypt | Input length | Ciphertext. Auth tag written separately via tag pointer. |
| AES-GCM Decrypt | Input length | Plaintext. Auth tag read from tag pointer for verification. |
| ChaCha20-Poly1305 Encrypt | Input length | Ciphertext. Auth tag written separately. |
| ChaCha20-Poly1305 Decrypt | Input length | Plaintext. Auth tag verified internally. |
| RSA-2048 Encrypt (OAEP) | 256 bytes | Fixed output size regardless of input. |
| RSA-2048 Decrypt (OAEP) | Up to 190 bytes | Max plaintext for 2048-bit OAEP with SHA-256. AX returns actual length. |
| RSA-4096 Encrypt (OAEP) | 512 bytes | Fixed output size. |
| RSA-4096 Decrypt (OAEP) | Up to 446 bytes | AX returns actual length. |
| RSA Sign (PSS) | 256 or 512 bytes | Matches key size (2048=256B, 4096=512B). |
| Ed25519 Sign | 64 bytes | Fixed. |
| SHA-256/384/512 | 32/48/64 bytes | Fixed. |
| HMAC-SHA256 | 32 bytes | Fixed. |
| Random Bytes | CX bytes | Caller specifies. |

**Crypto data flow (how the TSR marshals data between host memory and the card):**

The card has no bus-master capability and cannot read host RAM directly. All data flows through the TSR via programmed I/O:

1. Host application calls INT API (e.g., SHA-256 Hash with DS:SI=data, CX=length, ES:DI=output).
2. TSR reads the far pointer at DS:SI, determining the data source in host memory.
3. TSR streams the input data from host memory to the card's Data In port (base+0x04), one byte/word at a time. For CX=4096 bytes on an 8088, this takes approximately 20ms.
4. Card's ESP32-S3 processes the data through its hardware crypto accelerator.
5. TSR reads the result from the card's Data Out port (base+0x04), streaming into the output buffer at ES:DI.
6. TSR returns to the caller with CF=0 and any result metadata in registers.

**Performance expectations (bus-limited, not crypto-limited):**

| System | Approx. Hash Throughput | Approx. AES Throughput |
|--------|------------------------|----------------------|
| 8088 @ 4.77 MHz | ~50-80 KB/s | ~50-80 KB/s |
| 286 @ 10 MHz | ~200-400 KB/s | ~200-400 KB/s |
| 386 @ 25 MHz | ~500 KB/s-1 MB/s | ~500 KB/s-1 MB/s |
| 486 @ 33 MHz | ~1-2 MB/s | ~1-2 MB/s |

The ESP32-S3's hardware accelerators operate at 7-29 MB/s. The ISA bus is always the bottleneck. The raw crypto API is most useful for operations on small data (key exchange, signature verification, hashing short messages, generating random bytes) where the bus transfer time is negligible.

**Maximum single-call data size:** Limited by CX register (16-bit, max 65,535 bytes) and practical ISA transfer time. For hashing files larger than 64KB, use the streaming hash API (05/18 Init, 05/19 Update, 05/1A Final) with 4-16KB chunks.

#### Group 0x06: Asynchronous Event Handling

| AH | AL | Function | Input | Output |
|----|-----|----------|-------|--------|
| 06 | 00 | Get Pending Events | ES:DI=event buffer, CX=buffer size | AX=event count |
| 06 | 01 | Set Callback Mode | AL=0 polling / 1 IRQ | CF=0 on success |
| 06 | 02 | Acknowledge Event | BX=event ID | CF=0 on success |

**Event Structure (8 bytes each):**
```
Offset  Size  Field
0x00    2     Event ID
0x02    1     Event Type
0x03    1     Session Handle (if applicable)
0x04    2     Data Length (bytes available)
0x06    2     Reserved
```

**Event Types:**

| Value | Event |
|-------|-------|
| 0x01 | Data available on session |
| 0x02 | Session disconnected by remote |
| 0x03 | Session error |
| 0x04 | Network status changed |
| 0x05 | Certificate warning |
| 0x06 | Firmware message (diagnostic) |

#### Group 0x07: Diagnostics, Testing, and Maintenance

| AH | AL | Function | Input | Output |
|----|-----|----------|-------|--------|
| 07 | 00 | Get Uptime | None | DX:AX = uptime in seconds |
| 07 | 01 | Get Memory Info | None | AX=free heap (bytes), BX=largest free block |
| 07 | 02 | Get Session Stats | AL=session handle | AX=bytes sent (KB), BX=bytes recv (KB) |
| 07 | 03 | Get Firmware Build | ES:DI=buffer, CX=size | Buffer filled with build info string |
| 07 | 04 | Start Firmware Update | None (reads from SD) | CF=0 if update file found |
| 07 | 05 | Get Supported Ciphers | ES:DI=buffer, CX=size | Buffer filled with cipher list |
| 07 | 06 | Bus Loopback Test | DS:SI=test pattern, CX=length, ES:DI=output | AX=mismatches (0=pass) |
| 07 | 07 | Network Echo Test | DS:SI=ASCIIZ hostname, BX=port | AX=round-trip ms, CF=0 |
| 07 | 08 | Set Trace Mode | AL=0 off / 1 bus events / 2 network / 3 all | CF=0 on success |
| 07 | 09 | Read Trace Buffer | ES:DI=buffer, CX=buffer size | AX=bytes read |
| 07 | 0A | Get Crashlog | ES:DI=buffer, CX=buffer size | AX=bytes read (0 if no crash) |
| 07 | 0B | Clear Crashlog | None | CF=0 on success |
| 07 | 0C | Set Runtime Mode | AL=0 normal / 1 reduced | CF=0 on success |
| 07 | 0D | Get Runtime Mode | None | AL=current mode |

**Layer isolation diagnostics (functions 07/06 through 07/07):**

These functions allow users and NISADIAG to isolate failures to specific layers:

- **Bus Loopback (07/06):** TSR sends a data pattern to the card via the parallel bus; the ESP32 echoes it back without any network involvement. Tests: ISA bus integrity, CPLD data path, parallel bridge, ESP32 GPIO interface. If this fails, the problem is in the card hardware, bus seating, or address conflict.

- **Network Echo (07/07):** Card opens a TCP connection to the specified host:port, sends a small payload, and measures the round-trip time. Tests: WiFi/Ethernet connectivity, DNS, TCP stack. Does not involve TLS. If bus loopback passes but network echo fails, the problem is in the network layer.

Combined with TLS session open (which tests TLS handshake and certificate validation), these three tests let a user systematically narrow any failure: bus -> network -> TLS -> application.

**Runtime reduced mode (07/0C):**

In addition to the hardware Safe Mode jumper (J3, requires opening the case), the firmware supports a software-accessible reduced mode that can be activated without physical access:

- **Normal mode (AL=0):** All features enabled. Up to 4 concurrent sessions. IRQ and async events active.
- **Reduced mode (AL=1):** Single session only. Async events disabled (polling only). Trace mode forced off. WiFi power reduced to minimum TX level. This mode is useful for debugging timing-sensitive issues, testing on marginal hardware, or isolating whether a problem is caused by concurrency or load.

Reduced mode persists until explicitly changed back to normal mode or until the card is reset. NISADIAG provides `/REDUCED` and `/NORMAL` switches.

**Trace mode (07/08 through 07/09):**

When enabled, the ESP32 logs events to an internal ring buffer (16KB in PSRAM). The host can read the trace buffer at any time via 07/09. Trace entries include timestamped records of ISA bus read/write events, parallel bus transactions, network socket events, TLS handshake steps, and error conditions. Trace output is human-readable ASCII text, one event per line.

Trace mode adds latency to all operations (~5-10% on 8088, negligible on 386+) and can subtly change timing behavior, masking or revealing bugs that don't appear in normal operation. **Trace mode is disabled by default in release firmware builds.** It must be explicitly enabled via NISADIAG `/TRACE` or INT API 07/08, and it resets to disabled on every card reboot. Trace mode is a debugging tool, not part of the normal software workflow. Applications must never depend on trace mode being enabled or disabled for correct operation.

### 4.3 Error Codes

| Code | Name | Description |
|------|------|-------------|
| 0x00 | SUCCESS | No error |
| 0x01 | ERR_NOT_READY | Card not initialized or busy |
| 0x02 | ERR_INVALID_HANDLE | Session handle out of range or not open |
| 0x03 | ERR_NO_SESSIONS | All session slots in use |
| 0x04 | ERR_DNS_FAILED | DNS resolution failed |
| 0x05 | ERR_CONNECT_FAILED | TCP connection failed |
| 0x06 | ERR_TLS_HANDSHAKE | TLS handshake failed |
| 0x07 | ERR_CERT_INVALID | Certificate validation failed |
| 0x08 | ERR_CERT_EXPIRED | Certificate has expired |
| 0x09 | ERR_CERT_HOSTNAME | Certificate hostname mismatch |
| 0x0A | ERR_TIMEOUT | Operation timed out |
| 0x0B | ERR_DISCONNECTED | Remote host closed connection |
| 0x0C | ERR_BUFFER_TOO_SMALL | Provided buffer is insufficient |
| 0x0D | ERR_NETWORK_DOWN | No network connectivity |
| 0x0E | ERR_WIFI_AUTH | WiFi authentication failed |
| 0x0F | ERR_INVALID_PARAM | Invalid parameter |
| 0x10 | ERR_SD_NOT_FOUND | MicroSD card not present |
| 0x11 | ERR_SD_READ | MicroSD read error |
| 0x12 | ERR_SD_WRITE | MicroSD write error |
| 0x13 | ERR_CRYPTO_FAILED | Cryptographic operation failed |
| 0x14 | ERR_NOT_IMPLEMENTED | Function defined but not yet implemented in firmware |

**Note on XFER_TIMEOUT:** Transfer timeout (IOCHRDY watchdog expiry during bulk reads) is signaled via Status Register bit 5, not via the error code mechanism. It is a CPLD hardware flag, not a firmware error code. See Section 5.3.5 for the bulk transfer error detection protocol. The TSR checks Status Register bit 5 after each block transfer and retries if set.
| 0xFF | ERR_UNKNOWN | Unexpected internal error |

### 4.4 Segment/Memory Considerations

All buffer pointers use real-mode segment:offset addressing (DS:SI, ES:DI). Maximum single transfer size is 64KB (one segment). For larger transfers, applications must issue multiple calls.

The TSR itself consumes under 2KB of conventional memory. It does not require EMS, XMS, or any memory manager. It is compatible with HIMEM.SYS and EMM386.EXE.

**TSR vs SDK responsibility split:** The 2KB TSR is deliberately minimal: interrupt vector, ISR (flag-setting only), port I/O wrappers, and presence check. All higher-level logic (buffer management, multi-byte command assembly, retries, timeout handling, convenience wrappers like `nisa_http_get()`) lives in NETISA.LIB, which is linked into each application at compile time. This follows the mTCP model: the library is compiled into each application, not centralized in a TSR. The TSR is the hardware driver. The library is the programming interface.

### 4.5 API Stability Tiers

The INT API surface is categorized into stability tiers. Tier assignments are part of the v1 release and cannot be changed without a major version bump.

**Tier 1: Frozen (v1 contract, never changes semantics)**

Applications can depend on these unconditionally. Breaking changes require a new major INT API version (which would use a different software interrupt vector).

| Group | Functions |
|-------|-----------|
| 0x00 | Presence check, card status, firmware version |
| 0x03 | Session open/close/send/recv/status |
| 0x06 | Event polling and acknowledge |

**Tier 2: Stable (unlikely to change, may gain new options)**

Semantics are stable but new option values or flags may be added in minor versions. Existing option values never change meaning.

| Group | Functions |
|-------|-----------|
| 0x01 | Network configuration (WiFi, Ethernet) |
| 0x02 | DNS resolution |
| 0x04 | Certificate management |
| 0x05 | Raw crypto (existing algorithms stable, new algorithms may be added) |
| 0x07 | Diagnostics (existing functions stable, new functions may be added) |

**Tier 3: Experimental (may change between minor versions)**

Applications using these functions must check firmware version and handle ERR_NOT_IMPLEMENTED gracefully.

| Group | Functions |
|-------|-----------|
| 0x08 | UDP (v2 reserved) |
| 0x09 | HTTP/WebSocket helpers (v2 reserved) |

**Version negotiation:** The TSR reads the card's firmware version at installation (Section 6.3) and refuses to load if the major version mismatches. Applications can query firmware version via INT API 00/02 and check for specific Tier 2/3 functions before calling them. Functions that don't exist in the current firmware return ERR_NOT_IMPLEMENTED (0x14) with CF set.

---

## 5. Firmware Architecture

### 5.1 Runtime Environment

**Framework:** ESP-IDF v5.x (Espressif IoT Development Framework)
**TLS Library:** mbedTLS 3.x (bundled with ESP-IDF, hardware-accelerated on ESP32-S3)
**TCP/IP Stack:** lwIP 2.x (bundled with ESP-IDF)
**RTOS:** FreeRTOS (bundled with ESP-IDF, dual-core SMP)

### 5.2 Major Firmware Components

| Component | Responsibility | RTOS Task | Core Affinity |
|-----------|---------------|-----------|---------------|
| ISA Bus Driver | Parallel bus interface to CPLD, command/response marshaling | isa_bus_task | Core 0 (dedicated) |
| Command Dispatcher | Parses command opcodes from host, routes to appropriate handler | Part of isa_bus_task | Core 0 |
| Session Manager | Maintains up to 4 concurrent TLS/TCP sessions (v1) with independent state machines. Handle range 0-7 reserved for v2 expansion. | session_mgr_task | Core 1 |
| Network Manager | WiFi/Ethernet connection management, DHCP, interface selection | net_mgr_task | Core 1 |
| DNS Resolver | Hostname resolution with optional DoT/DoH | Part of session_mgr_task | Core 1 |
| Certificate Store | Loads/validates CA bundle from MicroSD, caches in PSRAM | Init only, then shared | Either |
| Crypto Engine | Exposes hardware accelerator for raw crypto operations (Group 0x05) | crypto_task | Core 1 |
| Config Manager | Reads/writes configuration from MicroSD JSON file | Init only | Either |
| Event Queue | Buffers asynchronous events for host retrieval | Shared data structure | Both |
| OTA Updater | Validates and applies firmware images from MicroSD | ota_task (on demand) | Core 1 |

**Core allocation rationale:** Core 0 is dedicated to the ISA bus driver because it must respond to GPIO interrupts from the CPLD parallel bus with minimal latency. Core 1 handles all network and crypto operations, which are latency-tolerant relative to bus timing. FreeRTOS SMP ensures both cores are utilized.

### 5.3 CPLD-to-ESP32-S3 Communication Protocol

The ATF1508AS (or ATF1504AS with external latches, see Section 5.3.2) CPLD and the ESP32-S3 communicate via a parallel bus. The original design multiplexed the ISA data bus (D0-D7) with the ESP32 parallel data bus to save CPLD pins. **This design was rejected after adversarial review** because it creates the single nastiest failure mode possible: bus contention between ISA-side and ESP32-side drivers during direction transitions. Random bad bytes, occasional lockups, and chipset-specific behavior would result. The failure mode is not clean; it is intermittent and nearly undiagnosable.

**Rev A uses separate, dedicated data paths for ISA and ESP32.** This eliminates all bus contention risk at the cost of a slightly larger CPLD or one additional external transceiver.

#### 5.3.1 Recommended: ATF1508AS (128 macrocells, TQFP-100)

The strongest recommendation from adversarial review was: **"Rev A should optimize for electrical boringness, not elegance."**

**The ATF1508AS-10JU84 (PLCC-84) is obsolete.** DigiKey lists it as "no longer manufactured" (confirmed April 2026). The PLCC-84 package is available only as last-time-buy from remaining distributor stock and carries high counterfeit risk from marketplace sellers. All new design work targets the **ATF1508AS-10AU100 (TQFP-100)**, which is confirmed active with ongoing Microchip PCNs through 2025. DigiKey lists it at $16.13, Future Electronics has 180 units in stock.

The ATF1508AS in TQFP-100 provides 128 macrocells and 68 I/O pins. This gives dedicated pins for both ISA D0-D7 (8 pins) and ESP32 PD0-PD7 (8 pins) with no multiplexing, plus ample macrocells for proper synchronizers, a real IRQ state machine, and future features. The TQFP-100 has 4 more I/O pins than the obsolete PLCC-84 (68 vs 64), providing additional spare pins.

**Prototyping note:** The TQFP-100 is an SMD package (14x14mm, 0.5mm pitch) and is not sockettable. For Phase 0 prototyping, use a TQFP-100 breakout/adapter board to convert to breadboard-compatible pin headers. For the production PCB, the TQFP-100 is soldered directly.

**Pin budget (ATF1508AS TQFP-100, 68 I/O):**

| Group | Signals | Pins |
|-------|---------|------|
| ISA data | D0-D7 | 8 |
| ISA address | A0-A9 | 10 |
| ISA control | AEN, IOR#, IOW#, IOCHRDY, RESET DRV, IRQ_OUT, IOCS16# | 7 |
| ESP32 data | PD0-PD7 (dedicated, separate from ISA data) | 8 |
| ESP32 control | PA0-PA3, PRW, PSTROBE, PREADY, PIRQ, PBOOT | 9 |
| Config | DIP switch (3), safe mode (1), IRQ jumper sense (1), 16-bit slot sense (1) | 6 |
| **Total** | | **48** |

48 of 68 I/O pins used. 20 spare for test points, LEDs, or future expansion. No multiplexing anywhere. Every signal has a dedicated physical pin.

**Macrocell budget (ATF1508AS, 128 macrocells):**

| Block | Estimated | Notes |
|-------|-----------|-------|
| Address decode | 10 | Case table for 8 addresses |
| ISA data path (latches, OE) | 12 | Separate input and output latches |
| ESP32 data path (latches, OE) | 12 | Dedicated parallel data, no mux |
| IOCHRDY controller + watchdog | 14 | 8-bit counter + registered release + timeout byte preload |
| IRQ state machine | 8 | Pending/presented/acked with edge re-trigger |
| 2-flop synchronizers (x3) | 6 | PREADY, PIRQ, PBOOT |
| Parallel bridge control | 6 | Strobe generation, handshake |
| Cache hit/miss logic | 4 | Register address decode for cache |
| 16-bit ISA support | 4 | IOCS16# and word assembly |
| **Total** | **~76** | 52 spare out of 128 |

This is comfortable. Room for mistakes, workarounds, and features the fitter report reveals are needed.

#### 5.3.2 Alternative: ATF1504AS with External Latches

If board space or cost requires the smaller PLCC-44, keep the ATF1504AS (64 macrocells) and add two external 74HCT574 octal D flip-flops ($0.40 each, DIP-20):

- **Write latch (host-to-ESP32):** CPLD drives ISA data onto the 74HCT574 input. CPLD pulses the latch clock. ESP32 reads the latched output on its GPIO pins. No bus contention: the latch captures a snapshot and holds it.
- **Read latch (ESP32-to-host):** ESP32 drives data onto the 74HCT574 input. ESP32 pulses the latch clock. CPLD reads the latched output when needed for ISA read responses.

This eliminates the shared bus multiplex at the cost of 2 additional DIP-20 packages and ~20mm of board space.

#### 5.3.3 Physical Interface (ATF1508AS version)

| Signal | CPLD Pin | ESP32-S3 Pin | Direction | Purpose |
|--------|----------|-------------|-----------|---------|
| D0-D7 | 8 pins (dedicated) | N/A | ISA bus bidirectional | ISA data, directly driven by CPLD |
| PD0-PD7 | 8 pins (dedicated) | GPIO4-GPIO11 | ESP32 bidirectional | Parallel data, completely isolated from ISA bus |
| PA0-PA3 | 4 pins | GPIO12-GPIO15 | CPLD -> ESP32 | Register address |
| PRW | 1 pin | GPIO16 | CPLD -> ESP32 | Read/Write direction |
| PSTROBE | 1 pin | GPIO17 | CPLD -> ESP32 | Data strobe (registered, explicit dead-band) |
| PREADY | 1 pin | GPIO18 | ESP32 -> CPLD | Data valid (2-flop synchronized before use) |
| PIRQ | 1 pin | GPIO38 | ESP32 -> CPLD | Interrupt request (2-flop synchronized) |
| PBOOT | 1 pin | GPIO21 | ESP32 -> CPLD | Boot complete (2-flop synchronized) |

**GPIO19/GPIO20 reserved for USB:** On the ESP32-S3, GPIO19 and GPIO20 are the USB D-/D+ pins used by the built-in USB-JTAG interface. These pins MUST NOT be assigned to the parallel bus. The USB serial console is the primary debugging interface for all development phases. PIRQ is assigned to GPIO38 instead.

**Note: PBEAT (heartbeat) removed from rev A.** The CPLD-side heartbeat monitor was not silicon-ready (required too many counter bits for a meaningful timeout, or an impractically high toggle rate). Firmware liveness is detected by the host via status register polling: if PBOOT is asserted but the card stops responding to commands (watchdog timeout on every read), the host can issue a hardware reset. CPLD-side crash detection is deferred to v2 if community demand warrants it.

#### 5.3.4 Parallel Transaction: ISA Write to Card

```
1. Host CPU executes OUT port, data     (IOW# goes low on ISA bus)
2. CPLD latches D0-D7 into internal register (rising edge of IOW#)
3. CPLD copies latched data to ESP32-facing PD0-PD7 output register
4. CPLD drives PA0-PA3 with register address, PRW low (write)
5. CPLD asserts PSTROBE low (registered, synchronous to CLK_16MHZ)
6. ESP32 GPIO ISR fires on PSTROBE falling edge, reads PD0-PD7 and PA0-PA3
7. CPLD deasserts PSTROBE high (next CLK_16MHZ edge, minimum 62.5ns pulse)

No bus contention: D0-D7 (ISA side) and PD0-PD7 (ESP32 side) are separate pins.
No wait states needed for writes.
```

#### 5.3.5 Parallel Transaction: ISA Read from Card

```
1. Host CPU executes IN port              (IOR# goes low on ISA bus)
2. CPLD checks if register has cached data in output latch
   a. CACHE HIT: drive cached data onto D0-D7 immediately (~10ns). Done.
   b. CACHE MISS: assert IOCHRDY low (registered, synchronous to CLK_16MHZ)
3. CPLD drives PA0-PA3, PRW high, pulses PSTROBE (request data from ESP32)
4. ESP32 GPIO ISR fires, prepares data, drives PD0-PD7, asserts PREADY
5. CPLD's 2-flop synchronizer detects PREADY assertion (2 CLK_16MHZ cycles = 125ns)
6. CPLD latches PD0-PD7 into data_out_latch (registered capture, no metastability)
7. CPLD drives data_out_latch onto D0-D7 (ISA bus, separate pins)
8. CPLD releases IOCHRDY (registered state transition, NOT combinational)
9. Host CPU completes read cycle

WATCHDOG TIMEOUT (if ESP32 does not respond within 10us):
- At step 2b, a counter starts at 0 and increments every CLK_16MHZ cycle
- If counter reaches 160 (10us at 16 MHz) before PREADY_sync is seen:
  - CPLD sets bit 5 (XFER_TIMEOUT) in the cached Status Register
  - CPLD releases IOCHRDY (registered)
  - The data byte driven onto D0-D7 is UNDEFINED (last latch contents)
  - The host does NOT attempt to interpret this byte as valid data or as an error code

IMPORTANT: The CPLD never drives an error code onto the data port (0x04/0x05).
During bulk transfers, 0x01 is a valid data byte. Driving an error code onto the
data bus would silently corrupt the data stream with no way for the TSR to
distinguish error from data without checking status per byte (which would destroy
throughput). Instead, the CPLD sets a flag and the TSR checks once per block.
```

**Bulk transfer error detection protocol:**

1. TSR begins a block read: `REP INSB` for N bytes from port base+0x04
2. After the block completes, TSR reads Status Register (base+0x00, cached, zero wait states)
3. If bit 5 (XFER_TIMEOUT) is set: the block contains at least one corrupted byte. Discard the entire block and retry.
4. TSR clears XFER_TIMEOUT by writing 0x20 to the Command Register (base+0x00).
5. If bit 5 is clear: all bytes in the block are valid.

This design allows full-speed REP INSB/OUTSB bulk transfers with a single status check per block. The only cost is that a timeout invalidates an entire block (typically 256-4096 bytes) rather than a single byte. This is acceptable because IOCHRDY timeouts should be rare (indicating ESP32 firmware failure or severe overload), and retrying a 4KB block is far cheaper than checking status per byte.

**Cached registers (zero wait-state reads):**
- 0x00: Status Register (includes PBOOT flag, error flags)
- 0x06: Error Code
- 0x07-0x09: Firmware Version (cached at boot, never changes)
- 0x0A: Active Session Count
- 0x0B: Session Capacity
- 0x0C: Network Status

The cache is expanded aggressively per adversarial review recommendation. Only the data ports (0x04-0x05), response length (0x01-0x02), and signal quality (0x0D) require non-cached reads. This means the only time IOCHRDY wait states are inserted is during bulk data transfer and explicit status queries, both of which are latency-tolerant.

#### 5.3.6 Bulk Data Transfer

For streaming data through port 0x04 (Data In/Out), no bus multiplexing is needed. Writes go: ISA D0-D7 -> CPLD latch -> PD0-PD7 -> ESP32. Reads go: ESP32 -> PD0-PD7 -> CPLD latch -> ISA D0-D7. Each direction uses dedicated pins throughout. IOCHRDY may be asserted briefly on reads if the ESP32 has not pre-staged the next byte, but with a 4KB deep FIFO in ESP32 PSRAM and proactive pre-staging, most bulk reads complete without wait states on 286+ systems.

### 5.4 CPLD Logic Design (Rev A, Post Red-Team)

The CPLD design has been revised to address seven findings from adversarial review. The pseudocode in earlier drafts expressed behavior, not fitted product terms. **Rev A prioritizes electrical reliability over cleverness.** The actual CUPL/Verilog implementation will be validated by running the fitter and producing a pin report before any PCB layout begins (Phase 0 deliverable).

**Clock source:** 16 MHz external oscillator on GCLK1. ISA BCLK on GCLK2.

#### 5.4.1 Clock Domain Crossing (CDC) Safety

The CPLD has three clock/event domains:
1. ISA bus strobes (IOR#, IOW#) - asynchronous edges from the host CPU
2. CLK_16MHZ - the CPLD's registered logic clock
3. ESP32 GPIO signals (PREADY, PIRQ, PBOOT) - asynchronous to both

**Every signal crossing from the ESP32 domain into the CPLD domain passes through a 2-flop synchronizer before driving any state machine or output.** This costs 2 macrocells per signal (6 total for PREADY, PIRQ, PBOOT) but eliminates metastability on every asynchronous boundary.

```verilog
// 2-flop synchronizer for PREADY (same pattern for PIRQ, PBOOT)
reg pready_meta, pready_sync;
always @(posedge CLK_16MHZ) begin
    pready_meta <= PREADY;       // First flop: may be metastable
    pready_sync <= pready_meta;  // Second flop: stable, safe to use
end
// All logic uses pready_sync, NEVER raw PREADY
```

ISA bus strobes (IOR#, IOW#) are sampled on CLK_16MHZ edges for registered state transitions. Combinational chip_select and register decode use raw bus signals (standard ISA practice, synchronous to the bus clock domain).

#### 5.4.2 IRQ State Machine (Replaces Direct Mirror)

The original design directly mirrored PIRQ to the ISA IRQ line. Adversarial review correctly identified this as too naive for mixed edge/level PIC behavior. Rev A implements a proper state machine:

```
States: IDLE -> PENDING -> PRESENTED -> WAIT_DEASSERT -> IDLE

IDLE:
  pirq_sync rises -> latch event, transition to PENDING

PENDING:
  Assert ISA IRQ line high (rising edge for XT PICs)
  Transition to PRESENTED

PRESENTED:
  Host reads Status Register (irq_ack detected) ->
    Deassert ISA IRQ line
    Transition to WAIT_DEASSERT

WAIT_DEASSERT:
  Hold IRQ low for minimum 4 CLK_16MHZ cycles (250ns dead time)
  This guarantees edge-triggered PICs see a clean low-to-high
  transition if another event arrives immediately
  If pirq_sync is still high (more events pending) ->
    Transition to PENDING (re-assert after dead time)
  Else ->
    Transition to IDLE
```

This handles both XT (edge-triggered) and AT (level-triggered) PICs correctly:
- XT: sees a clean rising edge per event, with guaranteed deassert between events
- AT: sees assertion during PRESENTED, deassert on acknowledge, no spurious re-triggers

Estimated cost: 8 macrocells (3 state bits + output register + acknowledge detect + dead-time counter).

**IRQ host contract (binding on TSR, SDK, and all application ISRs):**

1. **Acknowledge action:** Reading the Status Register (port base+0x00, IN AL, DX) is the sole acknowledge action. This transitions the CPLD from PRESENTED to WAIT_DEASSERT. No other register read or write has this side effect.

2. **Polling mode side effect:** Reading port 0x00 in polling mode (no ISR installed) still triggers the acknowledge transition. This is harmless in polling mode because the IRQ line is not connected (IRQ_SENSE jumper open) and the state machine simply cycles through WAIT_DEASSERT back to IDLE. Polling applications may read port 0x00 freely without worrying about state corruption.

3. **ISR must always read status before IRET:** If the application uses IRQ mode, the hardware ISR must read port 0x00 before returning. Failure to do so leaves the state machine in PRESENTED, holding the IRQ line HIGH indefinitely. On AT-class machines (level-triggered PIC), this causes an interrupt storm. On XT-class machines (edge-triggered PIC), the next event will not generate a new rising edge because the line never went low.

4. **Multiple pending events collapse:** If the ESP32 raises PIRQ while the state machine is in PRESENTED (host has not yet acknowledged), the second event is not lost. After the host acknowledges and the 250ns dead time elapses, the state machine checks pirq_sync: if still HIGH, it re-enters PENDING and generates a new IRQ edge. If the ESP32 pulses PIRQ briefly during PRESENTED and then deasserts before the host reads status, that event IS lost. The ESP32 firmware must hold PIRQ HIGH until the event is consumed, not pulse it.

5. **ISR timing budget:** The ISR has no hard deadline from the CPLD's perspective. The state machine will hold in PRESENTED as long as needed. However, holding an ISA IRQ asserted for more than ~1ms may cause other devices sharing the same IRQ level (on AT-class machines with shared interrupts) to miss their own interrupts. The TSR's ISR should read status, service the event, and IRET as quickly as possible.

6. **Reduced mode interaction:** In reduced mode (runtime safe mode), async events are disabled and the ESP32 never asserts PIRQ. The IRQ state machine stays in IDLE. The IRQ jumper position has no effect in reduced mode.

#### 5.4.3 IOCHRDY: Fully Registered Release with Status Flag Timeout

IOCHRDY assertion and release are both registered (synchronous to CLK_16MHZ). Release never occurs from a combinational path or a direct asynchronous signal.

On successful data return (pready_sync detected), the CPLD loads the data from the ESP32 parallel bus into the output latch before releasing IOCHRDY. Data is valid before the bus cycle completes.

On watchdog timeout, the CPLD sets bit 5 (XFER_TIMEOUT) in the cached Status Register and releases IOCHRDY. The data byte on D0-D7 is undefined (last latch contents). The CPLD does NOT drive an error code onto the data port, because any byte value (including 0x01) could be valid application data during bulk transfers. This prevents the in-band signaling data corruption vulnerability where timeout bytes are indistinguishable from legitimate data in a REP INSB stream.

Estimated cost: 14 macrocells (8-bit watchdog counter + hold flag + release register + status flag set + pready_sync sample).

**IOCHRDY validation plan (mandatory before PCB confidence):**

IOCHRDY is the highest technical risk in the entire design. Correct CUPL/Verilog logic is necessary but not sufficient. The following tests must pass on real hardware with a logic analyzer capturing IOCHRDY, IOR#, D0-D7, and the parallel bus signals:

1. **Single cached read:** Read port 0x00 (Status Register). IOCHRDY must remain HIGH throughout. Logic analyzer confirms zero wait states. Pass criteria: data valid within 100ns of IOR# assertion.

2. **Single non-cached read:** Read port 0x04 (Data port) with ESP32 responding normally. IOCHRDY must assert LOW, then release HIGH when pready_sync fires. Logic analyzer confirms hold time is consistent. Pass criteria: IOCHRDY releases before watchdog timeout (10us).

3. **256-byte block read:** REP INSB from port 0x04, 256 bytes. Logic analyzer confirms IOCHRDY asserts and releases cleanly for each byte with no stuck-low events. Pass criteria: no bus timeout, no data corruption, XFER_TIMEOUT flag remains clear.

4. **Deliberate timeout test:** Disable ESP32 PREADY response (firmware test mode). Read port 0x04. IOCHRDY must assert LOW, hold for exactly 10us (160 clock cycles), then release. XFER_TIMEOUT flag (Status Register bit 5) must be set. Pass criteria: IOCHRDY releases cleanly, machine does not hang.

5. **Timeout during block read:** REP INSB with ESP32 configured to stall on the 50th byte. First 49 bytes must be correct. Byte 50 triggers XFER_TIMEOUT. TSR detects flag, discards block, retries. Pass criteria: block-level retry succeeds, no data corruption in application buffer.

6. **Back-to-back I/O recovery:** Two consecutive reads to different registers (port 0x00 then port 0x04) with minimum I/O recovery time. Verify the CPLD correctly handles the transition from cached (zero wait) to non-cached (IOCHRDY wait) without glitching. Pass criteria: both reads return correct data.

7. **Cross-machine validation:** Repeat tests 1-6 on at minimum: one XT-class (4.77 MHz), one AT/286 (8 MHz ISA), one 386/486 (8-12 MHz ISA). Document chipset, ISA bus speed, and any behavioral differences. Pass criteria: identical behavior across all three, or documented workarounds for any machine-specific issues.

If test 7 reveals machine-specific failures, the IOCHRDY design must be revised before PCB layout. "Works on my machine" is not acceptable for a card targeting 40 years of ISA hardware.

#### 5.4.4 Heartbeat: Removed from Rev A CPLD

The CPLD-side heartbeat monitor has been removed. It required either too many counter macrocells for a meaningful timeout (23 bits for 500ms at 16 MHz) or an impractically high ESP32 toggle rate. The spec honestly acknowledged this was "not silicon-ready."

**Liveness detection in rev A:**
- PBOOT pin indicates ESP32 firmware has completed initialization. This is a one-time flag, not a continuous heartbeat.
- If the ESP32 crashes after boot, every non-cached read will timeout via the IOCHRDY watchdog, returning ERR_NOT_READY. The TSR detects this pattern (N consecutive timeouts) and reports "Card firmware unresponsive" to the application.
- The host can issue a hardware reset (write 0xFF to port base+0x07) to reboot the ESP32.
- NISADIAG `/STATUS` reports whether the card is responding to commands.
- The ERR LED is driven by the ESP32 (not the CPLD). If the ESP32 crashes, the LED state freezes, which is a visible indicator of a problem.

This approach is simpler, uses zero CPLD macrocells, and is more reliable than a timing-dependent hardware monitor. If community feedback reveals a strong need for hardware crash detection, it can be added in v2 with a dedicated small counter IC or by leveraging the ATF1508AS's additional macrocells.

#### 5.4.5 Address Decode

Combinational decode of A9-A4 (6 bits) against jumper-selected base, with A3-A0 used for register select within the 16-port window. The decode must NOT include A3, or registers 0x08-0x0F will be inaccessible. One caution: the case table must be verified in the fitter to ensure product term count does not exceed macrocell capacity for the equality compare.

#### 5.4.6 Register Cache Strategy

Expanded per adversarial review. All registers that can be cached, are cached. The ESP32 pushes updates to cache registers proactively via the parallel bus (writes to PD0-PD7 with register address on PA0-PA3 and a PSTROBE pulse).

**Cached (zero wait-state reads, 8 of 16 registers):**
0x00 Status, 0x06 Error Code, 0x07-0x09 Firmware Version, 0x0A Session Count, 0x0B Session Capacity, 0x0C Network Status

**Non-cached (IOCHRDY wait states, 6 of 16 registers):**
0x01-0x02 Response Length, 0x03 Reserved, 0x04-0x05 Data In/Out, 0x0D Signal Quality

#### 5.4.7 Fitter Validation Requirement

**The CPLD design MUST be run through the actual fitter before PCB layout begins.** The recommended path is Quartus II 13.0sp1 targeting EPM7128STC100-15 (see Section 12.6), which provides reliable fitting, timing analysis, and Verilog support. WinCUPL II v1.1.0 is an acceptable alternative for initial validation. Pseudocode macrocell estimates are unreliable because real implementations grow once registered outputs, synchronizers, output enables, hazard suppression, and reset behavior are added. The fitter report is a Phase 0 deliverable, not a Phase 6 afterthought.

Specific items to validate in the fitter:
- Equality compare on A9..A4 against jumper-derived base (product term count, 6 bits not 7)
- Tri-state and output enable control on ISA D0-D7 and ESP32 PD0-PD7
- Watchdog counter bit width (8 bits at 16 MHz = 16us, but registered carry chains may consume more macrocells than expected)
- IRQ state machine fits within estimated 8 macrocells
- 2-flop synchronizers place correctly with timing constraints met
- Total pin count matches available I/O on ATF1508AS TQFP-100

If the fitter reveals the design does not fit in the ATF1508AS (unlikely at ~76/128 macrocells), the fallback is a Lattice iCE40UP5K FPGA with fully open-source toolchain, at the cost of losing native 5V tolerance (requires level shifters on the ISA side).

#### 5.4.8 CPLD Programming via ESP32 (v2 Feature)

The ATF1508AS requires JTAG programming, which normally needs a dedicated programmer (ATDH1150USB, ~$50). This is a significant adoption barrier for hobbyist builders.

**v2 enhancement:** Route 4 ESP32-S3 GPIO pins to the CPLD's JTAG header (TDI, TDO, TMS, TCK). The ESP32 firmware includes an SVF/XSVF player that reads a .jed or .svf file from the MicroSD card and programs the CPLD automatically. This eliminates the need for any external programmer. The flow becomes: place CPLD .jed file on MicroSD, insert card, ESP32 programs the CPLD on first boot (or on explicit command via NISADIAG).

For Phase 0 and v1, a dedicated JTAG programmer is still required. The ESP32-as-programmer feature requires PCB traces from ESP32 GPIO to the JTAG header, which must be designed into the PCB layout (Phase 6). The 4 GPIO pins needed are available from the 16 spare I/O pins on the ATF1508AS.

### 5.5 Boot Sequencing and Card Readiness

The ESP32-S3 requires 300-500ms to boot its firmware and 2-5 seconds to associate with WiFi. During this window, the card must respond to ISA bus cycles without crashing the host.

**Boot sequence:**

1. **Power-on / RESET DRV assertion:** CPLD initializes immediately (combinational logic, no boot time). CPLD drives Status Register with CARD_BOOTING state (bit 6 = 0, all ESP32-managed bits = 0). All non-status register reads return 0x00. All writes are accepted and silently discarded.

2. **ESP32-S3 firmware starts (~300-500ms):** ESP32 initializes GPIO, parallel bus interface, and internal state.

3. **ESP32-S3 loads configuration (~100-200ms):** Reads WiFi credentials from NVS (flash) or MicroSD. Loads certificate bundle from MicroSD (if present) or flash. Initializes mbedTLS contexts.

4. **ESP32-S3 asserts PBOOT (~400-700ms from power-on):** CPLD sets bit 6 (BOOT_COMPLETE) in Status Register. Card is now ready for commands. WiFi may not yet be connected, but the card accepts API calls (network operations will return ERR_NETWORK_DOWN until WiFi associates).

5. **WiFi association (~2-5s from power-on):** ESP32 connects to configured WiFi network. DHCP completes. Network Status register transitions to CONNECTED. Card is fully operational.

**TSR installation behavior:**

The TSR probes the card during installation (Section 6.3). If Status Register bit 6 (BOOT_COMPLETE) is not set, the TSR prints "Waiting for card..." and polls every 100ms for up to 10 seconds. If the card does not become ready within 10 seconds, the TSR prints a warning but installs anyway (the card may come ready later). If all reads return 0xFF (empty bus), the TSR aborts with "Card not detected."

### 5.6 ISA Bus Timing Compliance

The CPLD handles all timing-critical bus interactions. The ESP32-S3 never directly interfaces with the ISA bus.

**I/O Read Cycle Timing (8 MHz AT bus, worst case):**

```
         |<-- 125ns -->|<-- 125ns -->|<-- 125ns -->|<-- 125ns -->|
BCLK:    ____/````\____/````\____/````\____/````\____
AEN:     ````````\________________________________________________
A0-A9:   --------<  VALID ADDRESS  >------------------------------
IOR#:    ````````````````\______________________/`````````````````
D0-D7:   ZZZZZZZZZZZZZZZZZZZZZZZZ<  VALID DATA  >ZZZZZZZZZZZZZZZZ
IOCHRDY: ````````````````````````````````````````````````````````  (cache hit)
         ````````````````\_______________/```````````````````````  (cache miss, 2 wait states)

Data setup time before IOR# rising edge: minimum 30ns (ATF1508AS provides ~10ns)
Data hold after IOR# rising edge: minimum 0ns
```

**I/O Write Cycle Timing:**

```
BCLK:    ____/````\____/````\____/````\____/````\____
A0-A9:   --------<  VALID ADDRESS  >------------------------------
IOW#:    ````````````````\______________________/`````````````````
D0-D7:   ZZZZZZZZZZZZZZZZ<  VALID DATA  >ZZZZZZZZZZZZZZZZZZZZZZZZ

CPLD latches D0-D7 on IOW# rising edge. No wait states needed for writes.
```

### 5.7 Session Architecture

Each session is an independent state machine managed by the Session Manager task on Core 1:

```
              +---> DNS_RESOLVING ---+
              |                      |
              |                      v
  CLOSED -----+              TCP_CONNECTING
              |                      |
              |                      v
              |               TLS_HANDSHAKE
              |                      |
              |                      v
              |               ESTABLISHED <---> DATA_AVAILABLE
              |                      |
              |                 +----+----+
              |                 |         |
              |                 v         v
              +<---------- CLOSING    ERROR
                                |         |
                                v         v
                              CLOSED    CLOSED
```

#### 5.7.1 Session Memory Layout (per session, in PSRAM)

```
Offset    Size     Field
0x0000    4 KB     Receive ring buffer (cleartext data from remote, waiting for host to read)
0x1000    4 KB     Transmit ring buffer (cleartext data from host, waiting to be encrypted and sent)
0x2000    2 bytes  RX ring head pointer
0x2002    2 bytes  RX ring tail pointer
0x2004    2 bytes  TX ring head pointer
0x2006    2 bytes  TX ring tail pointer
0x2008    1 byte   Session state (enum, see state machine above)
0x2009    1 byte   Last error code
0x200A    2 bytes  Flags (TLS version negotiated, cert validation result, etc.)
0x200C    4 bytes  Bytes sent (cumulative, for stats)
0x2010    4 bytes  Bytes received (cumulative, for stats)
0x2014    4 bytes  Remote IP address
0x2018    2 bytes  Remote port
0x201A    2 bytes  Local port (ephemeral)
0x201C    ~40 KB   mbedTLS session context (allocated from PSRAM heap)
0xC000    End      (Total per session: ~52 KB)
```

8 sessions x 52 KB = 416 KB, well within ESP32-S3's 8 MB PSRAM. The remaining ~7.5 MB is available for certificate cache, DNS cache, firmware buffers, and future expansion.

#### 5.7.2 Session Lifecycle

**Opening a session (INT API 03/00):**

1. Host calls INT with hostname and port.
2. TSR writes OPEN command to card via I/O ports.
3. ESP32-S3 allocates session slot, transitions to DNS_RESOLVING.
4. DNS resolution completes (or fails with ERR_DNS_FAILED).
5. TCP SYN sent, transitions to TCP_CONNECTING.
6. TCP handshake completes (or fails with ERR_CONNECT_FAILED).
7. TLS ClientHello sent, transitions to TLS_HANDSHAKE.
8. TLS handshake completes: certificate validation, key exchange, cipher suite negotiation.
   - If cert fails and validation mode is STRICT: ERR_CERT_INVALID, session goes to ERROR.
   - If cert fails and validation mode is WARN: session opens, warning event queued.
9. Session transitions to ESTABLISHED. Session handle returned to host.

Total time for steps 3-9: typically 500ms-3s depending on network latency and server TLS configuration. The host polls session status (INT API 03/05) or waits for IRQ.

**Sending data (INT API 03/03):**

1. TSR reads CX bytes from the host's buffer at DS:SI.
2. TSR writes bytes to the card via Data In port (0x04), streaming one byte (8-bit bus) or one word (16-bit bus) per I/O cycle.
3. ESP32-S3 moves bytes from the parallel receive buffer into the session's TX ring buffer (4KB).
4. Session Manager task detects data in TX ring, encrypts via mbedTLS, sends over TCP.
5. AX returns the number of bytes actually accepted into the TX ring buffer.

**Flow control:** Send Data is non-blocking. It always returns immediately. AX contains the actual number of bytes accepted, which may be less than CX if the TX ring buffer is full or nearly full. If AX=0, the buffer is completely full; the application must poll and retry. This is NOT an error condition (CF remains clear). The application pattern is:

```c
// Example: sending a buffer with flow control
int remaining = total_length;
char far *ptr = buffer;
while (remaining > 0) {
    int sent = netisa_send(handle, ptr, remaining);  // INT API 03/03
    ptr += sent;
    remaining -= sent;
    if (sent == 0) {
        // TX buffer full; yield or poll briefly
        netisa_poll();  // INT API 00/01 or timer delay
    }
}
```

**Receiving data (INT API 03/04):**

1. TCP data arrives on socket. lwIP callback fires.
2. mbedTLS decrypts data, writes cleartext to session's RX ring buffer (4KB).
3. If host is waiting (polling or IRQ mode), ESP32-S3 asserts PIRQ to trigger host IRQ.
4. TSR reads cleartext from Data Out port (0x04), streaming bytes into the host buffer at ES:DI.
5. AX returns bytes actually read, which may be less than CX if fewer bytes are available.

Receive Data is also non-blocking. If no data is available, AX=0 and CF is clear. Use Get Session Status (INT API 03/05) to check bytes available before calling Receive Data, or use the event system (Group 0x06) to be notified when data arrives.

**Closing a session (INT API 03/02):**

1. Host sends CLOSE command.
2. ESP32-S3 sends TLS close_notify, then TCP FIN.
3. Session transitions to CLOSING.
4. Remote acknowledges (or timeout after 5 seconds).
5. Session resources freed, slot returned to pool.
6. Session transitions to CLOSED.

### 5.8 DMA Exclusion

NetISA does NOT use ISA DMA (Direct Memory Access) in any version. This is a deliberate architectural decision:

1. **DMA channel conflicts.** ISA DMA channels 0-3 (8-bit) and 5-7 (16-bit) are scarce and heavily contested. Channel 2 is used by the floppy controller. Channel 1 is used by Sound Blaster. Channel 3 is sometimes used by hard disk controllers on XT systems. Claiming a DMA channel would create conflicts that jumper configuration cannot easily resolve.

2. **DMA adds hardware complexity.** DMA requires the card to become bus master, driving address and data lines. This requires additional bus transceivers, DMA request/acknowledge handshaking logic, and terminal count detection. The ATF1508AS would need most of its remaining spare macrocells for DMA, leaving no room for future features.

3. **DMA is unnecessary for the data rates involved.** ISA DMA on an 8-bit channel transfers at ~1 MB/s maximum. Programmed I/O (REP INSB/OUTSB on 286+, or tight IN/OUT loops on 8088) achieves comparable throughput with minimal CPU overhead. The network and TLS layers are the bottleneck, not the host bus transfer rate. Real-world throughput will be 10-100 KB/s, where DMA provides zero benefit.

4. **DMA complicates the software model.** DMA requires the host to set up DMA controller registers, manage page boundaries (DMA cannot cross a 64KB physical page boundary on 8-bit channels or a 128KB boundary on 16-bit channels), and handle terminal count interrupts. Programmed I/O with the INT API is dramatically simpler for application developers.

All data transfer between the host and the card uses programmed I/O via the Data In/Out ports (0x04-0x05). On 286+ systems, the REP INSB and REP OUTSB instructions provide efficient block transfers. On 8088 systems, a tight loop of IN AL, DX / STOSB instructions achieves approximately 200 KB/s, which is more than sufficient.

### 5.9 MCU Abstraction Layer (Future-Proofing)

The ESP32-S3 is the right choice today, but NetISA is designed to be built and used for 10+ years. The ESP32-S3 may become unavailable, and superior alternatives (RP2350 with crypto extensions, future Espressif chips, STM32 with hardware TLS) will emerge.

The firmware defines a Hardware Abstraction Layer (HAL) that isolates all ESP32-S3-specific code into replaceable modules:

| HAL Module | Abstracts | ESP32-S3 Implementation |
|-----------|-----------|------------------------|
| hal_bus | Parallel bus GPIO interface | GPIO ISR on specific pins |
| hal_wifi | WiFi association, scan, status | ESP-IDF WiFi API |
| hal_eth | Ethernet init, link status | W5500 SPI driver |
| hal_tls | TLS context create/handshake/read/write | mbedTLS with hardware acceleration |
| hal_crypto | Raw AES/SHA/RSA/ECC operations | ESP32-S3 hardware peripherals |
| hal_storage | NVS, MicroSD, cert store | ESP-IDF NVS + FATFS |
| hal_time | SNTP, RTC, timestamps | ESP-IDF SNTP + internal RTC |
| hal_system | Reboot, sleep, liveness status, crashlog | ESP-IDF system APIs |

A future port to RP2350 (or any other MCU) replaces these eight modules. The session manager, command dispatcher, event queue, and all INT API command handlers are MCU-agnostic and remain unchanged. The CPLD design and ISA bus interface are entirely MCU-independent.

This is not premature abstraction. It is a single-file-per-module boundary that costs nothing in v1 and prevents a full rewrite in v3.

### 5.10 HTTP Helper Functions (v2 Reserved)

The "host does everything" philosophy is architecturally correct but practically painful on 8088 systems. Parsing HTTP/1.1 headers, chunked transfer encoding, and JSON responses in real-mode C on a 4.77 MHz CPU is slow and complex. The community will ask for help.

**v1 approach:** The SDK (NETISA.H) includes host-side helper functions for common HTTP patterns: `nisa_http_get()`, `nisa_http_read_headers()`, `nisa_http_read_chunked()`. These are pure C functions that run on the host CPU, using the card only for TLS transport. They are convenience wrappers, not card firmware features.

**v2 reserved (INT API Group 0x09):** If community demand warrants it, optional on-card HTTP helpers can be added in firmware v2. These would offload specific painful operations to the ESP32-S3:

| AH | AL | Function | Input | Output |
|----|-----|----------|-------|--------|
| 09 | 00 | Receive Line | AL=session handle, ES:DI=buffer, CX=max | AX=line length, CF=0 (reads until \\r\\n) |
| 09 | 01 | Receive Chunk | AL=session handle, ES:DI=buffer, CX=max | AX=chunk data length, CF=0 (decodes chunked TE) |
| 09 | 02 | Skip Headers | AL=session handle | CF=0 when blank line reached |
| 09 | 03-0F | Reserved | | ERR_NOT_IMPLEMENTED |

These are explicitly optional and never required. Applications that parse HTTP themselves will always work. These helpers exist solely to lower the barrier for 8088-era developers who would otherwise give up on HTTP parsing.

**This does NOT violate the "pipe, not platform" principle.** The card is still a transport layer. These helpers are stateless convenience functions operating on the already-decrypted byte stream, equivalent to the card doing `readline()` instead of `read()`. No HTTP state machine, no cookie handling, no redirect following lives on the card.

### 5.11 Certificate Bundle User Experience

Certificate management is the most likely source of "why doesn't HTTPS work?" support requests. The spec must define a painless workflow.

**What ships with v1:**

1. The ESP32-S3's internal flash contains a baked-in Mozilla CA bundle (approximately 200KB, ~130 root certificates) from the build date. This means the card works out of the box without a MicroSD card for certificate validation against all major websites.

2. The MicroSD card, if present, can contain an updated CA bundle file (`/certs/ca-bundle.pem`). If this file exists, it overrides the baked-in bundle. If the file is corrupt or missing, the baked-in bundle is used as fallback.

3. Firmware updates via MicroSD include an updated baked-in bundle, so users who update firmware automatically get fresh certificates.

**Update workflow:**

The project repository maintains a `/tools/update-certs/` directory containing:

- A Python script that downloads the current Mozilla CA bundle, converts it to PEM, and writes it to a MicroSD-formatted directory structure.
- Pre-built MicroSD image files (.img) for each quarterly release, downloadable from GitHub Releases. Users who lack Python can download the image and write it to a MicroSD with Rufus, Etcher, or `dd`.
- NISADIAG.EXE reports cert bundle date and cert count, making it obvious when the bundle is stale.

**Certificate expiration timeline:** Root CA certificates typically have 20-30 year validity periods. A bundle from 2026 will remain valid for the vast majority of websites until approximately 2040-2050. Annual updates are recommended but not urgent.

---

## 6. DOS Driver (TSR)

### 6.1 Overview

NETISA.COM is a Terminate-and-Stay-Resident program that installs the INT API and manages communication with the card. It is written in a combination of 8086 assembly (for the resident portion) and OpenWatcom C (for the transient installation/configuration logic).

### 6.2 Memory Layout

```
Resident Portion (target: under 2KB)
+------------------------------------------+
| PSP (Program Segment Prefix)     256 bytes|  (can be released after install)
+------------------------------------------+
| INT handler entry point           ~128 B  |  Jump table + register save/restore
+------------------------------------------+
| I/O port access routines          ~256 B  |  IN/OUT wrappers, REP INSB/OUTSB
+------------------------------------------+
| IRQ handler (ISR)                 ~128 B  |  Reads status, sets flag, EOI
+------------------------------------------+
| Polling timer hook (INT 08h)      ~64 B   |  Optional, chains to old INT 08h
+------------------------------------------+
| State variables                   ~64 B   |  Base address, IRQ, flags, error buffer
+------------------------------------------+
| Error string buffer               ~256 B  |  ASCIIZ strings for last error detail
+------------------------------------------+
| Signature + version               ~16 B   |  "CRISA10" + version for detection
+------------------------------------------+
| Old INT vector save               ~8 B    |  Previous INT 63h handler address
+------------------------------------------+
| Old INT 08h vector save            ~4 B   |  Previous timer handler (polling mode)
+------------------------------------------+
Total resident: ~1,180 bytes (fits in under 2KB with margin)
```

The transient portion (installation logic, argument parsing, card detection, help text) is freed after the TSR goes resident. The PSP's first 256 bytes can also be released if the TSR does not need to access its command line or environment after installation.

### 6.3 Installation

```
NETISA.COM [/I:nn] [/A:hhh] [/Q:n] [/POLL] [/S] [/V] [/U] [/?]

  /I:nn    Software interrupt number in hex (default: 63)
  /A:hhh   Card base I/O address in hex (default: 280)
  /Q:n     IRQ number: 3, 5, 7, 9, 10, or 11 (default: 5)
  /POLL    Polling mode: do not use IRQ, poll on timer tick instead
  /S       Skip: install TSR but do not wait for card readiness or WiFi
  /V       Verbose: print card info and stay (no TSR install)
  /U       Unload: remove TSR from memory
  /?       Display help
```

**Installation sequence:**

1. Parse command-line arguments.
2. **Skip-init check:** If the user holds the ESC key during loading, print "Skipping NetISA initialization" and exit without going resident. This provides a "break glass" escape if the TSR is in AUTOEXEC.BAT and the card is malfunctioning, preventing a boot loop. Inspired by XTIDE Universal BIOS's key-to-skip-init pattern.
3. Check if already installed by calling INT with presence check (AH=00, AL=00). If AX returns 4352h, TSR is already resident. Abort with message.
4. Probe the card by reading Firmware Version registers at the specified base address. Verify response is not 0xFF (empty bus) or 0x00 (stuck bus). If no card detected, abort with message.
5. **Firmware version negotiation:** Read card firmware version (major.minor.patch). Compare against the TSR's compiled-in minimum compatible firmware version. If the firmware is older than the minimum, print a warning: "Card firmware vX.Y.Z is older than TSR requires (vA.B.C). Some features may not work. Please update firmware." If the firmware major version differs from the TSR's expected major version, refuse to install: "Incompatible firmware version. TSR requires firmware vA.x.x, card has vX.x.x." This prevents the subtle semantic mismatch bugs that XT-IDE's CHS translation changes demonstrated.
6. **Bus self-test:** Write a test pattern (0x55, then 0xAA) to the card's command register and read it back. Verify the round-trip matches. If not, print "Bus communication error at address 0xNNN. Check jumper settings and card seating." and abort. This catches address conflicts, bad slot contacts, and bus contention before the TSR goes resident.
7. Wait for BOOT_COMPLETE flag in Status Register (unless /S flag is set). Poll every 100ms for up to 10 seconds. Print "Waiting for card..." during wait. If timeout, print warning but continue installation.
8. Display card firmware version, network status, session capacity.
9. Save current INT vector for the chosen interrupt number.
10. If not /POLL mode: install IRQ handler at the appropriate hardware interrupt vector. For IRQ 3/5/7 (first PIC), install at INT 0Bh/0Dh/0Fh. For IRQ 9/10/11 (second PIC), install at INT 71h/72h/73h. Unmask the IRQ in the 8259 PIC(s).
11. If /POLL mode: hook INT 08h (timer tick, fires ~18.2 times/second). Chain to the previous handler after checking the card's status register.
12. Install the INT API handler at the chosen software interrupt vector.
13. Print success message with base address, IRQ, interrupt vector, and resident memory size.
14. Terminate and stay resident (INT 21h, AH=31h) with the calculated resident paragraph count.

### 6.4 Unloading

```
NETISA.COM /U
```

**Unload sequence:**

1. Call INT with presence check to find the resident TSR.
2. Verify that the INT vector still points to our handler (another TSR may have chained after us). If the vector has been modified, warn the user and refuse to unload (unloading would leave a dangling pointer in the chain).
3. If /POLL mode, verify INT 08h still points to our timer hook. Same chain-integrity check.
4. Restore the original INT vector.
5. If IRQ mode, mask the IRQ in the PIC and restore the original hardware interrupt vector.
6. If /POLL mode, restore the original INT 08h vector.
7. Free the TSR's memory (INT 21h, AH=49h on the TSR's MCB).
8. Print success message.

### 6.5 Reentrancy and Critical Sections

The INT API handler is NOT reentrant. ISA-era DOS is fundamentally single-tasking, and reentrancy concerns are limited to specific scenarios:

**Scenario 1: Hardware interrupt during INT API call.**

If the card's IRQ fires while the TSR is already processing an INT API call (e.g., the host is mid-transfer on port 0x04 and the card signals new data on another session), the ISR must NOT attempt to process the event. The ISR's job is minimal: read the status register, set a flag byte in the TSR's resident data area, send EOI to the PIC(s), and return. The flag is checked the next time the host calls any INT API function or polls explicitly.

**ISR Hard Safety Rules (violating any of these will cause system crashes):**

1. The ISR NEVER calls any DOS INT 21h function. DOS is not reentrant. Calling DOS from within an ISR while DOS is mid-call causes stack corruption and system hangs.
2. The ISR NEVER performs multi-byte I/O transfers to the card. It reads one byte (the status register) and writes zero bytes. All bulk I/O happens in the INT API handler, which runs at application level, not interrupt level.
3. The ISR NEVER modifies segment registers (DS, ES). The ISR uses CS-relative addressing for all data access (`[cs:flag_byte]`), because DS/ES may point to the interrupted application's data segment.
4. The ISR ALWAYS sends EOI (End of Interrupt) to the PIC(s) before returning. For IRQ 0-7: `out 20h, 20h`. For IRQ 8-15: `out 0A0h, 20h` then `out 20h, 20h`. Missing EOI masks all lower-priority interrupts permanently.
5. The ISR completes in under 50 clock cycles (10us on 8088, 0.1us on 486). It must not contain loops, delays, or conditional waits.

Implementation:

```asm
; INT API entry point
netisa_handler:
    cmp byte [cs:in_api], 1     ; Already in API call?
    je .busy                     ; Yes: return ERR_NOT_READY
    mov byte [cs:in_api], 1     ; Set reentrancy guard
    sti                          ; Re-enable hardware interrupts
    ; ... process API call ...
    mov byte [cs:in_api], 0     ; Clear reentrancy guard
    iret

.busy:
    stc                          ; Set carry = error
    mov ax, 0001h               ; ERR_NOT_READY
    iret
```

**Scenario 2: INT API call from within a hardware ISR.**

This should not happen in normal DOS applications. If it does (pathological case), the reentrancy guard catches it and returns ERR_NOT_READY. The application must retry.

**Scenario 3: TSR interactions with DOS.**

The TSR does NOT call any DOS INT 21h functions during API handling. All I/O is direct port access. This avoids the classic DOS reentrancy problem entirely. The TSR is safe to call from within other ISRs (subject to the reentrancy guard above) and from background TSRs like network redirectors.

### 6.6 Polling Mode Implementation

When installed with /POLL, the TSR hooks INT 08h (timer tick) to periodically check the card for pending events.

```asm
timer_hook:
    pushf
    call far [cs:old_int08]      ; Chain to original timer handler first (critical for system timekeeping)
    push ax
    push dx
    mov dx, [cs:base_addr]       ; Status register port
    in al, dx
    test al, 04h                 ; ASYNC_DATA bit
    jz .no_event
    or byte [cs:event_pending], 1  ; Set flag for next API call
.no_event:
    pop dx
    pop ax
    iret
```

At 18.2 Hz, polling adds approximately 50-55ms of latency to event detection. This is acceptable for interactive network use (human-perceptible latency starts at ~100ms). For time-sensitive applications, IRQ mode is recommended.

The polling hook adds 2 IN instructions (~12 clock cycles on 8088, ~4 on 486) to every timer tick. This is negligible overhead even on the slowest systems.

### 6.7 Register Preservation

The INT API handler preserves ALL registers except those explicitly documented as output registers for the called function. This is critical: DOS applications, TSRs, and some compilers assume that INT calls preserve registers not documented as modified. The handler saves/restores via PUSHA/POPA on 286+ or individual PUSH/POP on 8088.

```asm
; 8088-compatible register save (PUSHA not available on 8088)
netisa_handler:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    ; ... AX is not saved because it's always an output register ...
    ; ... process call, AH/AL determine function ...
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    iret
```

### 6.8 Diagnostic Utility

```
NISADIAG.EXE

  Non-resident utility. Runs, displays information, exits.

  Output includes:
    - Card detected at I/O base 0xNNN, IRQ N
    - Firmware version: X.Y.Z (build string)
    - Network: Connected/Disconnected
    - WiFi SSID: "name" (signal: NN%)
    - IP address: N.N.N.N
    - Active sessions: N of N max
    - Certificate store: N certs loaded, N KB free
    - Uptime: N hours N minutes
    - Free heap: N bytes
    - Supported ciphers: TLS_AES_256_GCM_SHA384, ...

  Exit codes:
    0 = card found, all OK
    1 = card not found at specified address
    2 = card found but not responding
    3 = card found but network error
```

### 6.9 WiFi Configuration Utility

```
CISAWIFI.EXE

  Interactive text-mode utility (80x25).

  Features:
    - Scan for available WiFi networks (displays SSID, signal, security type)
    - Select network and enter password (masked input)
    - Test connection (ping gateway, resolve DNS, test TLS handshake)
    - Save configuration to MicroSD (/SAVE)
    - Load configuration from MicroSD (/LOAD)
    - Display current configuration (/SHOW)
    - Set static IP / enable DHCP
    - Set DNS servers

  Command-line mode (scriptable):
    CISAWIFI.EXE /SSID:"MyNetwork" /PASS:"password" /SAVE /TEST
```

---

## 7. OS Compatibility

### 7.1 MS-DOS / FreeDOS

Primary target. INT API available directly. All example code and documentation targets DOS first.

### 7.2 Windows 3.x (Standard and Enhanced Mode)

- **Standard Mode:** Real-mode TSR loaded before Windows works natively. INT API callable from Windows applications via DPMI real-mode callback.
- **Enhanced Mode:** Requires a VxD (Virtual Device Driver) to virtualize the INT interface for multiple DOS boxes and Win16 applications. v2 deliverable.

### 7.3 Windows 95/98/ME

- Real-mode TSR loaded via CONFIG.SYS/AUTOEXEC.BAT is accessible to DOS applications running in Windows.
- Native Win32 WinSock provider (layered over the INT API) is a v2 deliverable. Would enable any WinSock application to transparently use NetISA for TLS.

### 7.4 OS/2

Not targeted for v1. INT API may work via OS/2's DOS compatibility layer but is untested.

---

## 8. Security Model and Risk Analysis

**Security posture statement:** NetISA is a transport security device, not a secure enclave. It provides TLS encryption, certificate validation, and cryptographic primitives that are dramatically better than the status quo (zero encryption on vintage PCs). It does NOT provide tamper resistance, secure boot, encrypted credential storage (v1), or protection against physical access attacks. The threat model assumes a cooperative user on an untrusted network, not a hostile actor with physical access to the machine. This is the correct posture for a hobbyist retro computing accessory. If a future use case demands higher assurance (e.g., retro banking terminal, industrial control), a security-hardened firmware variant with ESP32-S3 flash encryption and secure boot can be developed as a v2 or community effort without hardware changes.

This section documents the card's security properties, attack surfaces, and deliberate design tradeoffs. It is written for the community reviewer who asks: "Should I trust this card inside my PC?"

### 8.1 Threat Model Summary

NetISA operates in a unique threat environment. The host systems (8088-486 PCs) have zero built-in security: no memory protection, no privilege rings (in real mode), no secure boot, no encrypted storage. The card's purpose is to add one specific capability (modern cryptography) to systems that otherwise have none. The security posture should be evaluated against the alternative, which is no encryption at all.

### 8.2 What the Card Protects

- **Data in transit.** TLS 1.3 between the card and remote servers provides confidentiality, integrity, and server authentication. An attacker monitoring the WiFi or Ethernet network sees only encrypted traffic, identical in protection to a modern browser.
- **Server identity.** Certificate validation (with a current CA bundle) prevents MITM attacks at the TLS layer. The card verifies the server's certificate chain against its root store before establishing a session.
- **Cryptographic operations.** The raw crypto API (Group 0x05) provides access to modern algorithms (AES-256, SHA-512, Ed25519, X25519) that are computationally infeasible to break, regardless of how they are invoked.

### 8.3 What the Card Does NOT Protect

These are deliberate architectural boundaries, not oversights:

- **Data on the ISA bus.** Cleartext data flows between the host CPU and the card over the ISA bus. Anyone with physical access to the PC's interior could theoretically probe the ISA bus signals and read cleartext. This is inherent to the coprocessor architecture. The same is true of every ISA network card ever made, every CPU-to-peripheral bus, and every modern PCIe NIC before it reaches the TLS termination point. **Mitigation:** Physical access to the PC interior is required. If an attacker has physical access to a DOS-era PC, they have already won regardless of this card.

- **Data in host RAM.** Cleartext application data exists in the host PC's conventional memory. DOS has no memory protection; any program can read any address. A malicious TSR or a cold-boot attack could extract cleartext. **Mitigation:** This is a property of the host operating system, not the card. NetISA does not make this worse than it already is.

- **Endpoint security.** The card does not scan for viruses, prevent malware, or sandbox applications. A compromised DOS application could misuse the card's API (e.g., exfiltrate data over a TLS session). **Mitigation:** DOS has never had endpoint security. The card provides tools; the user decides how to use them.

- **WiFi credential confidentiality at rest.** WiFi SSID and password are stored in the ESP32-S3's NVS flash partition, unencrypted. Physical access to the card's JTAG port could extract them. **Mitigation:** The card's JTAG header can be left unpopulated in production builds. ESP32-S3 flash encryption is a v2 consideration. The WiFi password has the same exposure as any consumer WiFi device (routers, IoT devices, phones) that stores credentials in flash.

### 8.4 Attack Surface Analysis

#### 8.4.1 WiFi Interface

The ESP32-S3's WiFi radio is a bidirectional network interface. It is the primary external attack surface.

**Risk:** An attacker on the same WiFi network could attempt to exploit vulnerabilities in the ESP32-S3's WiFi stack (lwIP, WiFi driver) or TLS implementation (mbedTLS).

**Mitigations:**
- The ESP32-S3 does not run a web server, SSH, or any listening services. It initiates outbound connections only. There is no inbound attack surface at the TCP/IP layer unless the application explicitly opens a listening session (INT API 03/07 with a listen option, which is not defined in v1).
- mbedTLS is the most widely deployed embedded TLS library, maintained by Arm with active CVE tracking. ESP-IDF releases include security patches.
- WPA2-PSK or WPA3-SAE (ESP32-S3 supports both) encrypts the WiFi link layer.
- Firmware updates via MicroSD allow patching without network-based OTA (which would itself be an attack surface).

#### 8.4.2 MicroSD Card

The MicroSD slot accepts removable media. A maliciously crafted SD card could attempt to exploit the firmware.

**Risks:**
- A malformed certificate bundle (PEM file) could attempt to exploit the PEM parser in mbedTLS.
- A malformed firmware update image could attempt to exploit the OTA update parser.
- A poisoned wifi.json could set credentials for an attacker-controlled access point (evil twin).

**Mitigations:**
- Firmware update images must be signed. The ESP32-S3's secure boot feature (v2 consideration) can enforce that only Anthropic-signed (or project-signed) firmware is accepted. In v1, the firmware validates a SHA-256 checksum embedded in the update image header.
- The PEM parser in mbedTLS is fuzz-tested and hardened against malformed input.
- The wifi.json provisioning path is optional and documented as a bootstrap mechanism, not the primary configuration method. Users who are concerned about evil twin attacks should configure WiFi via the CISAWIFI.EXE utility instead, which writes directly to NVS.

#### 8.4.3 ISA Bus Interface

The ISA bus is a shared, unauthenticated bus. Any card or the host CPU can read/write any I/O address.

**Risks:**
- A malicious ISA card or a compromised host application could send commands to NetISA's I/O ports, potentially opening TLS sessions to attacker-controlled servers, exfiltrating data from open sessions, or reconfiguring WiFi settings.
- There is no authentication between the host and the card. The INT API trusts all callers equally.

**Mitigations:**
- This is inherent to the ISA bus architecture. No ISA card has ever had bus-level authentication. The host system is fully trusted by design; if it is compromised, all peripherals are compromised.
- The card does not store sensitive application data persistently. Session data exists only in volatile PSRAM and is lost on power cycle.
- The card cannot access host memory (no bus mastering, no DMA). A compromised card cannot read host RAM.

#### 8.4.4 Firmware Supply Chain

The ESP32-S3 firmware is the card's trusted computing base. A compromised firmware build could exfiltrate data, weaken crypto, or install backdoors.

**Mitigations:**
- All firmware source code is published under an open-source license. The community can audit, build, and verify every line of code.
- Build instructions use ESP-IDF's standard toolchain with reproducible build configuration. Users can compile from source and compare binary hashes.
- Firmware updates are loaded from MicroSD, not over the network in v1. This prevents remote firmware tampering.
- The ESP32-S3's eFuse-based secure boot (v2) can lock the bootloader to accept only firmware signed with a specific key, preventing unauthorized firmware from running even with physical access.

### 8.5 Certificate Validation and MITM

TLS security depends on correct certificate validation. The card provides three validation modes configurable per session:

| Mode | Behavior | Use Case |
|------|----------|----------|
| STRICT (default) | Connection refused if cert is invalid, expired, or hostname mismatches | Production use, public internet servers |
| WARN | Connection established but a certificate warning event is queued for the host application to inspect | Development, debugging, corporate MITM proxies |
| SKIP | No certificate validation | Testing only. Clearly documented as insecure. |

**Root CA bundle maintenance:** The card ships with the Mozilla CA bundle. Users update it by replacing the PEM file on the MicroSD card. The project maintains a download page with current bundles in the correct format. Certificate expiration and revocation checking (OCSP stapling) is a v2 feature; v1 validates the chain and expiry date but does not check revocation status.

### 8.6 Privacy Considerations

- **DNS queries:** By default, DNS queries are sent in plaintext to the configured DNS server. This exposes the hostnames the user visits. DNS-over-TLS (DoT) is planned for v1.5 to address this.
- **WiFi probe requests:** The ESP32-S3's WiFi driver sends probe requests that include the configured SSID. This is standard WiFi behavior and reveals the network name to nearby observers. There is no mitigation short of passive scanning, which the ESP32 WiFi driver does support (configurable in firmware).
- **No telemetry.** The card does not phone home, collect usage data, or communicate with any server not explicitly requested by the host application. There are zero hardcoded remote endpoints in the firmware.

### 8.7 Comparison to the Status Quo

Without NetISA, a DOS PC connecting to the internet has:

| Capability | Without NetISA | With NetISA |
|-----------|-----------------|---------------|
| Network encryption | None. All traffic in cleartext. | TLS 1.3 with modern cipher suites. |
| Server verification | None. No way to validate identity. | Full certificate chain validation. |
| Password security | Transmitted in cleartext over HTTP. | Encrypted inside TLS tunnel. |
| Cryptographic operations | Software-only, limited to algorithms available in DOS-era libraries (DES, MD5). Painfully slow. | Hardware-accelerated AES-256, SHA-512, Ed25519, RSA-4096, X25519, ChaCha20. |
| Random number generation | PRNG seeded from timer tick (predictable). | Hardware TRNG (cryptographically secure). |

The card transforms a completely unprotected system into one with transport-layer security equivalent to a modern browser. It does not address endpoint security, physical security, or host OS vulnerabilities, because those are unsolvable on DOS without replacing DOS itself.

### 8.8 Responsible Disclosure

Security vulnerabilities in NetISA firmware or hardware should be reported via GitHub Security Advisories (private disclosure). The project commits to acknowledging reports within 72 hours and issuing patches within 30 days for confirmed vulnerabilities. Critical vulnerabilities (remote code execution, TLS bypass) will receive expedited patches and a firmware update advisory posted to the project's communication channels.

---

## 9. Community Use Cases and Edge Case Analysis

This section catalogs the applications the retro community is likely to build, the edge cases they will encounter, and how the architecture handles (or deliberately defers) each one. Several items here surfaced gaps that are now addressed via additions to the INT API, firmware, or roadmap.

### 9.1 Expected Application Categories

#### Network Applications (require TLS sessions)

| Application | Protocol | Complexity | Notes |
|-------------|----------|-----------|-------|
| Web browser (MicroWeb + HTTPS) | HTTP/1.1 over TLS | Medium | Host parses HTTP headers, chunked encoding, redirects. Card handles TLS. Most impactful single application. |
| IRC client (TLS-only networks) | IRC over TLS | Low | Text protocol, single persistent session. Natural fit. |
| Email client (IMAPS/SMTPS/POP3S) | IMAP/SMTP/POP3 over TLS | Medium | Multiple sessions (one for IMAP, one for SMTP). STARTTLS support needed (see 9.3.5). |
| Telegram Bot client | HTTPS REST API + JSON | Medium | Original project motivation. Uses the Bot API (simple HTTPS/JSON), NOT TDLib. TDLib is a full C++ client library requiring MTProto, hundreds of MB of RAM, and modern OS threading. It cannot run on DOS. The Bot API is a REST API that works with simple HTTPS GET/POST requests and JSON parsing, which is entirely feasible on a 386+ with NetISA. Bot accounts have some limitations versus user accounts (cannot join groups uninvited, no secret chats) but are fully functional for messaging. An alternative approach: a modern gateway/proxy server running TDLib that exposes a simplified REST API, which the DOS client consumes via NetISA. |
| Gopher over TLS | Gopher over TLS | Low | Simple protocol, single session per request. |
| Gemini browser | Gemini (TLS-native) | Low | Gemini protocol is designed for TLS. Clean fit. |
| FTP over TLS (FTPS) | FTP over TLS | High | FTP uses separate control/data connections. Needs 2+ sessions. Passive mode required (active mode blocked by NAT). |
| BBS access (TLS telnet) | Telnet over TLS | Low | Single session, character-at-a-time. Low bandwidth. |
| Mastodon/Fediverse client | HTTPS REST API + JSON | Medium | ActivityPub uses standard HTTPS. JSON parsing on 8088 is the bottleneck. |
| Discord webhook poster | HTTPS POST | Low | One-way: post messages to a Discord channel via webhook URL. |
| RSS/Atom feed reader | HTTPS GET + XML | Medium | XML parsing on host. Multiple sessions for multiple feeds. |
| DOS package manager | HTTPS GET + file I/O | Medium | Download .zip/.lzh archives from HTTPS repositories. Integrity via SHA-256. |
| Home automation control | HTTPS REST API | Low | POST commands to Home Assistant or similar via REST API. |
| Weather/stock ticker | HTTPS GET + JSON | Low | Periodic API polling, text-mode display. |
| Online leaderboard for retro games | HTTPS POST/GET | Low | Submit scores, retrieve rankings. |
| Multi-machine file transfer | TLS session between two NetISA PCs | Medium | One card listens (requires v2 listen support), other connects. |

#### Local Crypto Applications (no network required)

| Application | Crypto Functions Used | Notes |
|-------------|----------------------|-------|
| SHA-256 file hasher (sha256sum) | SHA-256 streaming | Verify downloaded files, check disk integrity. |
| File encryptor/decryptor | AES-256-GCM, Random | Encrypt sensitive files on local storage. |
| Password manager | AES-256-GCM, HKDF, Random | Encrypted password database with master password. |
| TOTP authenticator | HMAC-SHA1, time source | Generate 2FA codes. Requires HMAC-SHA1 (added, see 9.3.8) and accurate time (see 9.3.6). |
| Digital signature tool | Ed25519 Sign/Verify | Sign files to prove authenticity. Verify signatures on downloads. |
| Encrypted archive tool | AES-256-GCM, SHA-256 | Create/extract encrypted .CIA archives (custom format). |
| Secure random password generator | Random | Generate cryptographically strong passwords. |
| Disk wipe verification | SHA-256 streaming | Hash disk sectors to verify wipe completion. |
| Software license validator | Ed25519 Verify | Verify license signatures in retro shareware. |
| PGP-like messaging | RSA or X25519 + AES-256 | Public key exchange, encrypted message files between PCs. |

### 9.2 Edge Cases the Community Will Hit

#### 9.2.1 HTTP Protocol Challenges

The card provides a cleartext TLS tunnel. All HTTP protocol handling is the host application's responsibility. The community will encounter:

**Chunked Transfer Encoding.** Many modern servers use chunked encoding by default. The host must parse chunk headers or request `Transfer-Encoding: identity` (not always honored). The SDK should include a helper function or example code for parsing chunked responses.

**HTTP Redirects (301/302/307).** Servers frequently redirect HTTP to HTTPS or www to non-www. The host must detect 3xx status codes, extract the Location header, close the current session, and open a new one to the redirect target. The SDK example code must demonstrate this pattern.

**Compressed Content.** Servers often send gzip-compressed responses by default. An 8088 cannot decompress gzip in any reasonable time. Applications must send `Accept-Encoding: identity` in their request headers to request uncompressed responses. The SDK documentation must emphasize this.

**Cookies.** Session cookies are required by many web services. The host must parse Set-Cookie headers, store cookies, and include them in subsequent requests. This is a host-side concern. The SDK may provide a simple cookie jar helper in v1.5.

**Keep-Alive.** HTTP/1.1 defaults to persistent connections. A TLS session can serve multiple HTTP requests without renegotiating TLS. Applications should reuse sessions rather than opening/closing per request. The session timeout configuration (INT API 03/06, option 0x02-0x03) controls how long an idle session remains open before the card closes it.

#### 9.2.2 Slow Host / Fast Network Mismatch

An 8088 at 4.77 MHz processes received data much slower than the network delivers it. The 4KB RX ring buffer can fill in ~40ms at broadband speeds, while the 8088 needs ~80ms to read 4KB via IN instructions.

**Current handling:** When the RX ring buffer is full, the ESP32-S3's lwIP TCP stack stops acknowledging incoming TCP segments (TCP flow control). The remote server pauses transmission. Data is not lost. When the host reads from the RX buffer and frees space, TCP acknowledges resume and data flows again. This is standard TCP behavior and requires no special handling from the application.

**Recommendation:** Applications on 8088 systems should read received data as quickly as possible to avoid triggering TCP flow control, which adds latency but does not lose data. On 286+ systems, REP INSB provides fast enough bulk reads that the buffer rarely fills.

#### 9.2.3 Binary Data Integrity

The I/O ports transfer raw bytes including 0x00 (null). There is no byte-stuffing or escaping. Binary data (images, compressed files, protocol buffers) transfers correctly through the card. The session layer is fully 8-bit clean.

#### 9.2.4 Very Long Hostnames

Internationalized domain names in punycode can exceed 64 characters (e.g., `xn--nxasmq6b.xn--80akhbyknj4f.xn--p1ai`). The hostname buffer for INT API functions 03/00 and 02/00 is an ASCIIZ string limited only by the caller's buffer size. The ESP32-S3's DNS resolver and mbedTLS SNI field support hostnames up to 253 characters (the DNS specification limit). No special handling required.

#### 9.2.5 No Real-Time Clock

Many XT and early AT systems lack a battery-backed RTC. TLS certificate validation requires checking the certificate's notBefore and notAfter dates, which requires knowing the current date and time. If the host has no RTC, certificate date validation may fail (the ESP32's internal clock starts at epoch 0 on power-on).

**Resolution:** The ESP32-S3 firmware performs SNTP (Simple Network Time Protocol) synchronization automatically after WiFi connects (see 9.3.6). This sets the ESP32's internal clock to the correct UTC time regardless of whether the host PC has an RTC. Certificate date validation uses the ESP32's clock, not the host's. The host can also read the card's time via a new API function (see 9.3.6).

#### 9.2.6 Captive Portals

Hotel, airport, and public WiFi networks often require a web-based login before internet access is granted. The card will successfully associate with the WiFi network (DHCP works) but all HTTPS connections will fail because the captive portal intercepts TLS handshakes.

**Detection:** The firmware can detect captive portals by attempting a known HTTP request (e.g., `http://connectivitycheck.gstatic.com/generate_204`) and checking if the response is a 204 (no portal) or a redirect (portal present). A new network status code CAPTIVE_PORTAL (value 3) in the Network Status register signals this condition to the host.

**Resolution:** The host application must handle the captive portal login via a plaintext HTTP session (INT API 03/07) to the portal page. This requires the user to interact with a web form on a text-mode DOS display, which is inherently clunky. A dedicated CISAPORTAL.EXE utility could render a simplified text version of the portal page. This is a v2 feature.

#### 9.2.7 Certificate Chain Size

Some servers send certificate chains exceeding 10KB (e.g., certificates with embedded SCT lists, or chains with 3+ intermediate CAs). The ESP32-S3 handles this internally using PSRAM. The host never sees the raw certificate data unless it explicitly requests peer cert info (INT API 04/03). Handshake time increases proportionally with chain size but is bounded by the ESP32's processing speed, not the ISA bus.

#### 9.2.8 WiFi Roaming and Reconnection

If the WiFi signal drops (user moves the PC, router reboots, interference), all active TLS sessions are lost. TCP connections are broken. The ESP32-S3's WiFi driver automatically attempts reconnection. When WiFi reassociates and DHCP completes, the Network Status register transitions back to CONNECTED, but all previously open sessions are now in ERROR state with ERR_DISCONNECTED.

Applications must handle this by detecting ERR_DISCONNECTED on send/receive, closing the dead session, and reopening when network status returns to CONNECTED. The SDK documentation must include a reconnection pattern.

#### 9.2.9 Simultaneous Applications

Two DOS programs cannot use the TSR simultaneously (DOS is single-tasking). However, a TSR-based background application (e.g., a mail notifier) might attempt to poll the card while the foreground application is mid-transfer. The reentrancy guard (Section 6.5) returns ERR_NOT_READY to the background caller. Background TSRs that use NetISA must be designed to handle ERR_NOT_READY gracefully and retry on the next timer tick.

Under Windows 3.x Enhanced Mode or Windows 95, multiple DOS boxes could theoretically attempt simultaneous access. This requires the VxD (v2) to serialize access to the card's I/O ports.

### 9.3 Gaps Identified and Addressed

The use case analysis above identified several gaps in the current architecture. The following additions are made to the spec:

#### 9.3.1 TLS Session Resumption

**Gap:** Opening a new TLS session takes 1-3 seconds due to the full handshake (key exchange, certificate validation). Applications that open/close sessions frequently (e.g., a web browser following redirects) will be painfully slow.

**Addition:** The ESP32-S3 firmware implements TLS 1.3 session resumption via Pre-Shared Keys (PSK), as defined in RFC 8446. After the first handshake with a server, the card caches a session ticket. Subsequent connections to the same server use the cached ticket for a 0-RTT or 1-RTT resumption, skipping the full handshake. The session ticket cache holds up to 16 entries in PSRAM, LRU-evicted. No INT API changes required; resumption is handled transparently in the firmware.

#### 9.3.2 RX Buffer Overflow Prevention

**Gap:** If the host is extremely slow reading data, can the 4KB RX ring buffer overflow?

**Addition:** No. The RX ring buffer has a hard size limit. When it is full, the ESP32-S3 stops calling `mbedtls_ssl_read()`, which in turn stops reading from the lwIP TCP receive buffer, which in turn stops acknowledging incoming TCP segments. Standard TCP flow control prevents the remote server from sending more data. No data is lost, no overflow occurs. The only consequence is increased latency as the remote server waits. This is documented in Section 9.2.2 above.

#### 9.3.3 Session Keep-Alive and Idle Timeout

**Gap:** How long can an idle session remain open before the card closes it?

**Addition to INT API Group 0x03, Session Options (function 03/06):**

| Value | Option | CX Value |
|-------|--------|----------|
| 0x05 | Idle timeout (seconds) | 0=no timeout (default), 1-65535 |
| 0x06 | TCP keepalive interval (seconds) | 0=disabled (default), 30-65535 |

When idle timeout is set, the card closes the session after CX seconds of no send/receive activity. When TCP keepalive is set, the card sends TCP keepalive probes at the specified interval to prevent NAT/firewall timeout. Default is no timeout and no keepalive, meaning sessions persist until explicitly closed or the connection drops.

#### 9.3.4 IPv6 Session Open

**Gap:** The INT API supports IPv6 DNS resolution (02/01) but session open (03/00, 03/01) only accepts IPv4 addresses.

**Addition to INT API Group 0x03:**

| AH | AL | Function | Input | Output |
|----|-----|----------|-------|--------|
| 03 | 08 | Open Session (IPv6) | DS:SI=ASCIIZ hostname (resolved via AAAA), BX=port | AL=session handle, CF=0 |
| 03 | 09 | Open Session (IPv6 literal) | DS:SI=16-byte IPv6, BX=port, ES:DI=ASCIIZ SNI | AL=session handle, CF=0 |

IPv6 support depends on the WiFi network providing IPv6 connectivity. If IPv6 is not available, these functions return ERR_CONNECT_FAILED. The ESP32-S3's lwIP stack supports dual-stack (IPv4+IPv6) natively.

#### 9.3.5 STARTTLS (Connection Upgrade)

**Gap:** Email protocols (SMTP, IMAP) and some others use STARTTLS to upgrade a plaintext connection to TLS mid-stream. The current API has no way to upgrade an existing plaintext session.

**Addition to INT API Group 0x03:**

| AH | AL | Function | Input | Output |
|----|-----|----------|-------|--------|
| 03 | 0A | Upgrade to TLS | AL=session handle, DS:SI=ASCIIZ SNI hostname | CF=0 on success |

This function takes an existing plaintext session (opened via 03/07) and performs a TLS handshake on it. The session transitions from ESTABLISHED (plaintext) through TLS_HANDSHAKE to ESTABLISHED (encrypted). If the handshake fails, the session transitions to ERROR. The host application is responsible for sending the protocol-specific STARTTLS command (e.g., "STARTTLS\r\n" for SMTP) and reading the server's acknowledgment before calling this function.

#### 9.3.6 Time Synchronization (SNTP)

**Gap:** TLS certificate validation requires accurate time. Many vintage PCs lack an RTC. The card's ESP32-S3 starts with no time reference on power-up.

**Addition to firmware:** The ESP32-S3 performs SNTP synchronization automatically within 5 seconds of WiFi association, using pool.ntp.org (configurable via MicroSD config). The ESP32's internal RTC maintains time between SNTP syncs. If SNTP fails, certificate date validation is relaxed (warn but do not fail), and a CLOCK_NOT_SET event is queued.

**Addition to INT API Group 0x00:**

| AH | AL | Function | Input | Output |
|----|-----|----------|-------|--------|
| 00 | 08 | Get UTC Time | ES:DI=7-byte buffer | Buffer filled: year(2B), month(1B), day(1B), hour(1B), min(1B), sec(1B) |
| 00 | 09 | Get Unix Timestamp | None | DX:AX = seconds since 1970-01-01 00:00:00 UTC |

This gives DOS applications access to accurate network time even on machines without an RTC. A community utility could use this to set the DOS system clock via INT 21h/2Bh+2Dh on every boot.

#### 9.3.7 Captive Portal Detection

**Gap:** Public WiFi with captive portals will cause all TLS connections to fail without obvious indication of why.

**Addition to Network Status values (INT API 00/03):**

| AL Value | Status |
|----------|--------|
| 0 | Disconnected |
| 1 | Connecting |
| 2 | Connected (internet reachable) |
| 3 | Captive portal detected (WiFi associated, internet blocked) |

The firmware performs a connectivity check after WiFi association by sending an HTTP GET to a known URL and checking for the expected response. If a redirect or unexpected response is received, the status is set to CAPTIVE_PORTAL. The host application can then prompt the user or launch CISAPORTAL.EXE.

#### 9.3.8 HMAC-SHA1 for TOTP Compatibility

**Gap:** TOTP (Time-based One-Time Password, RFC 6238) uses HMAC-SHA1, not HMAC-SHA256. The current crypto API only has HMAC-SHA256.

**Addition to INT API Group 0x05:**

| AH | AL | Function | Input | Output |
|----|-----|----------|-------|--------|
| 05 | 1B | HMAC-SHA1 | DS:SI=params struct, ES:DI=20-byte output | CF=0 on success |

SHA-1 is cryptographically weak for general use, but HMAC-SHA1 remains secure for message authentication and is required for TOTP interoperability with Google Authenticator, Authy, and similar services. A community TOTP utility combining this with the Get UTC Time function (00/08) would turn any DOS PC with a NetISA card into a hardware 2FA token generator.

#### 9.3.9 WPA2-Enterprise WiFi

**Gap:** Corporate and university WiFi networks use WPA2-Enterprise (802.1X) with PEAP, EAP-TLS, or EAP-TTLS authentication. The current WiFi config API only supports PSK (pre-shared key).

**Addition to INT API Group 0x01:**

| AH | AL | Function | Input | Output |
|----|-----|----------|-------|--------|
| 01 | 0B | Set WiFi Enterprise Mode | AL=EAP type (0=PEAP, 1=TTLS, 2=TLS) | CF=0 on success |
| 01 | 0C | Set WiFi Identity | DS:SI=ASCIIZ username | CF=0 on success |
| 01 | 0D | Set WiFi CA Cert | DS:SI=ASCIIZ path on MicroSD | CF=0 on success |
| 01 | 0E | Set WiFi Client Cert | DS:SI=ASCIIZ path on MicroSD | CF=0 on success |
| 01 | 0F | Set WiFi Client Key | DS:SI=ASCIIZ path on MicroSD | CF=0 on success |

The ESP32-S3 supports WPA2-Enterprise natively via ESP-IDF's WiFi API. This is a v1.5 feature (firmware changes only, no hardware changes).

#### 9.3.10 UDP Consideration

**Gap:** The card supports TCP only. Several useful protocols use UDP: NTP (handled internally by firmware), DNS (handled internally), game protocols, MQTT-SN, mDNS/Bonjour.

**Decision:** UDP support is deferred to v2. The INT API reserves Group 0x08 for UDP functions. The rationale for deferral: UDP adds a fundamentally different data flow model (datagrams vs. streams), the primary use cases (NTP, DNS) are handled internally by the firmware, and the v1 release scope is already extensive. The API reservation ensures forward compatibility.

**Reserved (v2) INT API Group 0x08:**

| AH | AL | Function | Input | Output |
|----|-----|----------|-------|--------|
| 08 | 00 | Open UDP Socket | BX=local port (0=ephemeral) | AL=socket handle, CF=0 |
| 08 | 01 | Send Datagram | AL=handle, DS:SI=dest struct, ES:DI=data, CX=len | AX=bytes sent, CF=0 |
| 08 | 02 | Receive Datagram | AL=handle, ES:DI=buffer, CX=bufsize | AX=bytes received, BX=source port, CF=0 |
| 08 | 03 | Close UDP Socket | AL=handle | CF=0 |
| 08 | 04-0F | Reserved | | ERR_NOT_IMPLEMENTED |

#### 9.3.11 Firmware Recovery Procedure

**Gap:** If a bad firmware image is flashed via MicroSD, the card could become unresponsive. The safe mode jumper (J3) boots with WiFi disabled but still requires working firmware.

**Addition:** The ESP32-S3 supports a factory partition scheme. The flash layout includes three partitions: factory (known-good firmware, write-protected), ota_0, and ota_1. Normal firmware updates write to ota_0 or ota_1 and set the boot partition accordingly. If the new firmware fails to boot (detected by a boot counter: if the firmware doesn't clear a "pending verification" flag within 30 seconds, the bootloader reverts to the factory partition on the next reboot), the ESP32 automatically falls back to the factory firmware.

This means the card is unbrickable via normal MicroSD firmware updates. The only way to brick it is via direct JTAG access to the ESP32-S3, which requires physical modification of the card (the JTAG header is unpopulated by default).

### 9.4 Use Case Compatibility Matrix

| Use Case | v1.0 | v1.5 | v2.0 | Blocking Issue |
|----------|------|------|------|---------------|
| HTTPS web browsing | YES | YES | YES | None |
| IRC over TLS | YES | YES | YES | None |
| Email (IMAPS/POP3S) | YES | YES | YES | None |
| Email (SMTP + STARTTLS) | YES* | YES | YES | *STARTTLS added in 9.3.5 |
| Telegram Bot API | YES | YES | YES | None |
| Gemini browser | YES | YES | YES | None |
| Gopher over TLS | YES | YES | YES | None |
| FTP over TLS | YES | YES | YES | Needs 2+ sessions; passive mode only |
| RSS feed reader | YES | YES | YES | XML parsing is host-side challenge |
| Home automation REST | YES | YES | YES | None |
| Discord webhooks | YES | YES | YES | None |
| Mastodon client | YES | YES | YES | JSON parsing on 8088 is slow |
| DOS package manager | YES | YES | YES | None |
| TOTP authenticator | YES* | YES | YES | *HMAC-SHA1 added in 9.3.8 |
| File encryption | YES | YES | YES | None |
| Password manager | YES | YES | YES | None |
| SHA-256 file hasher | YES | YES | YES | None |
| PGP-like messaging | YES | YES | YES | None |
| WPA2-Enterprise WiFi | NO | YES* | YES | *Added in 9.3.9 |
| Captive portal login | NO | NO | YES | Requires text-mode web form rendering |
| UDP protocols (games, MQTT-SN) | NO | NO | YES* | *Reserved API in 9.3.10 |
| WebSocket connections | YES* | YES | YES | *Host handles HTTP upgrade; card provides the TLS stream |
| Multi-PC encrypted file transfer | NO | NO | YES | Requires server/listen socket support |
| DOS system clock sync | YES* | YES | YES | *SNTP + Get UTC Time added in 9.3.6 |

---

## 10. Version Roadmap

### v1.0: Foundation

- 8/16-bit ISA card with ESP32-S3
- WiFi connectivity (802.11 b/g/n)
- TLS 1.3 with certificate validation and session resumption (PSK)
- STARTTLS support (plaintext-to-TLS upgrade for SMTP/IMAP)
- 4 concurrent TLS sessions with configurable idle timeout and TCP keepalive
- DNS resolution (plain DNS)
- SNTP time synchronization (automatic, exposed to host via Get UTC Time)
- Full raw crypto API (Group 0x05) including HMAC-SHA1 for TOTP
- Captive portal detection (Network Status = 3)
- Baked-in CA cert bundle in ESP32 flash (works without MicroSD), MicroSD override for updates
- WiFi credentials stored in ESP32 NVS (not on removable MicroSD)
- Firmware factory partition with automatic rollback (unbrickable)
- DOS TSR + diagnostic and WiFi utilities
- SDK: NETISA.H + NETISA.LIB (OpenWatcom) + NETISA_TC.LIB (Turbo C)
- Example apps: HTTPS GET, IRC client skeleton, SHA-256 file hasher, TOTP generator
- Ethernet pads on PCB (unpopulated)
- KiCad source, firmware source, all open

### v1.5: Ethernet, Packet Driver, and Enterprise

- W5500 Ethernet populated and supported
- **PC/TCP Packet Driver specification TSR** (INT 0x60-0x80, send_pkt/access_type/receive callback). Card appears as standard Ethernet NIC to existing DOS TCP/IP stacks. Gives instant mTCP, WATTCP, and Crynwr compatibility without NetISA-native application code. Plaintext TCP/IP only through packet driver; TLS requires the custom INT API. Dual-interface approach: standard networking for broad compatibility, crypto offload for applications that need it.
- SOCKS5 proxy mode (card acts as local SOCKS proxy for existing DOS TCP/IP software using mTCP/WATTCP; firmware-only feature, no hardware changes)
- WPA2-Enterprise WiFi (PEAP, EAP-TTLS, EAP-TLS)
- DNS-over-TLS (DoT)
- 8 concurrent sessions
- IPv6 support (session open + DNS AAAA)
- Certificate pinning API
- HTTP chunked encoding helper in SDK
- Cookie jar helper in SDK
- Community-reported bug fixes

### v2.0: Windows, UDP, and Ecosystem

- Windows 3.1 Enhanced Mode VxD
- Windows 95/98 WinSock provider
- UDP socket support (Group 0x08)
- TLS 1.2 backward compatibility (for older servers)
- Client certificate authentication (mutual TLS)
- Server/listen socket support (accept inbound TLS connections)
- Captive portal login utility (CISAPORTAL.EXE)
- ESP32-S3 flash encryption for NVS
- Community-contributed packet driver shim (for software expecting NE2000-style interface)
- NETISA_DJ.A (DJGPP/DPMI SDK library)

### v2.5 and Beyond (Community-Driven)

- DTLS (UDP-based TLS)
- Bluetooth (ESP32-S3 has BLE)
- Additional cipher suites as needed
- 32-bit protected mode driver (for DJGPP/DOS4GW applications)
- PCI version of the card (separate hardware, same API)
- mDNS/Bonjour discovery
- MQTT over TLS

---

## 10.1 Phased Development and Hardware Bring-Up

The v1 feature scope is deliberately comprehensive because the INT API is the social contract with developers. Every function defined in v1 must be available from the first release. However, the development and validation sequence is strictly layered. Each phase must be fully validated before the next begins. Do not write firmware for TLS until the ISA bus interface is proven. Do not attempt WiFi until TCP sockets work over Ethernet.

**Phase 0: Bus Interface Validation (Target: 2 weeks)**

Goal: Prove the CPLD, parallel interface, and ISA bus timing across machine classes.

Hardware: Prototype PCB or hand-wired ISA card with ATF1508AS CPLD (TQFP-100 on breakout board), 16 MHz oscillator, SN74LVC8T245 level shifter, ESP32-S3 devkit connected via ribbon cable. No WiFi, no Ethernet, no MicroSD needed.

Deliverables:
- CUPL source for address decode, data latch, IOCHRDY, IRQ
- ESP32 firmware that reads/writes the parallel interface via GPIO ISR
- **ESP32 USB serial console** (via built-in USB-JTAG): real-time log output of all ISA bus events, parallel bus transactions, and firmware state changes. Accessible from any modern PC via screen/minicom/PuTTY. This is the primary debugging interface for all phases.
- **Firmware crashlog**: on panic/watchdog reset, backtrace and register dump saved to NVS partition. Readable via USB serial console on next boot and via NISADIAG on host.
- DOS test program (not the TSR yet, just a standalone .COM) that writes a byte to the card and reads it back
- Loopback test: write 256 different byte values, read each back, verify integrity
- Test on minimum three machines: 8088 XT, 286 AT, 486 DX2
- Measure IOCHRDY behavior on each machine with a logic analyzer or oscilloscope
- Measure +5V rail voltage and ripple on each test machine with the card installed and WiFi transmitting. Verify voltage stays above 4.5V and ripple does not exceed 500mV on any machine. If XT PSU sags excessively, test with the 470uF input capacitor increased to 1000uF
- Verify IRQ delivery on each machine (both edge and level triggered)
- Verify polling mode works when IRQ is disabled

This is the highest-risk phase. If the bus interface doesn't work reliably across all three machine classes, nothing else matters. Budget extra time here. The CPLD design looks correct on paper but real bus loading, chipset timing variation, and ISR latency under load will reveal issues that simulation and pseudocode cannot predict.

**Phase 0 Shopping List (exact parts, verified available):**

All parts are orderable from DigiKey and/or Mouser as of early 2026. Phase 0 is deliberately cheap: everything except the logic analyzer is under $80 total, and most of it reuses into the final board.

*Active Components:*

| Part | Description | DigiKey PN | Qty | Est. Price |
|------|-------------|-----------|-----|-----------|
| ATF1508AS-10AU100 | CPLD, 128 MC, TQFP-100, 5V, 10ns | ATF1508AS-10AU100-ND | 2 | $16.13 ea |
| TQFP-100 breakout board | 0.5mm pitch QFP-100 to DIP adapter | (see prototyping section) | 2 | $3-5 ea |
| ESP32-S3-DevKitC-1U-N8R8 | Devkit, U.FL antenna, 8MB flash, 8MB PSRAM | ESP32-S3-DEVKITC-1U-N8R8 | 1 | $15.00 |
| SN74LVC8T245PWR | Level shifter, 8-bit, TSSOP-24 | 296-21853-1-ND | 2 | $1.44 ea |
| SN74HCT245N | Bus buffer, 8-bit, DIP-20 | 296-1612-5-ND | 2 | $0.65 ea |
| SN74AHC14N | Hex Schmitt trigger inverter, DIP-14 | 296-4556-5-ND | 1 | $0.50 |
| 16 MHz oscillator | Full-can DIP-14, 5V, TTL/CMOS output | ECS-100AX-160 (DigiKey: X947-ND) | 1 | $3.14 |

*Passive Components:*

| Part | Description | Qty | Est. Price |
|------|-------------|-----|-----------|
| 100nF ceramic caps (0805 or through-hole) | Bypass, X7R, 16V+ | 20 | $2.00 |
| 470uF electrolytic | Low-ESR, 10V, radial | 2 | $0.80 |
| 100uF electrolytic | 10V, radial | 2 | $0.50 |
| 47uF ceramic | X5R, 6.3V, 0805 | 2 | $0.60 |
| 10K resistor pack | 1/4W, through-hole or 0805 | 10 | $0.50 |
| 100K resistor | Pull-down for RESET DRV | 2 | $0.10 |
| 33-ohm resistor pack | Series termination for data bus | 10 | $0.50 |
| SMBJ5.0A TVS diode | 5V clamp, SMB | 1 | $0.25 |
| 10uF ceramic caps (0805) | X5R, 10V, for DC-DC input | 4 | $0.80 |

*Prototyping and Interconnect:*

| Part | Description | Qty | Est. Price |
|------|-------------|-----|-----------|
| ISA prototyping card | TexElec 8-Bit ISA Prototype Card v1.0 (texelec.com, not available at DigiKey/Mouser) | 1 | $10.99 |
| TQFP-100 breakout board | 0.5mm pitch QFP-100 to DIP adapter (e.g., Chip Quik PA0100 or generic QFP100 adapter from eBay/Amazon) | 2 | $3-5 ea |
| 20-pin ribbon cable + IDC connectors | ESP32 devkit to proto board, 12" | 1 | $3.00 |
| Breadboard | 830-point, for ESP32 devkit mounting | 1 | $5.00 |
| Jumper wire kit | Male-male, male-female, 22AWG | 1 | $6.00 |
| 2.54mm pin headers | For jumper blocks (IRQ, safe mode, address) | 2 strips | $1.00 |
| Jumper caps | 2.54mm, for jumper blocks | 10 | $1.00 |
| TSSOP-24 breakout board | For SN74LVC8T245 (SMD to DIP adapter) | 2 | $1.50 ea |

*Test Equipment (reusable across all phases):*

| Part | Description | Qty | Est. Price |
|------|-------------|-----|-----------|
| Logic analyzer | DSLogic U3Pro16 (16ch, 1GHz) or DSLogic Plus (16ch, 400MHz) | 1 | $70-150 |
| Logic analyzer probes | Grabber clips for PLCC/DIP pins | 1 set | $10 |
| Multimeter | For PSU voltage verification (you almost certainly own one) | 1 | $0 |
| USB-A to USB-C cable | For ESP32 devkit USB-JTAG serial console | 1 | $3 |

*CPLD Programming:*

| Part | Description | Qty | Est. Price |
|------|-------------|-----|-----------|
| ATDH1150USB | Microchip JTAG programmer for ATF15xx CPLDs | 1 | $50-60 |
| **OR** FT232H JTAG | FT232H breakout + OpenOCD (community ATF15xx JTAG) | 1 | $15-25 |
| **OR** Arduino JTAG | Community open-source ATF15xx JTAG via Arduino (free) | 1 | $0 (if you have an Arduino) |

**WARNING: TL866II+ and T48 universal programmers CANNOT program ATF1508AS CPLDs.** These programmers support only GAL/PAL devices (ATF16V8, ATF22V10) via their ZIF socket. The ATF1508AS requires JTAG protocol programming. Only the ATDH1150USB, an FT232H-based JTAG adapter, or an Arduino JTAG implementation works.

*Software Tools (free):*

| Tool | Purpose |
|------|---------|
| Quartus II 13.0sp1 (free from Intel/Altera) | CPLD design: Verilog, compile for EPM7128STC100-15, convert via POF2JED |
| POF2JED (free from Microchip) | Convert Quartus .pof to ATF1508AS .jed. CRITICAL: set JTAG=ON, TDI_PULLUP=ON, TMS_PULLUP=ON |
| WinCUPL II v1.1.0 (free from Microchip) | Alternative CPLD path. CUPL source provided for this flow. |
| ESP-IDF v5.x (free) | ESP32-S3 firmware development |
| OpenWatcom v2 (free, open source) | DOS TSR and utility development |
| NASM (free, open source) | 8086 assembly for TSR resident portion |
| DOSBox-X (free, open source) | Initial INT API testing without real hardware |

**Phase 0 total estimated cost (excluding logic analyzer and programmer): $110-130**
**Phase 0 total with FT232H JTAG + budget logic analyzer: $195-260**
**Phase 0 total with ATDH1150USB + quality logic analyzer: $230-300**

The cost increase from the original estimate ($65-80) is driven by the CPLD package change: ATF1508AS-10AU100 (TQFP-100) is $16.13 each vs the obsolete PLCC-84 at $7.50, plus TQFP-100 breakout boards ($3-5 each) replace the $1.50 PLCC sockets.

*Notes:*
- Order 2x of CPLD and level shifters. You will make mistakes. Spares prevent week-long delays waiting for replacements.
- The ESP32-S3-DevKitC-1U variant has a U.FL connector (not PCB antenna). This matches the final card design which uses U.FL to an external antenna. For Phase 0 bench testing, attach a short 2.4 GHz pigtail antenna directly to the U.FL connector.
- Phase 0 does not require a TPS563200 switching regulator. The ESP32 devkit has its own USB-powered 3.3V regulator. Power the prototype CPLD from the ISA bus +5V, and power the devkit from USB.
- The TSSOP-24 breakout boards for the SN74LVC8T245 are essential unless you are comfortable soldering 0.65mm pitch directly to perf board.

**Phase 1: TSR and Command Protocol (Target: 2 weeks)**

Goal: Prove the DOS software model end-to-end.

Hardware: Same Phase 0 prototype board.

Deliverables:
- NETISA.COM TSR with presence check, installation, unloading, and IRQ/polling mode
- NISADIAG.EXE showing card status, firmware version, bus loopback test
- Command mailbox protocol working: host sends multi-byte command, card processes, host reads multi-byte response
- Bulk data transfer: REP OUTSB / REP INSB at maximum rate, verify no data corruption
- Measure throughput on each test machine
- NETISA.H and NETISA.LIB (stub, with loopback test example)

**Phase 2: TCP Sockets over Ethernet (Target: 3 weeks)**

Goal: Prove network connectivity with the simplest possible path.

Hardware: Add W5500 module (breakout board, SPI-connected to ESP32-S3). Still no WiFi, no MicroSD.

Rationale for Ethernet first: An RJ45 cable into a switch is deterministic. There is no SSID configuration, no antenna placement, no RF debugging, no association delay, no WPA handshake. If TCP doesn't work over Ethernet, the problem is in the firmware's socket or session management, not in RF. This eliminates an entire debugging dimension during the phase where the network stack is most fragile.

Deliverables:
- INT API Group 0x03 (sessions) working for plaintext TCP (function 03/07)
- INT API Group 0x02 (DNS) working
- Demonstrate: DOS program opens a TCP connection to a local HTTP server, sends a GET request, receives and displays the response
- Session state machine validated: open, send, receive, close, error handling
- Multiple concurrent sessions (at least 2)
- Flow control validation: send data faster than network can transmit, verify no data loss

**Phase 3: TLS Client Sessions (Target: 3 weeks)**

Goal: Prove TLS 1.3 works end-to-end over the proven TCP/Ethernet path.

Hardware: Same as Phase 2. Add MicroSD for certificate bundle.

Deliverables:
- INT API Group 0x03 function 03/00 (Open TLS Session) working
- INT API Group 0x04 (certificate management) working
- Demonstrate: DOS program opens HTTPS connection to example.com, retrieves a page
- Certificate validation tested: valid cert (pass), expired cert (fail), self-signed cert (warn/skip modes)
- TLS session resumption validated (connect, close, reconnect, measure handshake time)
- STARTTLS validated (open plaintext, upgrade to TLS)
- SNTP time sync working

**Phase 4: WiFi and Provisioning (Target: 2 weeks)**

Goal: Add WiFi as a second network interface, prove antenna and provisioning.

Hardware: Connect U.FL antenna to ESP32-S3 module. Mount inside actual PC case to test RF.

Deliverables:
- INT API Group 0x01 (WiFi config) working
- CISAWIFI.EXE utility working
- WiFi association, DHCP, DNS, TLS all functional
- Test WiFi signal inside at least 3 different PC cases (XT, AT tower, 486 desktop)
- Captive portal detection working
- NVS credential storage working
- Config save/load from MicroSD working

**Phase 5: Raw Crypto, Utilities, and Polish (Target: 3 weeks)**

Goal: Complete the v1 feature set.

Deliverables:
- INT API Group 0x05 (raw crypto) fully implemented and tested
- All hash functions, AES-GCM, ChaCha20, X25519, Ed25519, RSA, HMAC, HKDF, Random
- Streaming hash API validated
- HMAC-SHA1 for TOTP validated
- Get UTC Time validated
- INT API Group 0x06 (async events) validated
- INT API Group 0x07 (diagnostics) validated
- All example applications compiled and tested
- Firmware update via MicroSD validated (including rollback on bad image)
- Turbo C SDK variant (NETISA_TC.LIB) compiled and tested
- Full test matrix (Section 3.8) completed on all target machines

**Phase 6: PCB Design and Production (Target: 4 weeks)**

Goal: Finalize KiCad design and produce v1.0 boards.

Deliverables:
- KiCad schematic finalized (all components, all connections verified against prototype)
- ISA edge connector dimensions verified against EISA specification (not blog posts or other open-source projects, which may have incorrect spacing). Critical dimensions: finger-to-board-edge distance must be sufficient for JLCPCB beveling (minimum 0.5mm, targeting 0.8mm); pad A1 to bracket face distance must match standard ISA cards (measure with calipers against 3+ known-good vintage cards). PicoGUS had to fix both of these in successive revisions.
- 4-layer PCB layout with proper power planes, ground pour, controlled impedance for parallel bus
- PCB surface finish: HASL with lead for edge connector fingers (thicker plating than ENIG, better wear resistance in ISA slots that see repeated insertion). ENIG is acceptable for non-edge-connector pads.
- Design rule check (DRC) and electrical rule check (ERC) clean
- Gerber review (visual inspection of every layer)
- BOM finalized with Mouser/DigiKey/LCSC part numbers
- Order 5-10 prototype boards from JLCPCB or PCBWay
- Assemble 3 boards: one for primary development, one for compatibility testing, one for community review
- Final validation on production boards across full test matrix

**Total estimated timeline: Phase 0 through Phase 6 = 19 weeks (~5 months) for a solo developer working evenings and weekends.**

With Claude Code and the garage PCs, phases 1-5 (firmware and DOS software) can overlap significantly with hardware prototyping. The critical path is Phase 0: nothing starts until the bus interface is proven.

---

## 11. Bill of Materials (Preliminary, v1)

| Component | Part | Qty | Est. Unit Cost |
|-----------|------|-----|----------------|
| MCU | ESP32-S3-WROOM-1U-N8R8 (U.FL, 8MB flash, 8MB PSRAM) | 1 | $6.13 |
| CPLD | Microchip ATF1508AS-10AU100 (TQFP-100, 128 macrocells, 5V native) | 1 | $16.13 |
| DC-DC regulator | TPS563200DDCT (3A sync buck, SOT-23-6) | 1 | $1.00 |
| Buck inductor | Wurth 744043004.7 (4.7uH, shielded) | 1 | $0.40 |
| Input TVS diode | SMBJ5.0A (5V clamp, SMB) | 1 | $0.20 |
| Bulk input cap | 470uF 10V low-ESR electrolytic | 1 | $0.50 |
| WiFi surge cap | 47uF X5R 6.3V ceramic | 1 | $0.30 |
| ESD protection | SRV05-4 TVS diode arrays (SOT-23-6) | 4 | $0.40 ea |
| Level shifter | SN74LVC8T245 (TSSOP-24) CPLD-to-ESP32 parallel bus | 1 | $0.60 |
| Bus buffer | 74HCT245 (DIP-20) ISA data bus drive reinforcement | 1 | $0.40 |
| WiFi antenna | 2.4 GHz U.FL pigtail + bracket-mount RP-SMA or PCB patch | 1 | $2.00 |
| MicroSD slot | Molex 104031-0811 push-push | 1 | $0.80 |
| Status LEDs | 3mm green, blue, amber, red | 4 | $0.10 ea |
| RTC crystal | ABS07-32.768KHZ-T (Abracon) | 1 | $0.35 |
| CPLD oscillator | ECS-100AX-160 16.000MHz (ECS Inc, DIP-14, 5V, TTL) or equiv | 1 | $3.14 |
| Ferrite beads | BLM18AG601SN1D | 3 | $0.10 ea |
| RESET filter | 10K resistor + 100nF cap + 74AHC14 (Schmitt trigger) + 100K pull-down | 1 set | $0.50 |
| ISA edge connector | 98-pin (62+36), HASL-with-lead surface finish | 1 | PCB edge |
| DIP switch | 3-position (address select) | 1 | $0.30 |
| Jumper blocks | 2.54mm headers + jumper caps (IRQ, safe mode) | 2 | $0.20 ea |
| Passives | Resistors, capacitors, decoupling | ~30 | $3.00 total |
| PCB | 4-layer, ISA form factor, HASL-with-lead edge fingers, beveled | 1 | $8-15 (qty 5, JLCPCB) |
| Bracket | Standard ISA half-height, with LED/antenna holes | 1 | $2.00 |
| **Ethernet (v1.5)** | W5500 (LQFP-48) + RJ45 MagJack + 25MHz crystal | 1 | $4.00 |

**Estimated BOM cost (v1, WiFi only): $40-45 per card in small quantity.** The CPLD package change (PLCC-84 obsolete, TQFP-100 at $16.13 vs $6.50) accounts for most of the increase from original estimates.

**Estimated BOM cost (v1.5, WiFi + Ethernet): $44-49 per card.**

---

## 12. Programming Languages and Toolchain

### 12.1 Language Selection Summary

| Component | Language | Toolchain | Rationale |
|-----------|---------|-----------|-----------|
| TSR resident portion | 8086 Assembly | NASM 2.x | Every byte matters. Direct INT handler entry/exit, exact register control, no runtime overhead. |
| TSR transient portion | C (8086 real mode) | OpenWatcom v2 wcc | Argument parsing, string handling, card detection. Linked with ASM resident module. |
| NISADIAG.EXE | C (8086 real mode) | OpenWatcom v2 wcc | Readability, maintainability, community contributions. |
| CISAWIFI.EXE | C (8086 real mode) | OpenWatcom v2 wcc | Interactive UI logic, string handling for SSID/password input. |
| SDK library (NETISA.LIB) | C with inline ASM | OpenWatcom v2 wcc | Developer adoption. Clean C API wrapping INT calls. |
| SDK header (NETISA.H) | C | Portable (OW, Turbo C, DJGPP compatible) | Maximum compatibility across DOS compilers. |
| Example applications | C (8086 real mode) | OpenWatcom v2 wcc | Primary examples. Turbo C 2.0 compatibility as secondary goal. |
| ESP32-S3 firmware | C | ESP-IDF v5.x (GCC cross-compiler) | ESP-IDF is C-native. C++ available but not needed. |
| CPLD logic | Verilog (recommended) or CUPL (alternative) | Quartus II 13.0sp1 + POF2JED, or WinCUPL II | Verilog via Quartus is the community-recommended path for ATF150x. CUPL source also provided. |
| Windows VxD (v2) | x86 Assembly + C | MASM + Microsoft DDK | Required by VxD architecture. |
| Windows WinSock (v2) | C | OpenWatcom v2 (Win16) or MSVC | Standard Win16/Win32 development. |

### 12.2 Why OpenWatcom v2 for DOS C Code

OpenWatcom v2 is the only actively maintained C compiler that produces genuine 8086 real-mode code and cross-compiles from modern 64-bit Linux, Windows, and macOS hosts. This combination is critical for a project that targets 8088 hardware but is developed on modern machines.

**Key capabilities:**

- Targets 8086, 80186, 80286 real mode and 80386+ protected mode from a single toolchain
- Produces .COM (tiny model) and .EXE (small/medium/large/huge model) DOS executables
- Inline assembly via `#pragma aux` and `_asm` blocks, allowing C and assembly to interoperate tightly
- Small memory model produces compact executables with minimal runtime overhead; the C runtime startup code can be bypassed entirely for .COM programs
- wmake build system supports DOS, Windows, and Linux host builds from the same makefile
- Active community on GitHub (open-watcom/open-watcom-v2) with Discord support channel
- Comprehensive documentation including DOS-specific programming guides
- Produces highly optimized 8086 code (register allocation, loop optimization, dead code elimination)
- Free and open source under the Sybase Open Watcom Public License

**Compiler flags for NetISA DOS targets:**

```
# 8086 real mode, small memory model, max optimization, no stack checking
wcc -0 -ms -ox -s -zl -fo=.obj source.c

# -0     Target 8086 (no 186+ instructions)
# -ms    Small memory model (near code, near data)
# -ox    Maximum optimization
# -s     Disable stack overflow checking (saves code size)
# -zl    Suppress default library references (for custom linking)
```

**Alternatives evaluated and rejected:**

| Compiler | Why Rejected |
|----------|-------------|
| Turbo C 2.0 | Free but proprietary (Borland), no source. Cannot cross-compile from Linux. Produces good 8086 code but lacks modern build system integration. Acceptable for community contributions but not the primary toolchain. |
| DJGPP (GCC) | 32-bit protected mode only. Requires DPMI host. Cannot target 8088/8086. Eliminates XT compatibility. |
| GCC ia16 (gcc-ia16) | Experimental 16-bit x86 GCC port. Immature, limited optimization, poor DOS runtime support. Not production-ready. |
| Borland C++ 3.1 | Proprietary. Excellent 8086 code but cannot legally redistribute the compiler or its runtime. Cannot cross-compile from modern systems. |
| Digital Mars | Free, decent 8086 support, but tiny community and stagnant development. |
| Pacific C | 8086 capable but very limited optimization and effectively abandoned. |

### 12.3 Why NASM for the TSR Resident Portion

The TSR's resident code (interrupt handler, ISR, polling hook, I/O port access routines) must be as small and controlled as possible. Assembly is the only correct choice for this portion.

**Why NASM specifically:**

- Free, open source (BSD license), actively maintained, runs on all modern platforms
- Intel syntax (matches IBM PC documentation and most retro computing references)
- Native support for .COM output (`org 100h`, `bits 16`, `cpu 8086`)
- Produces raw binary output without linker dependency for simple .COM programs
- Can produce .OBJ files (OMF format) for linking with OpenWatcom C modules via `wlink`
- Preprocessor macros enable clean, maintainable assembly code
- `cpu 8086` directive ensures no accidental use of 186+ instructions

**Alternative:** OpenWatcom's built-in assembler (WASM) is also acceptable and simplifies the toolchain to a single vendor. WASM uses MASM syntax, which some developers prefer. The project should accept contributions in either NASM or WASM syntax.

### 12.4 SDK Design (NETISA.H / NETISA.LIB)

The SDK is the primary mechanism for community adoption. It must be trivially easy for a DOS C programmer to use NetISA without understanding the INT calling convention, register layout, or I/O port details.

**NETISA.H** provides:

```c
/* NetISA SDK - Public API Header
 * Compatible with: OpenWatcom v2, Turbo C 2.0+, Borland C++ 3.x+
 * Memory model: Small or Large
 */

#ifndef NETISA_H
#define NETISA_H

/* Status codes */
#define NISA_OK             0x00
#define NISA_ERR_NOT_READY  0x01
#define NISA_ERR_NO_SESSIONS 0x03
/* ... all error codes ... */

/* Session handle type */
typedef unsigned char NISA_SESSION;

/* Initialize - call once at program start */
int nisa_init(void);
    /* Returns NISA_OK if TSR is loaded and card is ready */

/* Network */
int nisa_wifi_connect(const char *ssid, const char *password);
int nisa_net_status(void);  /* Returns 0=disconnected, 1=connecting, 2=connected */
int nisa_get_ip(unsigned char ip[4]);

/* TLS Sessions */
int nisa_open(const char *hostname, unsigned int port, NISA_SESSION *handle);
int nisa_open_plain(const char *hostname, unsigned int port, NISA_SESSION *handle);
int nisa_send(NISA_SESSION handle, const void *buf, unsigned int len);
    /* Returns bytes accepted (may be < len if buffer full) */
int nisa_recv(NISA_SESSION handle, void *buf, unsigned int bufsize);
    /* Returns bytes received (0 if none available) */
int nisa_close(NISA_SESSION handle);
int nisa_status(NISA_SESSION handle);

/* DNS */
int nisa_resolve(const char *hostname, unsigned char ip[4]);

/* Crypto (no network required) */
int nisa_sha256(const void *data, unsigned int len, unsigned char hash[32]);
int nisa_sha512(const void *data, unsigned int len, unsigned char hash[64]);
int nisa_random(void *buf, unsigned int count);
int nisa_aes256gcm_encrypt(const void *key, const void *nonce,
                           const void *plaintext, unsigned int len,
                           const void *aad, unsigned int aad_len,
                           void *ciphertext, void *tag);
/* ... remaining crypto functions ... */

#endif /* NETISA_H */
```

**Compiler compatibility strategy:** The header uses only C89 constructs (no C99/C11 features). Function implementations in NETISA.LIB use OpenWatcom `#pragma aux` for the INT calls, which is compiler-specific. For Turbo C compatibility, a separate NETISA_TC.LIB is built using Turbo C's `int86()` and `int86x()` functions from `<dos.h>`. Both libraries expose the same API via the same header.

For DJGPP (32-bit protected mode, v2+ target), the INT calls must go through DPMI real-mode interrupt simulation (`__dpmi_int()`). A NETISA_DJ.A (DJGPP archive) would be a v2 deliverable.

**Developer experience on 8088-class systems:** The SDK is designed with a realistic understanding of what an 8088 can and cannot do. Modern web protocols (HTTP/1.1, JSON APIs, chunked encoding, gzip compression) are genuinely difficult to parse on a 4.77 MHz CPU with 640KB RAM. The SDK does not pretend otherwise. Instead, it provides a layered approach:

1. **Raw layer** (all systems): `nisa_send()` / `nisa_recv()` over TLS sessions. The application handles everything above the byte stream. This works on any system but requires the developer to write protocol parsing.

2. **Helper layer** (all systems, host-side): `nisa_http_get()`, `nisa_http_read_headers()`, `nisa_http_read_chunked()`. These are C functions in NETISA.LIB that run on the host CPU. They handle common HTTP patterns using the raw layer underneath. They are slow on 8088 but functional.

3. **Assisted layer** (v2, on-card): INT API Group 0x09 helpers (receive line, decode chunked TE) that offload the most painful parsing to the ESP32. These are optional and reserved for v2.

The expected developer experience for a "fetch HTTPS data" use case on an 8088: call `nisa_http_get("api.example.com", "/data")`, wait 3-8 seconds for DNS + TLS handshake + response, receive data into a buffer. Subsequent requests to the same server use TLS session resumption and complete in 1-3 seconds. This is slow by modern standards but dramatically faster than "impossible," which is the current state of HTTPS on vintage PCs.

### 12.5 Firmware Language

The ESP32-S3 firmware is written in C using ESP-IDF v5.x, which is natively C-based. C++ is available in ESP-IDF but adds runtime overhead (exception handling, RTTI, static constructors) without proportional benefit for this firmware's needs. All firmware components (ISA bus driver, session manager, crypto engine, network manager) are written in plain C.

**Build environment:**

```bash
# Install ESP-IDF
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32s3
source export.sh

# Build NetISA firmware
cd netisa/firmware
idf.py set-target esp32s3
idf.py build
idf.py flash   # Via USB-JTAG or UART
```

CI builds use the official ESP-IDF Docker container (`espressif/idf:v5.x`) in GitHub Actions.

### 12.6 CPLD Language and Toolchain

**Recommended workflow: Quartus II 13.0sp1 + POF2JED.** The ATF1508AS is register-compatible with the Altera EPM7128STC100-15. Write the design in Verilog, compile in Quartus Prime Lite (free) targeting EPM7128STC100-15, then convert the .pof output to .jed using Microchip's POF2JED utility. This workflow provides a modern IDE, a reliable fitter with timing analysis, and Verilog/VHDL support.

**Critical POF2JED settings (incorrect settings can brick the chip):**
- JTAG Mode: **ON** (not "Auto"). If set to Auto or Off, JTAG may be disabled after programming, requiring 12V applied to OE1 pin through a 2K resistor to recover.
- TDI Pullup: **ON**
- TMS Pullup: **ON**
- Device: ATF1508AS (match exact package)

**Why not WinCUPL:** WinCUPL's IDE is unstable (crashes on code errors, broken copy-paste), and the legacy fitters (v1.8.7.8, 2003) produce fitting failures that newer fitters resolve. WinCUPL II v1.1.0 (released 2025) ships updated fitters but still has reported compilation issues. The CUPL language also lacks adequate support for complex state machines. The community consensus (peterzieba/5Vpld, avitech.com.au vintage bits, multiple VOGONS threads) is that the Quartus path is more reliable for anything beyond simple GAL designs.

**Alternative: WinCUPL II for simple validation.** If Quartus is unavailable, WinCUPL II v1.1.0 with its updated fitters (version 1918) is acceptable. The Phase 0 CUPL file (netisa.pld) is provided for this path. For production, convert to Verilog and use Quartus.

**Experimental: Yosys + atf15xx_yosys.** The hoglet67/atf15xx_yosys project enables open-source Verilog synthesis for ATF150x devices. However, it lacks robust support for bidirectional and tri-state signals, which are essential for ISA bus work. Not recommended for NetISA until this limitation is resolved.

**Programming hardware:** Microchip ATDH1150USB JTAG programmer (~$50), FT232H-based JTAG adapter with OpenOCD (~$15-25), or Arduino-based JTAG (community tool from peterzieba/5Vpld). **TL866II+ and T48 universal programmers cannot program ATF1508AS** (they only support GAL/PAL devices via ZIF socket, not JTAG).

**Counterfeit chip warning:** Purchase ATF1508AS exclusively from authorized distributors (DigiKey, Arrow, Future Electronics). Cheap ATF1508AS chips from AliExpress, Amazon marketplace sellers, and eBay frequently arrive JTAG-locked, non-functional, or counterfeit. The ATF1508AS-10AU100 (TQFP-100, commercial temp, 10ns) is the production target. The PLCC-84 variant (ATF1508AS-10JU84) is confirmed obsolete at DigiKey as of April 2026. Do not design new hardware around the PLCC-84 package.

### 12.7 Build System and CI

All DOS software uses wmake (OpenWatcom's make utility) with a single top-level makefile that builds all targets:

```makefile
# Top-level wmake targets
all: netisa.com nisadiag.exe cisawifi.exe netisa.lib examples

# TSR: link NASM-assembled resident stub with Watcom C transient code
netisa.com: tsr/resident.obj tsr/transient.obj
    wlink sys com file tsr/resident.obj, tsr/transient.obj

# Utility programs
nisadiag.exe: diag/nisadiag.obj sdk/netisa.lib
    wlink sys dos file diag/nisadiag.obj lib sdk/netisa.lib
```

**Cross-compilation from modern hosts:** OpenWatcom v2 and NASM both run natively on modern Linux, Windows, and macOS. The entire DOS software suite can be built on a modern machine without any emulator. The resulting .COM and .EXE files are copied to a floppy image, MicroSD, or transferred via serial/network to the target DOS machine.

**Testing environments:**

| Environment | Purpose | Notes |
|-------------|---------|-------|
| DOSBox-X | Quick iteration, API testing | Does not emulate real ISA hardware; requires mock I/O port driver |
| 86Box | Accurate hardware emulation | Can emulate ISA bus timing; useful for IOCHRDY testing |
| MartyPC | Cycle-accurate 8088 emulation | Best for verifying 8088 compatibility |
| Real hardware | Final validation | Required before any release. See test matrix in Section 3.8 |

### 12.8 Repository Structure

```
netisa/
  docs/
    architecture-spec.md    (this document)
    v1-scope.md
    int-api-reference.md
    security-model.md
  hardware/
    kicad/                  (schematic + PCB)
    cpld/                   (CUPL source for ATF1508AS)
    gerbers/
    bom/
  firmware/
    main/                   (ESP-IDF project root)
    components/
      isa_bus/              (parallel bus driver, Core 0)
      session_mgr/          (TLS session state machines)
      crypto_engine/        (raw crypto API handlers)
      net_mgr/              (WiFi/Ethernet, DHCP, DNS)
      cert_store/           (CA bundle loading/validation)
      config_mgr/           (NVS and MicroSD config)
  dos/
    tsr/
      resident.asm          (NASM, INT handler + ISR + polling hook)
      transient.c           (Watcom C, installation logic)
    sdk/
      netisa.h            (public API header, portable C89)
      netisa.c            (Watcom C, INT call wrappers)
      netisa_tc.c         (Turbo C variant using int86x())
    diag/
      nisadiag.c            (Watcom C, diagnostic utility)
    wifi/
      cisawifi.c            (Watcom C, WiFi configuration utility)
    examples/
      http_get/             (minimal HTTPS GET, ~50 lines of C)
      irc_client/           (TLS IRC client skeleton)
      telegram/             (Telegram Bot API client)
      sha256sum/            (local file hashing utility)
      encrypt/              (file encryption/decryption tool)
    makefile                (wmake, builds all DOS targets)
  tools/
    mksdcard/               (Python script to prepare MicroSD with certs + config)
    mock_ioport/            (DOSBox-X I/O port mock for testing without hardware)
  LICENSE
  README.md
```

---

## 13. Community and Governance

- All code and hardware designs released under MIT license (or BSD 2-clause; TBD before v1). Hardware under CERN-OHL-P.
- Hardware contributions (alternate processor boards, PCI version) welcome as separate repos linking back to the INT API spec.
- The INT API specification is versioned independently of the firmware. Breaking changes require a major version bump and are avoided whenever possible.
- **Compatibility list (wiki):** Maintained from day one. Structured fields: machine model, CPU, chipset, ISA bus speed, BIOS version, card firmware version, result (works/partial/fails), failure description, reporter. This is the single most valuable community artifact for an ISA project. PicoGUS's compatibility list is the primary model.
- **JLCPCB production files** (BOM with LCSC part numbers + CPL placement file) included in every hardware release tag. This allows anyone to order assembled boards without manual component sourcing.
- Discussion and coordination via GitHub Discussions initially; dedicated forum if community grows. VOGONS thread for retro computing audience.

---

## 14. Open Questions

1. **INT vector selection:** 0x63 is believed to be unused by common DOS software. INT 67h is EMS (EMM386). INT 60h-66h are commonly listed as "user available" but some TSRs claim them. Needs community validation. Alternative: 0x6B.
2. **DPMI support:** Should the TSR provide DPMI-aware entry points for protected-mode DOS applications (DJGPP, DOS/4GW)? Adds complexity but expands audience. Deferred to v2 in current roadmap.
3. **License choice:** MIT vs. BSD 2-Clause for software, CERN-OHL-P for hardware. Need to pick a combination that covers both cleanly. CERN-OHL-P is specifically designed for open hardware and is compatible with MIT/BSD software licenses.
4. **Project name:** "NetISA" is a working title. Community input welcome.
5. **Maximum data transfer per call:** 64KB (segment limit) is the natural ceiling for real-mode. Is this sufficient for all use cases, or should a streaming/DMA mode be specified for v2?
6. **XT power budget validation:** Card draws ~350mA at 5V peak. Need hands-on verification on original IBM 5150/5160 PSUs (63.5W, ~2A on +5V rail shared with motherboard and all slots) with typical slot population (floppy controller, video card, this card). May need to document minimum PSU requirements.
7. **WiFi antenna placement:** U.FL pigtail to bracket-mounted antenna is the plan. Need to test whether a short pigtail inside the case to a bracket-mount RP-SMA jack provides adequate signal, or if a bracket-mounted PCB patch antenna is needed. Metal PC cases vary significantly in RF attenuation.
8. **Telegram as launch application:** The original design motivation was a DOS Telegram client. Should the v1 release include a reference Telegram Bot API client in the examples/ directory, or should this be a separate community project?
9. **Parallel bus contention during fast REP OUTSB:** On 286+ systems, REP OUTSB generates back-to-back I/O writes with only I/O recovery time between them. Need to verify that the CPLD can complete the parallel handshake to the ESP32 before the next ISA write arrives. If not, IOCHRDY may need to be asserted briefly on writes as well, not just reads. Requires prototyping.
10. **ESP32-S3 flash encryption:** NVS stores WiFi credentials unencrypted in flash. ESP32-S3 supports transparent flash encryption, but enabling it complicates firmware updates and debugging. Evaluate for v2.
11. **Standard packet driver interface (high-impact adoption question):** Should v1 or v1.5 include a PC/TCP Packet Driver Specification-compliant driver alongside the custom INT API? A packet driver (TSR on INT 0x60-0x80 exposing send_pkt/access_type/receive callbacks) would give instant compatibility with mTCP, WATTCP, and every existing DOS TCP/IP application without requiring NetISA-specific code. The card would appear as a standard Ethernet NIC for plaintext TCP/IP, while the custom INT API provides TLS/crypto offload for applications that need it. This "dual interface" approach (standard networking + crypto acceleration) dramatically increases adoption because users get value from day one with existing software. The cost: a second TSR (~4-6KB), more firmware complexity (Ethernet frame emulation in the ESP32), and a larger test surface. The research shows this pattern (trsnic, Apple2Idiot) is the strongest predictor of community adoption for retro network cards.
12. **ATF1508AS PLCC-84 EOL (RESOLVED):** The ATF1508AS-10JU84 (PLCC-84) is confirmed obsolete at DigiKey as of April 2026. All design work now targets the ATF1508AS-10AU100 (TQFP-100, 68 I/O pins, active production). The Verilog pin assignments must be updated for the TQFP-100 pinout before the fitter can be run. Phase 0 prototyping uses a TQFP-100 breakout board instead of a PLCC-84 socket.

---

## Appendix A: Prior Art and Inspiration

| Project | Relationship to NetISA |
|---------|------------------------|
| WiFi232 / RetroWiFi Modem | Hayes modem emulation over WiFi. Solves connectivity but not crypto. NetISA operates at a lower layer. |
| Snark Barker | Modern-component ISA sound card. Reference for ISA card design with KiCad and community engagement. |
| PicoGUS | RP2040-based ISA card (GUS emulation + NE2000 WiFi). Closest prior art for ISA+MCU+WiFi. IOCHRDY wait-state management directly applicable. |
| Graphics Gremlin | iCE40HX4K ISA video card. Complete ISA bus interface in Verilog with open-source toolchain. |
| Sergey Kiselev's ISA projects | Meticulously documented KiCad ISA card designs (Micro 8088, ISA Ethernet). PCB layout and BOM reference. |
| mTCP / WATTCP | DOS TCP/IP stacks. NetISA replaces or supplements these by handling the transport layer on-card. |
| MicroWeb | DOS web browser. Primary candidate application for NetISA HTTPS support. |
| ISASTM | ISA-over-USB adapter (STM32H743). ISA bus timing reference. |
| The DISAppointment | LPC-to-ISA bridge. ISA bus interface reference for modern-to-legacy bridging. |
| 8086 ISA Multiplier Card | ISA coprocessor concept validation. Published April 2026. Demonstrates community appetite for ISA accelerator cards. |
| trsnic (TRS-80) | ESP32 network card for TRS-80 with TLS. Different bus, similar crypto offload concept. |
| 5Vpld (github.com/peterzieba/5Vpld) | Scripts and tools for ATF150x and GAL programmable logic. Direct toolchain reference for the ATF1508AS CPLD. |
| CERBERUS (Tony's project) | DOS diagnostic utility targeting same era hardware. Potential integration for network diagnostics. |
| XT-IDE + XTIDE Universal BIOS | ISA IDE controller with option ROM BIOS. Demonstrates latch timing race conditions fixed only via logic analyzer, CHS semantic versioning warnings, and skip-init patterns. |
| BlueSCSI | RP2040/RP2350-based SCSI emulator. Demonstrates USB serial console diagnostics, crashlog analysis, and firmware recovery via SD card bootloader. |
| Lo-tech ISA CompactFlash | Discrete TTL ISA storage adapter. Reference for Slot 8 compatibility, option ROM integration, and detailed troubleshooting documentation. |

---

## Appendix B: Consolidated Lessons From Prior Retro Hardware Projects

This appendix distills actionable engineering rules from five mature retro hardware/software projects (PicoGUS, XT-IDE, mTCP, BlueSCSI, Lo-tech ISA CF) into a checklist for NetISA development. Each item references the originating project and the failure it prevents.

### B.1 ISA Bus Timing

| # | Rule | Source | Spec Section |
|---|------|--------|-------------|
| 1 | Never hold IOCHRDY low for more than 10us. Some chipsets (SiS, VIA) will release the bus early. Design all "long work" (flash erase, WiFi join, cert parsing) to be asynchronous with host-visible status polling. | PicoGUS: IOCHRDY abuse during flash | 2.4.3, 5.5 |
| 2 | Test at ISA bus speeds from 4.77 MHz to 12 MHz. Document a hard ceiling (likely 8.33 MHz for safe IOCHRDY timing). If the bus is faster than the ceiling, document it as unsupported, don't try to make it work. | PicoGUS: 8.33 MHz ceiling | 3.4, Phase 0 |
| 3 | Budget nanoseconds on paper, then verify with a logic analyzer. XT-IDE rev 3 had a latch timing race invisible in simulation, caught only by HP logic analyzer captures. | XT-IDE: rev 3-to-4 timing fix | Phase 0 |
| 4 | Filter RESET DRV with an RC circuit + Schmitt trigger. Add a weak pull-down for out-of-machine programming. | PicoGUS: v1.1 to v1.2 RESET fixes | 2.4.1 |
| 5 | Expect chipset-specific behavior. Intel 430-series is the gold standard. SiS and some VIA chipsets cause DMA/timing failures even with original vintage hardware. Document known-bad chipsets, don't try to fix them. | PicoGUS: compatibility list | 3.5, 3.8 |

### B.2 Host Software Model

| # | Rule | Source | Spec Section |
|---|------|--------|-------------|
| 6 | Build a single "truth tool" (NISADIAG) that detects card presence, reports firmware version, runs bus self-test, and validates configuration. Ship this before any application code. PicoGUS's PGUSINIT caught more problems than any other tool. | PicoGUS: PGUSINIT pattern | 6.8, Phase 1 |
| 7 | Implement firmware version negotiation in the TSR. Refuse to load if firmware major version mismatches. Warn if minor version is lower than TSR expects. XT-IDE's CHS translation semantic change corrupted user data when BIOS was upgraded without reformatting. | XT-IDE: CHS translation warning | 6.3 |
| 8 | Provide a "skip init" path: keypress (ESC) during loading skips TSR installation. Prevents boot loops when the card malfunctions and the TSR is in AUTOEXEC.BAT. | XT-IDE: key-to-skip-init | 6.3 |
| 9 | Clearly distinguish hardware IRQ from software interrupt in all documentation and error messages. mTCP's most common support issue is users confusing NIC hardware IRQ with packet driver software interrupt. NetISA has the same two-number problem (IRQ jumper vs INT vector). | mTCP: IRQ confusion | 6.3, docs |
| 10 | Treat host-visible API semantics as a versioned contract. Error codes, session handle meanings, and buffer formats must never change meaning between versions without a major version bump. | XT-IDE: CHS semantics | 4.1 |

### B.3 Hardware Design and Manufacturing

| # | Rule | Source | Spec Section |
|---|------|--------|-------------|
| 11 | Verify ISA edge connector finger-to-edge distance against the EISA specification (not other open-source projects). PicoGUS v1.1 inherited incorrect spacing, requiring a fix in v1.2. Measure 3+ vintage ISA cards with calipers to cross-reference. | PicoGUS: edge connector fixes | Phase 6 |
| 12 | Use HASL-with-lead for ISA edge connector surface finish. ENIG is too thin for repeated slot insertion. Hard gold is ideal but prohibitively expensive at JLCPCB. | PicoGUS: manufacturing | BOM |
| 13 | Include JLCPCB PCBA production files (BOM with LCSC part numbers + CPL placement file) in every hardware release. This dramatically lowers the barrier for community builds. | PicoGUS: production files | 12.8 |
| 14 | Test every component at actual operating conditions, not just datasheet nominal. PicoGUS lost months debugging a DAC silicon bug (10% of chips silent at 22.05 KHz) and PSRAM speed rating violations. | PicoGUS: PCM510xA, PSRAM | Phase 0, Phase 5 |
| 15 | Keep BOM, gerbers, KiCad files, and interactive BOM perfectly synchronized. PicoGUS had a cap labeled C9 on the board but C10 in the BOM, causing misplacement on DIY builds. | PicoGUS: v1.1.1 BOM fix | 12.8 |

### B.4 Diagnostics and Community

| # | Rule | Source | Spec Section |
|---|------|--------|-------------|
| 16 | Maintain a structured compatibility list (wiki page) from day one. Track: machine model, CPU, chipset, bus speed, BIOS version, result (works/partial/fails), and reporter. PicoGUS's compatibility list is its most valuable community artifact. | PicoGUS: compatibility list | 13 |
| 17 | Build extensive tracing into the firmware. mTCP's design goals explicitly prioritize tracing as a first-class feature for diagnosing user problems. For NetISA, the ESP32 should log ISA bus events, TLS handshake details, and error conditions to a ring buffer readable via NISADIAG or USB serial. | mTCP: tracing philosophy | 6.8, 5.8 |
| 18 | Provide a USB serial console for debugging during development. BlueSCSI's USB serial interface (accessible via screen or minicom) provides real-time firmware logs, crash logs, and diagnostic commands without requiring a DOS utility. The ESP32-S3's USB-JTAG peripheral provides this natively. | BlueSCSI: USB serial console | Phase 0 |
| 19 | Implement a firmware crashlog that survives reboot. BlueSCSI saves crash details to SD card for post-mortem analysis. NetISA should save the last panic backtrace to a reserved NVS partition or the MicroSD, readable by NISADIAG. | BlueSCSI: crashlog | 5.5 |
| 20 | Ship early, iterate often, document everything. PicoGUS's "perpetual beta" philosophy with meticulous issue tracking and wiki maintenance built a community that found and fixed bugs no single developer could have anticipated. Plan for 3+ hardware revisions before declaring v1 stable. | PicoGUS: open development | 13 |

---

*This document is a living specification. Feedback, corrections, and contributions are welcome via GitHub Issues.*
