# NetISA Phase 0 - Validation Checklist and Wiring Guide

## Overview

Phase 0 proves one thing: **a byte can survive a round trip through the ISA bus, CPLD, parallel interface, and ESP32-S3 on real vintage hardware.** Nothing else matters until this works.

## File Inventory

```
phase0/
  cpld/
    netisa.v             Verilog source for ATF1508AS (recommended, Quartus path)
    netisa.pld           CUPL source for ATF1508AS (alternative, WinCUPL path)
  firmware/
    CMakeLists.txt         ESP-IDF project file
    sdkconfig.defaults     Build configuration
    main/
      CMakeLists.txt       Component build file
      main.c               ESP32-S3 parallel bus handler
  dos/
    nisatest.asm           NASM source for DOS loopback test
  README.md                This file
```

## IMPORTANT: TQFP-100 Package

**The ATF1508AS-10JU84 (PLCC-84) is obsolete.** All Phase 0 work targets the **ATF1508AS-10AU100 (TQFP-100)**. This means:

- No PLCC socket. Use a TQFP-100 breakout/adapter board for prototyping.
- Pin numbers in `netisa.v` port comments are the Quartus fitter TQFP-100 assignments from `output_files/netisa.pin`. See `WIRING.md` for the complete pin mapping. (`netisa.pld` still has PLCC-84 numbers and is not maintained.)
- The Quartus target device is **EPM7128STC100-15** (TQFP-100 package), not EPM7128SLC84-15 (PLCC-84).
- JTAG programming via ATDH1150USB or FT232H JTAG adapter. TL866II+/T48 CANNOT program this chip.

## Build Instructions

### 1. CPLD (ATF1508AS)

**Option A: Quartus II 13.0sp1 + POF2JED (RECOMMENDED)**
```
1. Install Quartus II 13.0sp1 (free from Intel, includes MAX 7000 support)
2. Create project targeting EPM7128STC100-15 (TQFP-100, pin-compatible with ATF1508AS-10AU100)
3. Open netisa.v, remap pin assignments to TQFP-100 pinout (see ATF1508AS datasheet Table 3-5)
4. Compile, review fitter report and timing analysis
5. Generate .pof file
6. Run POF2JED (free from Microchip): convert .pof to .jed
   CRITICAL: Set JTAG=ON, TDI_PULLUP=ON, TMS_PULLUP=ON
   (Wrong settings can brick the chip, requiring 12V recovery)
7. Program ATF1508AS with .jed file via ATDH1150USB or FT232H JTAG
```

**Option B: WinCUPL II (alternative, provided CUPL source)**
```
1. Install WinCUPL II v1.1.0 from Microchip (free download)
2. Open netisa.pld
3. Device -> Select: f1508ispplcc84
4. Run -> Compile
5. Check .rpt file for macrocell usage and pin assignments
6. Output: netisa.jed (JEDEC programming file)
Note: WinCUPL IDE is unstable. Use command-line cupl.exe if GUI crashes.
```

**Programming the CPLD (JTAG only, TQFP-100 package):**
```
Using ATDH1150USB programmer (recommended):
  atmisp -> load netisa.jed -> program

Using Arduino JTAG (community tool):
  See github.com/peterzieba/5Vpld for instructions

NOTE: TL866II+ / T48 CANNOT program the TQFP-100 package.
      The PLCC-84 variant is obsolete. Use JTAG.
```

### 2. ESP32-S3 Firmware

```bash
# Set up ESP-IDF v5.x (if not already installed)
# See https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/

cd phase0/firmware

# Set target
idf.py set-target esp32s3

# Build
idf.py build

# Flash and monitor (USB-C cable to DevKit)
idf.py flash monitor

# Expected output:
#   NetISA Phase 0 - Bus Validation
#   Firmware v1.0.1
#   PBOOT asserted. Card ready for ISA transactions.
#   Waiting for ISA bus transactions...
```

### 3. DOS Test Program

```bash
# On a modern PC with NASM installed:
nasm -f bin -o nisatest.com phase0/dos/nisatest.asm

# Transfer NISATEST.COM to the test machine via:
#   - Floppy disk
#   - Serial transfer (e.g., Kermit)
#   - CF card / SD card adapter
#   - Network (if available)
```

## Wiring Guide

**The complete, authoritative wiring guide is in [`phase0/WIRING.md`](WIRING.md).** That document contains every signal-by-signal connection derived from the actual Quartus II fitter pin assignments for EPM7128STC100-15 (TQFP-100), including ISA bus routing through HCT245 buffers, ESP32 parallel bus through LVC8T245 level shifters, 74AHC14 Schmitt trigger wiring, oscillator, configuration jumpers, pull-up/pull-down resistor summary, decoupling capacitor placement, and a pre-power verification checklist.

**Do not reference the PLCC-84 pin numbers that appeared in earlier versions of this file.** Those were from the obsolete package. `WIRING.md` has the correct TQFP-100 pin numbers from `phase0/cpld/output_files/netisa.pin`.

Key safety reminders (see WIRING.md for full details):

- **Verify +5V with a multimeter before inserting the card.** Check resistance from ISA +5V to GND (must be > 1 kOhm, no dead short).
- **ISA ground and ESP32 DevKit ground MUST be connected** via a short 16-18 AWG wire.
- **Do NOT connect ESP32 GPIO19 or GPIO20 to anything.** They are USB D-/D+.
- **10K pull-ups on all buffer /OE pins are mandatory** for fail-safe during CPLD programming.
- **IOCHRDY is open-drain.** The CPLD emulates open-drain via tri-state. Add a 10K pull-up to +5V.

## Validation Checklist

### Gate 0: CPLD Fitter Report (BEFORE wiring anything)

- [ ] CUPL compiles without errors in WinCUPL
- [ ] Fitter report shows all logic fits in ATF1508AS
- [ ] Macrocell usage < 100 (out of 128)
- [ ] All pin assignments are valid I/O pins (not power/GND/JTAG)
- [ ] No timing violations at 16 MHz clock
- [ ] .jed file generated successfully
- [ ] CPLD programmed and verified

**STOP HERE if the fitter report shows problems. Fix the CUPL first.**

### Gate 1: Power and Clock

- [ ] ISA +5V present at CPLD VCC pins (measure with multimeter)
- [ ] CPLD draws < 100mA from ISA +5V rail
- [ ] 16 MHz oscillator running (verify with scope or logic analyzer on GCLK1)
- [ ] ESP32 DevKit boots and shows USB serial console output
- [ ] ISA GND and ESP32 GND are connected (verify continuity)

### Gate 2: Address Decode

- [ ] Logic analyzer shows chip_sel (TP0) assert on access to base address
- [ ] chip_sel does NOT assert on access to other addresses
- [ ] chip_sel does NOT assert during DMA cycles (AEN high)
- [ ] Changing DIP switch changes the active address range

### Gate 3: ISA Write Path

- [ ] DOS: `OUT 0x284, 0x55` - ESP32 serial console shows "Write reg=0x04 val=0x55"
- [ ] DOS: `OUT 0x284, 0xAA` - ESP32 console shows correct value
- [ ] Repeat for all 16 registers (base+0x00 through base+0x0F)
- [ ] ESP32 write counter increments with each OUT instruction

### Gate 4: ISA Read Path (cached)

- [ ] DOS: `IN 0x287` returns firmware major version (0x01)
- [ ] Read completes without IOCHRDY assertion (verify with logic analyzer)
- [ ] Status register (base+0x00) returns value with BOOT_COMPLETE bit set

### Gate 5: ISA Read Path (non-cached, IOCHRDY)

- [ ] DOS: `IN 0x284` (data port) triggers IOCHRDY assertion
- [ ] Logic analyzer shows IOCHRDY LOW for < 10us
- [ ] IOCHRDY releases cleanly (no glitches, registered transition)
- [ ] Read returns correct data from ESP32

### Gate 6: Loopback Test

- [ ] NISATEST.COM detects card
- [ ] Bus self-test (0x55/0xAA) passes
- [ ] Full 256-byte loopback test passes: 0 mismatches
- [ ] ESP32 console shows 256 writes and 256 reads

### Gate 7: IRQ

- [ ] ESP32 asserts PIRQ -> ISA IRQ line goes HIGH (logic analyzer)
- [ ] DOS ISR fires on IRQ assertion
- [ ] Status register read deasserts IRQ line
- [ ] Dead-time visible between deassert and reassert (>250ns)

### Gate 8: Cross-Machine Validation

Run Gates 1-7 on each machine:

| Machine | CPU | Result | Notes |
|---------|-----|--------|-------|
| XT clone | 8088 @ ?.?? MHz | | |
| AT / 286 | 286 @ ?? MHz | | |
| 486 system | 486 @ ?? MHz | | |

- [ ] All three machines pass full loopback test
- [ ] IOCHRDY timing is within spec on all machines
- [ ] No bus contention or glitches visible on logic analyzer
- [ ] PSU +5V stays above 4.5V during testing on all machines

### Gate 9: Stress Test

- [ ] Run loopback test 100 times in a loop (batch file) on each machine
- [ ] Zero failures across all runs
- [ ] ESP32 does not crash or reset during stress test
- [ ] No timing degradation over extended operation

## What Happens After Phase 0

If all gates pass: proceed to Phase 1 (TSR + command protocol).

If the fitter fails: simplify CUPL, remove features, try Quartus path, or evaluate ATF1504AS + external latches as fallback.

If loopback fails on specific machines: document the chipset, capture timing with logic analyzer, adjust IOCHRDY timing or add delays.

If the loopback fails on all machines: the parallel bus interface design needs revision. Check wiring, check clock, check level shifter direction, check CPLD programming verification.

## Estimated Time

- CPLD: 2-3 days (CUPL, fitter, debug, program)
- ESP32 firmware: 1 day (build, flash, verify serial output)
- DOS test program: 1 day (assemble, transfer, initial test)
- Wiring: 2-3 days (proto board, ribbon cables, testing continuity)
- Per-machine validation: 1-2 days each
- Total: ~2 weeks (evenings/weekends)
