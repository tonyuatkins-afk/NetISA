# NetISA Phase 0 Wiring Guide

Complete signal-by-signal wiring instructions for the NetISA Phase 0 hand-wired prototype. This document is authoritative for the prototype build and is derived directly from the Quartus II 13.0sp1 fitter pin assignments for `netisa.v` targeting `EPM7128STC100-15` (TQFP-100, pin-compatible with ATF1508AS-10AU100).

This guide is intended to be self-contained. With the parts list below and this document, someone can wire the entire prototype from scratch without referring to any other source.

---

## Warnings (read before wiring)

- **Verify +5V with a multimeter before inserting the card into the ISA slot.** Short-to-ground on the ISA +5V rail can destroy the chipset regulator on a vintage motherboard.
- **ESP32-S3 GPIO19 and GPIO20 are USB D-/D+. DO NOT connect anything to these pins.** They are used by the onboard USB-JTAG serial console, which is the primary debugging interface for Phase 0. The parallel bus uses GPIO4-GPIO18 and GPIO21/GPIO38 only.
- **IOCHRDY is open-drain.** The CPLD drives it LOW through `assign IOCHRDY = iochrdy_hold ? 1'b0 : 1'bz`; never drive it HIGH. The motherboard provides the pull-up; an additional 10K pull-up on the prototype is recommended for reliability.
- **ISA ground and ESP32 DevKit ground MUST be connected** with a short, heavy (16-18 AWG) wire. Without a common ground reference, the level shifters cannot function and signal integrity is undefined.
- **Do NOT connect the ESP32 DevKit 5V pin to the ISA +5V rail.** The DevKit is USB-powered; sharing +5V could backfeed the USB port or create a ground loop. Only share GND between the two domains.
- **10K pull-ups on all bus buffer /OE pins are mandatory.** When the CPLD is unprogrammed or being JTAG-programmed, its outputs float. Without pull-ups, buffers may enable in random directions and short ISA bus lines or ESP32 GPIO pins.
- **The ATF1508AS CPLD is 5V native.** It tolerates the full ISA bus +5V environment directly on its I/O pins without level shifters on the ISA side. Do not place level shifters between the ATF1508AS and the ISA bus.

---

## Parts list (Phase 0 prototype BOM)

| Qty | Part | Package | Purpose |
|-----|------|---------|---------|
| 1 | ATF1508AS-10AU100 | TQFP-100 on breakout board | CPLD (ISA bus logic) |
| 1 | ESP32-S3-DevKitC-1U-N8R8 | Module on breakout | Main processor (WiFi, crypto, TCP/IP) |
| 2 | SN74LVC8T245PWR | TSSOP-24 on PA0036 breakout | 5V to 3.3V level shifter (CPLD to ESP32) |
| 2 | SN74HCT245N | DIP-20 | ISA data bus buffer, address input buffer |
| 1 | SN74AHC14N | DIP-14 | Schmitt trigger inverter (RESET filter, PRW inversion, chip_sel inversion) |
| 1 | ECS-100AX-160 | DIP-14 full-can oscillator | 16 MHz system clock for CPLD |
| 1 | TexElec 8-bit ISA Prototype Card v1.0 | PCB | ISA edge connector breakout to 0.1" pin headers |
| 1 | Solderless breadboard (large) | | Prototyping surface for ICs and passives |
| ~16 | 100nF X7R ceramic capacitor | 0805 or leaded | Decoupling on every IC VCC pin |
| 1 | 470uF 16V electrolytic | leaded | Bulk decoupling on ISA +5V input |
| ~12 | 10K 1/4W resistor | leaded | Pull-ups and pull-downs |
| 1 | 100K 1/4W resistor | leaded | RESET DRV pull-down (out-of-machine operation) |
| 1 | 100nF X7R ceramic | 0805 or leaded | RESET DRV RC filter capacitor |
| many | Jumper wires, ribbon cables, 0.1" headers | | Connections |
| 5 | Jumper shunts (0.1" pitch) | | Configuration jumpers (ADDR_J[2:0], SAFE_MODE, IRQ_SENSE) |

---

## A. CPLD TQFP-100 pin assignment table

Complete pin map from `phase0/cpld/output_files/netisa.pin`. Every assigned pin on the ATF1508AS-10AU100 TQFP-100 package. All I/O signals are TTL level, 5V.

### Power and ground pins

| TQFP Pin | Function | Connection |
|---------:|----------|------------|
| 3 | VCCIO | +5V (bank 1), decouple with 100nF |
| 11 | GND | ISA ground |
| 18 | VCCIO | +5V (bank 2), decouple with 100nF |
| 26 | GND | ISA ground |
| 34 | VCCIO | +5V (bank 3), decouple with 100nF |
| 38 | GND | ISA ground |
| 39 | VCCINT | +5V core, decouple with 100nF |
| 43 | GND | ISA ground |
| 51 | VCCIO | +5V (bank 4), decouple with 100nF |
| 59 | GND | ISA ground |
| 66 | VCCIO | +5V (bank 5), decouple with 100nF |
| 74 | GND | ISA ground |
| 82 | VCCIO | +5V (bank 6), decouple with 100nF |
| 86 | GND | ISA ground |
| 91 | VCCINT | +5V core, decouple with 100nF |
| 95 | GND | ISA ground |
| 90 | GND+ | unused dedicated, tie to ISA ground |

**Total VCC pins**: 8 (6x VCCIO, 2x VCCINT); minimum 8x 100nF decoupling caps placed within 5mm of each VCC pin.

### JTAG programming header (reserved; do not use for signaling)

| TQFP Pin | Signal | Connection |
|---------:|--------|------------|
| 4 | TDI | JTAG header, 10K pull-up to +5V |
| 15 | TMS | JTAG header, 10K pull-up to +5V |
| 62 | TCK | JTAG header, no pull-up |
| 73 | TDO | JTAG header |

Wire all four to a 6-pin header for ATDH1150USB or FT232H JTAG programmer. Include VCC and GND pins on the header.

### ISA address bus inputs (A0-A15, direct from ISA bus via optional buffer)

| TQFP Pin | Signal | ISA Pin | ISA Label | Direction |
|---------:|--------|:-------:|-----------|-----------|
| 30 | A0  | A31 | SA0  | input |
| 80 | A1  | A30 | SA1  | input |
| 84 | A2  | A29 | SA2  | input |
| 50 | A3  | A28 | SA3  | input |
| 85 | A4  | A27 | SA4  | input |
| 27 | A5  | A26 | SA5  | input |
|  5 | A6  | A25 | SA6  | input |
| 16 | A7  | A24 | SA7  | input |
| 72 | A8  | A23 | SA8  | input |
| 94 | A9  | A22 | SA9  | input |
| 29 | A10 | A21 | SA10 | input |
|  7 | A11 | A20 | SA11 | input |
| 92 | A12 | A19 | SA12 | input |
| 79 | A13 | A18 | SA13 | input |
| 68 | A14 | A17 | SA14 | input |
| 98 | A15 | A16 | SA15 | input |

All 16 bits are used (full 16-bit decode, A15-A10 required LOW per spec section 2.2). Address inputs route from the TexElec card's ISA A-side header pins (SA0-SA15) into the CPLD, optionally through `HCT245 #2` for SA0-SA7 (see Section E below).

### ISA data bus (D0-D7, bidirectional via HCT245 #1)

| TQFP Pin | Signal | ISA Pin | ISA Label | Direction |
|---------:|--------|:-------:|-----------|-----------|
| 36 | D[0] | A9 | SD0 | bidir |
| 33 | D[1] | A8 | SD1 | bidir |
| 37 | D[2] | A7 | SD2 | bidir |
| 76 | D[3] | A6 | SD3 | bidir |
| 32 | D[4] | A5 | SD4 | bidir |
| 23 | D[5] | A4 | SD5 | bidir |
| 13 | D[6] | A3 | SD6 | bidir |
| 35 | D[7] | A2 | SD7 | bidir |

All 8 data bits pass through `HCT245 #1` (DIP-20, VCC=5V) for ESD protection and drive strength. See Section E below.

### ISA control signals

| TQFP Pin | Signal | ISA Pin | ISA Label | Direction |
|---------:|--------|:-------:|-----------|-----------|
| 67 | AEN_n | A11 | AEN | input (wired direct, see Note 1) |
| 44 | IOR_n | B14 | IOR# | input |
| 100 | IOW_n | B13 | IOW# | input |
| 31 | IOCHRDY | A10 | IOCHRDY | output (open-drain style, 10K pull-up) |
| 89 | RESET_n | B2 | RESET DRV | input via RC filter + 74AHC14 (see Section G) |
| 20 | IRQ_OUT | B21 | IRQ7 | output (hardwired to IRQ7 for Phase 0) |
| 64 | IOCS16_n |; |; | output, NOT CONNECTED for 8-bit slot |
| 87 | CLK |; |; | 16 MHz oscillator output (see Section F) |

**Note 1 (AEN polarity)**: The Verilog module input is named `AEN_n` but is wired directly to the ISA AEN signal (which is active HIGH on the ISA bus, HIGH during DMA cycles). The Verilog inverts it internally (`wire AEN = ~AEN_n;`) so that `chip_sel = base_match & AEN & upper_zero` is active during CPU cycles. **Connect the ISA AEN pin (A11) directly to CPLD pin 67. No external inverter needed.**

### ESP32 parallel bus (PD, PA, PRW, PSTROBE, PREADY, PIRQ, PBOOT)

| TQFP Pin | Signal | ESP32 GPIO | Through | Direction | Notes |
|---------:|--------|:----------:|---------|-----------|-------|
| 42 | PD[0] | GPIO4 | LVC8T245 #1 | bidir | Data bit 0 |
| 46 | PD[1] | GPIO5 | LVC8T245 #1 | bidir | Data bit 1 |
| 48 | PD[2] | GPIO6 | LVC8T245 #1 | bidir | Data bit 2 |
| 60 | PD[3] | GPIO7 | LVC8T245 #1 | bidir | Data bit 3 |
| 57 | PD[4] | GPIO8 | LVC8T245 #1 | bidir | Data bit 4 |
| 54 | PD[5] | GPIO9 | LVC8T245 #1 | bidir | Data bit 5 |
|  6 | PD[6] | GPIO10 | LVC8T245 #1 | bidir | Data bit 6 |
|  9 | PD[7] | GPIO11 | LVC8T245 #1 | bidir | Data bit 7 |
|  8 | PA[0] | GPIO12 | LVC8T245 #2 | CPLD→ESP32 | Register address bit 0 |
| 14 | PA[1] | GPIO13 | LVC8T245 #2 | CPLD→ESP32 | Register address bit 1 |
| 97 | PA[2] | GPIO14 | LVC8T245 #2 | CPLD→ESP32 | Register address bit 2 |
| 96 | PA[3] | GPIO15 | LVC8T245 #2 | CPLD→ESP32 | Register address bit 3 |
| 24 | PRW   | GPIO16 | LVC8T245 #2 | CPLD→ESP32 | Read/write direction (+ drives LVC #1 DIR via 74AHC14) |
| 52 | PSTROBE | GPIO17 | LVC8T245 #2 | CPLD→ESP32 | Transaction strobe (active low) |
| 58 | PREADY | GPIO18 | **direct wire** | ESP32→CPLD | 3.3V into 5V TTL input is safe (see Note 2) |
| 56 | PIRQ   | GPIO38 | **direct wire** | ESP32→CPLD | 3.3V into 5V TTL input is safe |
| 28 | PBOOT  | GPIO21 | **direct wire** | ESP32→CPLD | 3.3V into 5V TTL input is safe |

**Note 2 (no level shifter for ESP32-to-CPLD direction)**: The ATF1508AS uses 5V CMOS I/O cells with TTL-compatible input thresholds. VIH ≈ 2.0V, well below the ESP32's 3.3V output high (~3.0V). A 3.3V ESP32 GPIO output drives a 5V CPLD input directly with margin, so PREADY, PIRQ, and PBOOT do not need level shifters. The CPLD-to-ESP32 direction DOES need level shifting because the CPLD's 5V output would exceed the ESP32 GPIO absolute maximum of 3.6V.

### Configuration jumpers

| TQFP Pin | Signal | Jumper | Default (Phase 0) |
|---------:|--------|--------|-------------------|
| 45 | ADDR_J[0] | 3-pos DIP switch, pin 1 | OFF (open → pulled HIGH) |
| 25 | ADDR_J[1] | 3-pos DIP switch, pin 2 | OFF (open → pulled HIGH) |
| 83 | ADDR_J[2] | 3-pos DIP switch, pin 3 | OFF (open → pulled HIGH) |
| 65 | SAFE_MODE | 2-pin header with shunt | OPEN (pulled HIGH via 10K) |
| 88 | IRQ_SENSE | 2-pin header with shunt | SHUNT INSTALLED (HIGH, IRQ enabled) |
| 78 | SLOT16_n | tied permanently | HIGH (+5V, 8-bit slot mode) |

For ADDR_J, each switch pulls the CPLD pin to GND when ON (LOW = 0). OFF state is pulled HIGH by a 10K resistor to +5V. All three switches OFF = base address 0x340 per the decode table; see spec section 2.3.1. For Phase 0 loopback test, use all OFF (base 0x340) or all ON (base 0x280) as preferred.

### Test points

| TQFP Pin | Signal | Purpose |
|---------:|--------|---------|
| 19 | TP0 | `chip_sel` (internal); also drives HCT245 #1 OE# via 74AHC14 inverter (see Section E) |
| 99 | TP1 | `iochrdy_hold` (internal); logic analyzer observation only |

Both test points should be brought out to a 2-pin header on the prototype board for easy logic analyzer probe attachment.

---

## B. ISA bus to CPLD connections

Table of every ISA edge connector pin used by NetISA Phase 0, with the physical path from the TexElec prototype card pin header to the CPLD. All signals are 5V TTL unless noted.

### Address lines (SA0-SA15)

Lower 8 address lines (SA0-SA7) route through **HCT245 #2** as an input buffer. Upper 8 lines (SA8-SA15) route direct from the TexElec card pin headers to the CPLD pins listed below.

| ISA Pin | ISA Signal | Path | CPLD Pin |
|:-------:|------------|------|---------:|
| A31 | SA0 | TexElec → HCT245 #2 A1 → B1 → CPLD | 30 |
| A30 | SA1 | TexElec → HCT245 #2 A2 → B2 → CPLD | 80 |
| A29 | SA2 | TexElec → HCT245 #2 A3 → B3 → CPLD | 84 |
| A28 | SA3 | TexElec → HCT245 #2 A4 → B4 → CPLD | 50 |
| A27 | SA4 | TexElec → HCT245 #2 A5 → B5 → CPLD | 85 |
| A26 | SA5 | TexElec → HCT245 #2 A6 → B6 → CPLD | 27 |
| A25 | SA6 | TexElec → HCT245 #2 A7 → B7 → CPLD |  5 |
| A24 | SA7 | TexElec → HCT245 #2 A8 → B8 → CPLD | 16 |
| A23 | SA8  | TexElec → direct → CPLD | 72 |
| A22 | SA9  | TexElec → direct → CPLD | 94 |
| A21 | SA10 | TexElec → direct → CPLD | 29 |
| A20 | SA11 | TexElec → direct → CPLD |  7 |
| A19 | SA12 | TexElec → direct → CPLD | 92 |
| A18 | SA13 | TexElec → direct → CPLD | 79 |
| A17 | SA14 | TexElec → direct → CPLD | 68 |
| A16 | SA15 | TexElec → direct → CPLD | 98 |

### Data lines (SD0-SD7)

All 8 data lines pass through **HCT245 #1** (bidirectional).

| ISA Pin | ISA Signal | Path | CPLD Pin |
|:-------:|------------|------|---------:|
| A9 | SD0 | TexElec ↔ HCT245 #1 A1 ↔ B1 ↔ CPLD | 36 |
| A8 | SD1 | TexElec ↔ HCT245 #1 A2 ↔ B2 ↔ CPLD | 33 |
| A7 | SD2 | TexElec ↔ HCT245 #1 A3 ↔ B3 ↔ CPLD | 37 |
| A6 | SD3 | TexElec ↔ HCT245 #1 A4 ↔ B4 ↔ CPLD | 76 |
| A5 | SD4 | TexElec ↔ HCT245 #1 A5 ↔ B5 ↔ CPLD | 32 |
| A4 | SD5 | TexElec ↔ HCT245 #1 A6 ↔ B6 ↔ CPLD | 23 |
| A3 | SD6 | TexElec ↔ HCT245 #1 A7 ↔ B7 ↔ CPLD | 13 |
| A2 | SD7 | TexElec ↔ HCT245 #1 A8 ↔ B8 ↔ CPLD | 35 |

### Control signals

| ISA Pin | ISA Signal | Path | CPLD Pin |
|:-------:|------------|------|---------:|
| A11 | AEN     | TexElec → direct → CPLD | 67 (labeled `AEN_n`, inverted internally) |
| B14 | IOR#    | TexElec → direct → CPLD (also → HCT245 #1 DIR) | 44 |
| B13 | IOW#    | TexElec → direct → CPLD | 100 |
| A10 | IOCHRDY | TexElec ↔ direct ↔ CPLD (with 10K pull-up to +5V) | 31 |
| B2  | RESET DRV | TexElec → RC filter → 74AHC14 → CPLD | 89 (labeled `RESET_n`) |
| B21 | IRQ7    | TexElec ← direct ← CPLD | 20 (labeled `IRQ_OUT`) |

### Power and ground

| ISA Pin | ISA Signal | Connection |
|:-------:|------------|------------|
| B3 | +5V | Bulk 470uF, then distribute to all 5V VCC rails (CPLD VCCIO/VCCINT, HCT245 #1 VCC, HCT245 #2 VCC, 74AHC14 VCC, oscillator VCC, LVC8T245 VCCA) |
| B29 | +5V | Second +5V rail (tie to B3 via short wire, optional redundant) |
| B1 | GND | Common ground rail |
| B10 | GND | Common ground rail |
| B31 | GND | Common ground rail |

**All three +5V and three GND pins on the ISA connector should be tied to their respective power rails for drive strength and noise margin.** Do not skip this: vintage motherboards expect balanced current draw across the ISA power pins.

### ISA pins NOT USED for Phase 0

| ISA Pin | Signal | Disposition |
|:-------:|--------|-------------|
| B4 | IRQ2/9 | No connect |
| B5 | -5V | No connect |
| B6 | DRQ2 | No connect |
| B7 | -12V | No connect |
| B8 | 0WS# (NOWS) | No connect (do not drive; per research report, NOWS must never be asserted with IOCHRDY) |
| B9 | +12V | No connect |
| B11 | SMEMW# | No connect (NetISA is I/O-mapped, not memory-mapped) |
| B12 | SMEMR# | No connect |
| B15 | DACK3# | No connect |
| B16 | DRQ3 | No connect |
| B17 | DACK1# | No connect |
| B18 | DRQ1 | No connect |
| B19 | REFRESH# | No connect |
| B20 | BCLK | No connect (CPLD uses its own 16 MHz oscillator, not ISA BCLK) |
| B22 | IRQ6 | No connect |
| B23 | IRQ5 | No connect |
| B24 | IRQ4 | No connect |
| B25 | IRQ3 | No connect |
| B26 | DACK2# | No connect |
| B27 | T/C | No connect |
| B28 | BALE | No connect |
| B30 | OSC | No connect (ISA 14.318 MHz OSC, not used) |
| A1 | IOCHK# | No connect (do not assert; asserting IOCHK# triggers NMI on AT+ systems) |
| A12 | SA19 | No connect (address is purely I/O, max 16 bits) |
| A13 | SA18 | No connect |
| A14 | SA17 | No connect |
| A15 | SA16 | No connect |

---

## C. ESP32 parallel bus to CPLD connections

The ESP32-S3-DevKitC-1U-N8R8 sits on a breadboard powered by USB. Ground is shared with the ISA rail. Power and signal wiring between CPLD and ESP32 is entirely through level shifters or (for ESP32→CPLD signals) direct wires.

### ESP32 DevKit power and ground

| DevKit Pin | Connection |
|------------|------------|
| 5V (USB) | **DO NOT CONNECT** to ISA +5V. Powered from USB only. |
| 3V3 | LVC8T245 #1 VCCB, LVC8T245 #2 VCCB, 100nF decoupling on each VCCB |
| GND (any) | Common ground rail (connect to ISA GND with 16-18 AWG wire) |

### Bidirectional data bus (PD0-PD7) via LVC8T245 #1

LVC8T245 #1 handles the 8-bit bidirectional data bus. Direction is controlled by an inverted `PRW` signal from the CPLD (see Section D).

| Signal | CPLD Pin | LVC8T245 #1 | ESP32 GPIO |
|--------|---------:|-------------|:----------:|
| PD[0] | 42 | A1 ↔ B1 | GPIO4 |
| PD[1] | 46 | A2 ↔ B2 | GPIO5 |
| PD[2] | 48 | A3 ↔ B3 | GPIO6 |
| PD[3] | 60 | A4 ↔ B4 | GPIO7 |
| PD[4] | 57 | A5 ↔ B5 | GPIO8 |
| PD[5] | 54 | A6 ↔ B6 | GPIO9 |
| PD[6] |  6 | A7 ↔ B7 | GPIO10 |
| PD[7] |  9 | A8 ↔ B8 | GPIO11 |

### Unidirectional CPLD→ESP32 control signals via LVC8T245 #2

LVC8T245 #2 carries 6 signals in the fixed CPLD→ESP32 direction (A→B). Two channels remain unused.

| Signal | CPLD Pin | LVC8T245 #2 | ESP32 GPIO |
|--------|---------:|-------------|:----------:|
| PA[0]    |  8 | A1 → B1 | GPIO12 |
| PA[1]    | 14 | A2 → B2 | GPIO13 |
| PA[2]    | 97 | A3 → B3 | GPIO14 |
| PA[3]    | 96 | A4 → B4 | GPIO15 |
| PRW      | 24 | A5 → B5 | GPIO16 |
| PSTROBE  | 52 | A6 → B6 | GPIO17 |
| (unused) | ; | A7 → B7 | no connect |
| (unused) | ; | A8 → B8 | no connect |

Tie A7 and A8 inputs to GND to prevent floating.

### Unidirectional ESP32→CPLD signals via DIRECT WIRES (no level shifter)

The CPLD accepts 3.3V logic at its TTL inputs, so these signals do not need shifting.

| Signal | ESP32 GPIO | CPLD Pin | Notes |
|--------|:----------:|---------:|-------|
| PREADY | GPIO18 | 58 | ESP32 asserts HIGH to signal data valid |
| PIRQ   | GPIO38 | 56 | ESP32 asserts HIGH to request interrupt |
| PBOOT  | GPIO21 | 28 | ESP32 asserts HIGH on firmware boot complete |

### Critical: GPIO19 and GPIO20 RESERVED FOR USB

| GPIO | Function | Rule |
|------|----------|------|
| GPIO19 | USB D- | **DO NOT CONNECT anything. Leave completely floating.** |
| GPIO20 | USB D+ | **DO NOT CONNECT anything. Leave completely floating.** |

These pins carry the USB-JTAG serial console, which is the primary debugging interface for all firmware development. Any connection will break `idf.py monitor` and prevent firmware flashing.

---

## D. Level shifter wiring (SN74LVC8T245 x 2)

Both level shifters are SN74LVC8T245PWR on PA0036 TSSOP-24 breakout boards. Pin labels (A1-A8, B1-B8, DIR, OE#, VCCA, VCCB, GND) are printed on the breakout board and are referenced directly below.

### LVC8T245 #1: bidirectional data bus (PD0-PD7)

| Pin | Connection |
|-----|------------|
| VCCA | +5V (from ISA power rail) |
| VCCB | +3.3V (from ESP32 DevKit 3V3 pin) |
| GND | common ground rail |
| DIR | **driven by 74AHC14 Gate 2 output** (inverted PRW; see Section G) |
| OE# | GND (always enabled for Phase 0) |
| A1 | CPLD pin 42 (PD[0]) |
| A2 | CPLD pin 46 (PD[1]) |
| A3 | CPLD pin 48 (PD[2]) |
| A4 | CPLD pin 60 (PD[3]) |
| A5 | CPLD pin 57 (PD[4]) |
| A6 | CPLD pin 54 (PD[5]) |
| A7 | CPLD pin 6  (PD[6]) |
| A8 | CPLD pin 9  (PD[7]) |
| B1 | ESP32 GPIO4  |
| B2 | ESP32 GPIO5  |
| B3 | ESP32 GPIO6  |
| B4 | ESP32 GPIO7  |
| B5 | ESP32 GPIO8  |
| B6 | ESP32 GPIO9  |
| B7 | ESP32 GPIO10 |
| B8 | ESP32 GPIO11 |

**Decoupling**: 100nF X7R on VCCA, 100nF X7R on VCCB. Place within 3mm of each supply pin.

**Why DIR is inverted PRW**: The Verilog drives PRW HIGH when the ISA host is reading (`assign PRW = IOR & chip_sel;`). When PRW=1, the ESP32 is expected to drive PD (so the CPLD can latch and forward to the ISA bus). Data flow is ESP32→CPLD = B→A on LVC8T245 = DIR must be LOW. When PRW=0, the CPLD drives PD (forwarding a host-written byte to the ESP32). Data flow is CPLD→ESP32 = A→B = DIR must be HIGH. So DIR = `!PRW`, produced by one channel of the 74AHC14 inverter.

### LVC8T245 #2: unidirectional CPLD to ESP32 control signals

| Pin | Connection |
|-----|------------|
| VCCA | +5V (from ISA power rail) |
| VCCB | +3.3V (from ESP32 DevKit 3V3 pin) |
| GND | common ground rail |
| DIR | **+5V** (tied HIGH, fixed A→B direction) |
| OE# | GND (always enabled) |
| A1 | CPLD pin 8  (PA[0]) |
| A2 | CPLD pin 14 (PA[1]) |
| A3 | CPLD pin 97 (PA[2]) |
| A4 | CPLD pin 96 (PA[3]) |
| A5 | CPLD pin 24 (PRW) |
| A6 | CPLD pin 52 (PSTROBE) |
| A7 | GND (unused input, prevent floating) |
| A8 | GND (unused input, prevent floating) |
| B1 | ESP32 GPIO12 |
| B2 | ESP32 GPIO13 |
| B3 | ESP32 GPIO14 |
| B4 | ESP32 GPIO15 |
| B5 | ESP32 GPIO16 |
| B6 | ESP32 GPIO17 |
| B7 | no connect |
| B8 | no connect |

**Decoupling**: 100nF X7R on VCCA, 100nF X7R on VCCB.

**Note on PRW routing**: PRW appears TWICE in the wiring because it has two jobs. The first copy (from CPLD pin 24 → 74AHC14 Gate 2 input) is inverted to drive the LVC8T245 #1 DIR control. The second copy (from CPLD pin 24 → LVC8T245 #2 A5 → B5 → ESP32 GPIO16) is level-shifted so the ESP32 ISR can read the current read/write state. Run one wire from CPLD pin 24 to both destinations (74AHC14 pin 3 and LVC8T245 #2 A5).

---

## E. ISA bus buffer wiring (SN74HCT245N x 2)

Both are SN74HCT245N in DIP-20 packages. Pinout:

```
                ┌──────┐
         DIR  1 │      │ 20  VCC
          A1  2 │      │ 19  OE#
          A2  3 │      │ 18  B1
          A3  4 │      │ 17  B2
          A4  5 │      │ 16  B3
          A5  6 │      │ 15  B4
          A6  7 │      │ 14  B5
          A7  8 │      │ 13  B6
          A8  9 │      │ 12  B7
         GND 10 │      │ 11  B8
                └──────┘
```

### HCT245 #1: ISA data bus buffer (D0-D7, bidirectional)

| DIP Pin | Function | Connection |
|--------:|----------|------------|
| 20 | VCC | +5V (ISA rail) |
| 10 | GND | common ground |
| 1  | DIR | CPLD pin 44 (IOR_n); **direct wire from ISA IOR#** |
| 19 | OE# | 74AHC14 Gate 3 output (pin 6); **inverted TP0 / chip_sel**; also 10K pull-up to +5V (fail-safe) |
| 2  | A1  | TexElec card SD0 header pin (ISA A9) |
| 3  | A2  | TexElec card SD1 header pin (ISA A8) |
| 4  | A3  | TexElec card SD2 header pin (ISA A7) |
| 5  | A4  | TexElec card SD3 header pin (ISA A6) |
| 6  | A5  | TexElec card SD4 header pin (ISA A5) |
| 7  | A6  | TexElec card SD5 header pin (ISA A4) |
| 8  | A7  | TexElec card SD6 header pin (ISA A3) |
| 9  | A8  | TexElec card SD7 header pin (ISA A2) |
| 18 | B1  | CPLD pin 36 (D[0]) |
| 17 | B2  | CPLD pin 33 (D[1]) |
| 16 | B3  | CPLD pin 37 (D[2]) |
| 15 | B4  | CPLD pin 76 (D[3]) |
| 14 | B5  | CPLD pin 32 (D[4]) |
| 13 | B6  | CPLD pin 23 (D[5]) |
| 12 | B7  | CPLD pin 13 (D[6]) |
| 11 | B8  | CPLD pin 35 (D[7]) |

**Decoupling**: 100nF X7R on VCC (pin 20), within 5mm.

**Direction logic**:
- During ISA write (IOR#=1, IOW#=0): DIR=1 → A→B direction → ISA bus drives HCT245 → CPLD captures data. Correct.
- During ISA read (IOR#=0, IOW#=1): DIR=0 → B→A direction → CPLD drives HCT245 → drives ISA bus. Correct.
- Direction control comes DIRECTLY from the ISA bus IOR# signal through a single jumper wire to pin 1 of HCT245 #1. No inversion needed.

**OE# logic**:
- When TP0 (chip_sel) = 0 (card not addressed): 74AHC14 Gate 3 output = 1 → OE#=1 → buffer disabled. ISA bus not driven.
- When TP0 = 1 (card addressed): 74AHC14 Gate 3 output = 0 → OE#=0 → buffer enabled.
- The 10K pull-up to +5V on OE# (pin 19) ensures the buffer defaults to DISABLED if the 74AHC14 output is unavailable (e.g., during CPLD programming when TP0 floats, or if the 74AHC14 is missing).
- Additionally, place a **10K pull-down on CPLD pin 19 (TP0)** to GND so that if the CPLD is unprogrammed, TP0 floats LOW, the 74AHC14 gate outputs HIGH, and OE# is HIGH (disabled). Belt-and-suspenders fail-safe.

### HCT245 #2: ISA address line input buffer (SA0-SA7, unidirectional)

| DIP Pin | Function | Connection |
|--------:|----------|------------|
| 20 | VCC | +5V (ISA rail) |
| 10 | GND | common ground |
| 1  | DIR | **+5V** (tied HIGH, fixed A→B direction) |
| 19 | OE# | **GND** (always enabled); 10K pull-up to +5V as fail-safe |
| 2  | A1  | TexElec card SA0 header pin (ISA A31) |
| 3  | A2  | TexElec card SA1 header pin (ISA A30) |
| 4  | A3  | TexElec card SA2 header pin (ISA A29) |
| 5  | A4  | TexElec card SA3 header pin (ISA A28) |
| 6  | A5  | TexElec card SA4 header pin (ISA A27) |
| 7  | A6  | TexElec card SA5 header pin (ISA A26) |
| 8  | A7  | TexElec card SA6 header pin (ISA A25) |
| 9  | A8  | TexElec card SA7 header pin (ISA A24) |
| 18 | B1  | CPLD pin 30 (A0) |
| 17 | B2  | CPLD pin 80 (A1) |
| 16 | B3  | CPLD pin 84 (A2) |
| 15 | B4  | CPLD pin 50 (A3) |
| 14 | B5  | CPLD pin 85 (A4) |
| 13 | B6  | CPLD pin 27 (A5) |
| 12 | B7  | CPLD pin 5  (A6) |
| 11 | B8  | CPLD pin 16 (A7) |

**Decoupling**: 100nF X7R on VCC (pin 20).

**Rationale for buffering A0-A7 only**: The low 8 bits of the I/O address (SA0-SA7) change on every I/O cycle, presenting the highest load on the ISA bus and the longest wiring between the TexElec card and the breadboard. The upper 8 address lines (SA8-SA15) change less frequently and go direct to the CPLD. If signal integrity issues appear on any upper address line during bringup, move that line through a spare HCT245 channel; but this is not expected on a short prototype run.

---

## F. 16 MHz oscillator wiring (ECS-100AX-160)

The ECS-100AX-160 is a 14-pin DIP full-can crystal oscillator. It produces a 16 MHz TTL-level clock output directly on pin 8. Pinout:

| Pin | Function |
|----:|----------|
| 1 | Tri-state / enable (active HIGH or NC, depending on variant; tie to VCC for always-on) |
| 7 | GND |
| 8 | OUT (16 MHz TTL) |
| 14 | VCC (+5V) |
| Others | No connect |

### Wiring

| Oscillator Pin | Connection |
|:-------------:|------------|
| 1 | +5V (enable always) |
| 7 | common ground |
| 8 | CPLD pin 87 (CLK) |
| 14 | +5V |

**Decoupling**: 100nF X7R on pin 14 (VCC), placed within 5mm. Additionally, add a 10uF tantalum or electrolytic near the oscillator for low-frequency supply rejection if bringup reveals clock jitter.

**Routing**: Keep the trace from oscillator pin 8 to CPLD pin 87 short (< 5 cm) and away from the ISA data bus and the HCT245 data buffer to minimize crosstalk. If possible, use a short shielded or twisted-pair wire for this trace.

**Verification**: After powering on, verify the 16 MHz clock on CPLD pin 87 with an oscilloscope or logic analyzer. Look for a clean square wave at 16.000 MHz with approximately 50% duty cycle, rise/fall times under 10 ns, and amplitude swinging 0 to +5V.

---

## G. Schmitt trigger / inverter wiring (SN74AHC14N)

The SN74AHC14N is a hex Schmitt trigger inverter in DIP-14. It provides three functions in the NetISA Phase 0 prototype:

1. **Gate 1**: RESET DRV filtering and inversion (ISA → CPLD RESET_n)
2. **Gate 2**: PRW inversion for LVC8T245 #1 DIR control
3. **Gate 3**: chip_sel (TP0) inversion for HCT245 #1 OE# control

Gates 4, 5, 6 are unused. Tie their inputs to GND to prevent floating and noise.

### SN74AHC14N DIP-14 pinout

| Pin | Function |
|----:|----------|
| 1 | 1A (Gate 1 input) |
| 2 | 1Y (Gate 1 output) |
| 3 | 2A (Gate 2 input) |
| 4 | 2Y (Gate 2 output) |
| 5 | 3A (Gate 3 input) |
| 6 | 3Y (Gate 3 output) |
| 7 | GND |
| 8 | 4Y (Gate 4 output, unused; no connect) |
| 9 | 4A (Gate 4 input, unused; tie to GND) |
| 10 | 5Y (Gate 5 output, unused; no connect) |
| 11 | 5A (Gate 5 input, unused; tie to GND) |
| 12 | 6Y (Gate 6 output, unused; no connect) |
| 13 | 6A (Gate 6 input, unused; tie to GND) |
| 14 | VCC (+5V) |

### Gate 1: RESET DRV filter and inversion

```
ISA B2 (RESET DRV) ──┬── 10K series R ──┬── 74AHC14 pin 1 (1A input)
                     │                  │
                     │                  └── 100nF cap to GND
                     │
                     └── 100K resistor to GND (out-of-machine pull-down)

74AHC14 pin 2 (1Y output) ── CPLD pin 89 (RESET_n)
```

The 10K series resistor + 100nF to GND forms an RC low-pass filter with ~1ms time constant. This rejects sub-millisecond glitches on the RESET DRV line that have been observed on ALi M6117D and AMD Slot-A systems. The 74AHC14 Schmitt trigger converts the slow RC-filtered edge into a clean fast edge with hysteresis, and inverts it (since the CPLD input is labeled RESET_n and expects active LOW for reset). The 100K pull-down ensures RESET DRV defaults to LOW (no reset) when the card is not installed in a motherboard, allowing bench-top testing.

### Gate 2: PRW inversion for LVC8T245 #1 DIR

```
CPLD pin 24 (PRW) ──┬── LVC8T245 #2 A5 (for routing to ESP32 GPIO16)
                    │
                    └── 74AHC14 pin 3 (2A input)

74AHC14 pin 4 (2Y output) ── LVC8T245 #1 DIR pin (inverted PRW)
```

Produces `DIR = !PRW` so the LVC8T245 #1 data bus level shifter flows in the correct direction on every bus cycle. See Section D for the rationale.

### Gate 3: chip_sel (TP0) inversion for HCT245 #1 OE#

```
CPLD pin 19 (TP0 / chip_sel) ──┬── Test point header (for logic analyzer probe)
                               │
                               ├── 10K pull-down to GND (fail-safe when CPLD unprogrammed)
                               │
                               └── 74AHC14 pin 5 (3A input)

74AHC14 pin 6 (3Y output) ── HCT245 #1 OE# (pin 19)
                          └── 10K pull-up to +5V on HCT245 pin 19
```

Produces `OE# = !chip_sel` so the data bus buffer enables only when the card is addressed. Both the 10K pull-down on TP0 and the 10K pull-up on HCT245 OE# ensure the buffer is SAFELY DISABLED whenever the CPLD cannot actively drive TP0.

**Note**: TP0 also serves as a logic analyzer test point. When probing TP0 with a 10:1 passive probe (>10 MΩ input impedance), no observable effect on the buffer OE signal. Active probes are equally safe. Do not short TP0 to GND or +5V while the card is live.

### Power and unused pins

| Pin | Connection |
|----:|------------|
| 14 (VCC) | +5V |
| 7 (GND) | common ground |
| 9 (4A), 11 (5A), 13 (6A) | tie to GND |
| 8 (4Y), 10 (5Y), 12 (6Y) | no connect |

**Decoupling**: 100nF X7R on pin 14, within 5mm.

---

## H. Configuration jumpers

Five user-settable jumpers and one tie-high configuration input. All use 0.1" pin headers.

### Base address DIP switch (ADDR_J[2:0])

3-position DIP switch, one position per address bit. Each switch shorts the corresponding CPLD pin to GND when ON (selecting bit = LOW). When OFF, the 10K pull-up pulls the pin HIGH (bit = 1).

```
                    +5V
                     │
                     ├── 10K ──┬── CPLD pin 45 (ADDR_J[0]) ── SW1 ── GND
                     │         │
                     ├── 10K ──┼── CPLD pin 25 (ADDR_J[1]) ── SW2 ── GND
                     │         │
                     └── 10K ──┴── CPLD pin 83 (ADDR_J[2]) ── SW3 ── GND
```

Switch encoding (from spec section 2.3.1):

| SW3 | SW2 | SW1 | ADDR_J | Base |
|:---:|:---:|:---:|:------:|:----:|
| ON  | ON  | ON  | 000 | 0x280 |
| ON  | ON  | OFF | 001 | 0x290 |
| ON  | OFF | ON  | 010 | 0x2A0 |
| ON  | OFF | OFF | 011 | 0x2C0 |
| OFF | ON  | ON  | 100 | 0x300 |
| OFF | ON  | OFF | 101 | 0x310 |
| OFF | OFF | ON  | 110 | 0x320 |
| OFF | OFF | OFF | 111 | 0x340 |

**Phase 0 recommendation**: start with all switches ON (base 0x280) to match the Verilog testbench and the DOS loopback test expectations.

### SAFE_MODE jumper

2-pin header with a jumper shunt. Installed = tied to GND (LOW, SAFE_MODE off). Removed = pulled HIGH via 10K (SAFE_MODE on, sets status register bit 3).

```
+5V ── 10K ──┬── CPLD pin 65 (SAFE_MODE)
             │
             └── 2-pin header ── GND (jumper installed = LOW)
```

**Phase 0 recommendation**: leave the shunt installed (SAFE_MODE = LOW = off).

### IRQ_SENSE jumper

2-pin header with a jumper shunt. Installed = tied to +5V (HIGH, IRQ driving enabled). Removed = pulled LOW via 10K (IRQ output tri-state).

```
+5V ──┬── 2-pin header ── CPLD pin 88 (IRQ_SENSE)
      │                    │
      │                    └── 10K to GND
      │
      (jumper installed = HIGH)
```

**Phase 0 recommendation**: leave the shunt installed (IRQ enabled). Without IRQ_SENSE = HIGH, the CPLD `assign IRQ_OUT = IRQ_SENSE ? ... : 1'bz;` tri-states the IRQ output, which is fine for polling-mode testing but will prevent the Gate 7 IRQ validation test.

### SLOT16_n (permanent tie, not a jumper)

For the 8-bit TexElec proto card, there is no IOCS16# pin on the ISA connector. Tie CPLD pin 78 (SLOT16_n) permanently to +5V via a direct wire. This makes the CPLD treat the card as 8-bit-only and the IOCS16_n output stays tri-stated.

```
+5V ── CPLD pin 78 (SLOT16_n)
```

---

## I. Power distribution

### 5V distribution (from ISA edge connector)

```
ISA B3 (+5V)  ──┬── 470uF bulk electrolytic ── GND
ISA B29 (+5V) ──┤
                │
                ├── 100nF ── CPLD VCCIO pin 3
                ├── 100nF ── CPLD VCCIO pin 18
                ├── 100nF ── CPLD VCCIO pin 34
                ├── 100nF ── CPLD VCCIO pin 51
                ├── 100nF ── CPLD VCCIO pin 66
                ├── 100nF ── CPLD VCCIO pin 82
                ├── 100nF ── CPLD VCCINT pin 39
                ├── 100nF ── CPLD VCCINT pin 91
                │
                ├── 100nF ── HCT245 #1 pin 20 (VCC)
                ├── 100nF ── HCT245 #2 pin 20 (VCC)
                │
                ├── 100nF ── 74AHC14 pin 14 (VCC)
                │
                ├── 100nF ── oscillator pin 14 (VCC)
                │
                ├── 100nF ── LVC8T245 #1 VCCA
                └── 100nF ── LVC8T245 #2 VCCA
```

**Bulk decoupling**: the 470uF electrolytic should be placed at the ISA edge connector side of the prototype, as close to ISA pin B3 as physically possible. This absorbs current transients from CPLD switching activity and prevents droop on the ISA +5V rail. A 16V or higher voltage rating is required.

**Local decoupling**: one 100nF X7R ceramic within 5mm of every VCC pin of every IC. Total: approximately 16 decoupling capacitors (8 for CPLD, 2 for HCT245 x2, 1 for 74AHC14, 1 for oscillator, 2 for LVC8T245 VCCA x2).

### 3.3V distribution (from ESP32 DevKit)

```
ESP32 DevKit 3V3 pin ──┬── 100nF ── LVC8T245 #1 VCCB
                       │
                       └── 100nF ── LVC8T245 #2 VCCB
```

Only the LVC8T245 B-side requires 3.3V. Do NOT distribute 3.3V to any other component. Do NOT connect ESP32 DevKit 5V to ISA +5V.

### Ground distribution

```
ISA B1, B10, B31 (GND) ──┬── common ground rail
                         │
ESP32 DevKit GND        ─┤
                         │
                         ├── CPLD GND pins (11, 26, 38, 43, 59, 74, 86, 90, 95)
                         ├── HCT245 #1 pin 10
                         ├── HCT245 #2 pin 10
                         ├── 74AHC14 pin 7
                         ├── Oscillator pin 7
                         ├── LVC8T245 #1 GND
                         └── LVC8T245 #2 GND
```

The common ground rail should be a single low-impedance plane (copper fill on a PCB, or a dense star-ground pattern on a breadboard). **Use 16-18 AWG wire for the ISA-to-ESP32 ground bridge.** This is the most important single wire in the entire prototype and the one most likely to cause flaky behavior if done wrong.

### +5V verification procedure (DO BEFORE INSERTING THE CARD)

1. With the card OUT of the ISA slot, set a multimeter to resistance mode.
2. Measure resistance from ISA B3 (+5V) to ISA B1 (GND). Expected: > 1 kΩ (no dead short).
3. Measure resistance from every other IC VCC pin to GND. All should read > 1 kΩ.
4. If any pin reads < 100 Ω, stop and find the short before powering on.
5. Only after all resistance checks pass, insert the card into the ISA slot with the host powered OFF, then power on the host.
6. Within 10 seconds of power-on, measure +5V on the CPLD VCCINT pins (39 and 91) with a multimeter. Must be **between 4.75V and 5.25V**. If below 4.5V the PSU is sagging; test on a different machine with a stronger PSU.

---

## J. Pull-up and pull-down resistor summary

All resistors are 10K 1/4W unless otherwise noted.

| Resistor | From | To | Purpose |
|----------|------|----|---------|
| 10K | CPLD pin 45 (ADDR_J[0]) | +5V | Default HIGH when DIP switch open |
| 10K | CPLD pin 25 (ADDR_J[1]) | +5V | Default HIGH when DIP switch open |
| 10K | CPLD pin 83 (ADDR_J[2]) | +5V | Default HIGH when DIP switch open |
| 10K | CPLD pin 65 (SAFE_MODE) | +5V | Default HIGH when jumper shunt removed |
| 10K | CPLD pin 88 (IRQ_SENSE) | GND | Default LOW when jumper shunt removed |
| 10K | CPLD pin 31 (IOCHRDY) | +5V | Open-drain pull-up for reliable HIGH when not driven |
| 10K | CPLD pin 19 (TP0) | GND | Fail-safe LOW when CPLD unprogrammed |
| 10K | CPLD pin 4 (TDI) | +5V | JTAG idle-high per ATF1508AS datasheet |
| 10K | CPLD pin 15 (TMS) | +5V | JTAG idle-high per ATF1508AS datasheet |
| 10K | HCT245 #1 pin 19 (OE#) | +5V | Fail-safe disable when 74AHC14 unavailable |
| 10K | HCT245 #2 pin 19 (OE#) | +5V | Fail-safe disable (redundant with direct-GND tie) |
| **100K** | ISA B2 (RESET DRV) | GND | Out-of-machine bench testing (keeps RESET LOW when not in slot) |

Total: 11x 10K and 1x 100K resistor.

---

## K. Decoupling capacitor summary

| Qty | Value | Location |
|----:|-------|----------|
| 1 | 470uF electrolytic | ISA +5V bulk, at edge connector |
| 8 | 100nF X7R | CPLD VCCIO x6 + VCCINT x2 |
| 2 | 100nF X7R | HCT245 #1 VCC, HCT245 #2 VCC |
| 1 | 100nF X7R | 74AHC14 VCC |
| 1 | 100nF X7R | Oscillator VCC |
| 2 | 100nF X7R | LVC8T245 #1 VCCA, LVC8T245 #2 VCCA |
| 2 | 100nF X7R | LVC8T245 #1 VCCB, LVC8T245 #2 VCCB |
| 1 | 100nF X7R | RESET DRV RC filter cap |

Total: 1x 470uF + 17x 100nF.

Place every 100nF within 5mm of the pin it decouples. The PA0036 breakout boards for the LVC8T245 may already include on-board decoupling; verify on the breakout silkscreen before adding extra caps.

---

## L. Test points

Two test points are exposed on the CPLD for logic analyzer observation during bringup.

| Signal | CPLD Pin | Physical Access | Monitors |
|--------|---------:|-----------------|----------|
| TP0 | 19 | 2-pin header (shared with 74AHC14 Gate 3 input) | `chip_sel`; asserted HIGH when card is addressed |
| TP1 | 99 | 2-pin header | `iochrdy_hold`; asserted HIGH when CPLD is holding IOCHRDY LOW |

Bring both TP pins to a labeled 0.1" header on the prototype board. A 4-pin header with TP0, GND, TP1, GND (ground-signal-ground-signal layout) is ideal for clean logic analyzer probing.

### Logic analyzer channel assignment recommendation

For a typical 16-channel or 24-channel logic analyzer, the following channel assignment makes the Phase 0 bringup debugging productive:

| Channel | Signal | Source Pin |
|--------:|--------|-----------:|
| 0 | CLK (16 MHz) | CPLD pin 87 |
| 1 | IOR# | ISA B14 / CPLD pin 44 |
| 2 | IOW# | ISA B13 / CPLD pin 100 |
| 3 | AEN | ISA A11 / CPLD pin 67 |
| 4 | chip_sel (TP0) | CPLD pin 19 |
| 5 | iochrdy_hold (TP1) | CPLD pin 99 |
| 6 | IOCHRDY | ISA A10 / CPLD pin 31 |
| 7 | IRQ_OUT | CPLD pin 20 |
| 8 | PSTROBE | CPLD pin 52 |
| 9 | PREADY | CPLD pin 58 |
| 10 | PRW | CPLD pin 24 |
| 11 | PBOOT | CPLD pin 28 |
| 12-15 | PA[0..3] | CPLD pins 8, 14, 97, 96 |
| 16-23 | SD0-SD7 (bonus) | HCT245 #1 A-side or ISA A2-A9 |

The 5 MHz or 10 MHz sample rate is sufficient for capturing ISA bus cycles; 100 MHz is ideal for observing parallel bus timing on the ESP32 side.

---

## Build and validation checklist

Work through these gates IN ORDER. Do not proceed past a failed gate.

### Before powering on

- [ ] All 8 CPLD GND pins wired to common ground
- [ ] All 8 CPLD VCC pins wired to +5V with individual 100nF decoupling
- [ ] 470uF bulk cap on ISA +5V, polarity verified
- [ ] SLOT16_n (CPLD pin 78) tied to +5V
- [ ] ADDR_J DIP switch installed with 10K pull-ups
- [ ] SAFE_MODE and IRQ_SENSE jumper headers installed
- [ ] 16 MHz oscillator wired per Section F
- [ ] RESET DRV filter (10K + 100nF + 74AHC14 + 100K pull-down) wired per Section G Gate 1
- [ ] HCT245 #1 installed and wired per Section E (data bus)
- [ ] HCT245 #2 installed and wired per Section E (address buffer)
- [ ] 74AHC14 installed, all 3 used gates wired, unused inputs (pins 9, 11, 13) tied to GND
- [ ] LVC8T245 #1 installed and wired per Section D, PRW inverter connected
- [ ] LVC8T245 #2 installed and wired per Section D, DIR tied HIGH
- [ ] ESP32 DevKit positioned on breadboard with GND bridged to ISA GND via 16-18 AWG wire
- [ ] ESP32 DevKit 3V3 pin wired to both LVC8T245 VCCB pins
- [ ] ESP32 GPIO19 and GPIO20 floating (not connected to anything)
- [ ] All 10K pull-ups and pull-downs installed per Section J
- [ ] All 17 decoupling capacitors installed per Section K
- [ ] TP0 and TP1 broken out to a header for logic analyzer
- [ ] JTAG header wired for programming the CPLD

### Pre-power checks

- [ ] Multimeter resistance from ISA +5V (B3) to GND: > 1 kΩ
- [ ] Multimeter resistance from CPLD VCCINT (pin 39 and pin 91) to GND: > 1 kΩ
- [ ] Multimeter resistance from HCT245 VCC, 74AHC14 VCC, oscillator VCC, LVC8T245 VCCA each to GND: > 1 kΩ
- [ ] Multimeter continuity: ISA GND ↔ ESP32 DevKit GND: < 1 Ω
- [ ] Multimeter continuity: ESP32 3V3 ↔ both LVC8T245 VCCB pins: < 1 Ω
- [ ] No visible solder bridges on breakout boards or breadboard
- [ ] CPLD .jed file is flashed and verified via JTAG (done before first power-on of the ISA bus)

### Power on

- [ ] Insert card into ISA slot with host PC powered OFF
- [ ] Power on the host
- [ ] Within 5 seconds, measure +5V on CPLD VCCINT pin 39: between 4.75V and 5.25V
- [ ] Within 5 seconds, measure +3.3V on LVC8T245 VCCB: between 3.15V and 3.45V
- [ ] Logic analyzer shows 16 MHz clock on CPLD CLK (pin 87): clean square wave, 50% duty
- [ ] ESP32 DevKit USB serial console shows "NetISA Phase 0 - Bus Validation" boot banner
- [ ] ESP32 serial console shows "PBOOT asserted. Card ready for ISA transactions."
- [ ] Measure CPLD pin 28 (PBOOT input from ESP32): HIGH (3.3V or higher)
- [ ] No smoke, no smell, no heat above room temperature + 10°C on any IC

### Functional validation

Once all pre-power and power-on checks pass, proceed to the 9-gate validation checklist in `phase0/README.md` starting at Gate 0.

---

## Revision history

| Date | Change |
|------|--------|
| 2026-04-11 | Initial version generated from Quartus II 13.0sp1 fitter pin assignments. `netisa.pin` timestamp: 2026-04-11, fit target EPM7128STC100-15, 95/128 macrocells, 61/84 pins. |

## References

- Pin assignments: `phase0/cpld/output_files/netisa.pin`
- Architecture spec: `docs/netisa-architecture-spec.md` sections 2.2, 2.4, 2.13, 5.3
- Phase 0 overview and validation gates: `phase0/README.md`
- Verilog source: `phase0/cpld/netisa.v`
- Testbench: `phase0/cpld/netisa_tb.v` (160/160 tests passing)
- CPLD programming file: `phase0/cpld/output_files/netisa.jed`
