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
- Pin numbers in netisa.v and netisa.pld are currently PLCC-84 assignments. **These MUST be remapped to TQFP-100 pin numbers from the ATF1508AS datasheet before running the fitter.** The logical design is identical; only the physical pin assignments change.
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

**Programming the CPLD:**
```
Using ATDH1150USB programmer:
  atmisp -> load netisa.jed -> program

Using TL866II+ / T48:
  Insert ATF1508AS in PLCC-84 ZIF adapter
  Select device: ATF1508AS
  Load .jed, program, verify

Using Arduino JTAG (community tool):
  See github.com/peterzieba/5Vpld for instructions
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

## Wiring Guide: Prototype Setup

### Power

The ESP32-S3 DevKit is powered via USB (separate from ISA bus).
The ATF1508AS is powered from the ISA bus +5V rail.
The SN74LVC8T245 level shifter bridges the two voltage domains.

```
ISA +5V ----[470uF cap]----+---- ATF1508AS VCC (pins 8,22,34,48,60,72)
                            |
                            +---- SN74LVC8T245 VCCA (5V side)
                            |
                            +---- 100nF bypass caps (one per VCC pin)

ISA GND --------------------+---- ATF1508AS GND (pins 10,20,32,44,56,68,80)
                            |
                            +---- SN74LVC8T245 GND
                            |
                            +---- ESP32 DevKit GND (MUST connect ISA GND to DevKit GND)

ESP32 DevKit 3V3 -----------+---- SN74LVC8T245 VCCB (3.3V side)
                            |
                            +---- 100nF bypass cap
```

**CRITICAL: ISA ground and ESP32 DevKit ground MUST be connected.** Without a common ground reference, the level shifter cannot function and signal integrity is undefined. Run a short, thick wire (16-18 AWG) between ISA bus GND and DevKit GND.

**IMPORTANT: Add 10K pull-ups on all buffer /OE pins.** When the CPLD is unprogrammed or being JTAG-programmed, its outputs float. Without pull-ups, bus buffers may enable in random directions, potentially shorting ISA bus lines or ESP32 GPIO. Connect a 10K resistor from each 74HCT245 /OE pin and each SN74LVC8T245 /OE pin to their respective VCC (5V for HCT, 3.3V for LVC). The CPLD drives /OE low during normal operation, overriding the pull-up.

### ISA Bus to CPLD (directly wired to ISA edge connector)

**WARNING: CPLD pin numbers below are for the OBSOLETE PLCC-84 package.** Before wiring, remap all CPLD pin numbers to the TQFP-100 pinout using the ATF1508AS datasheet (Table 3-5, "100-Lead TQFP Pinout"). The ISA bus pin assignments (column 1) and ESP32 GPIO numbers are correct and do not change. Only the CPLD physical pin numbers change between packages.

```
ISA Pin    Signal      CPLD Pin (PLCC-84)   CPLD Pin (TQFP-100)
-------    ------      ------------------   --------------------
A31/B31    A0          4                    TBD (remap from datasheet)
A30/B30    A1          5                    TBD
A29/B29    A2          6                    TBD
A28/B28    A3          9                    TBD
A27/B27    A4          11                   TBD
A26/B26    A5          12                   TBD
A25/B25    A6          13                   TBD
A24/B24    A7          15                   TBD
A23/B23    A8          16                   TBD
A22/B22    A9          17                   TBD
A9/B9      D0          18                   TBD
A8/B8      D1          19                   TBD
A7/B7      D2          21                   TBD
A6/B6      D3          23                   TBD
A5/B5      D4          24                   TBD
A4/B4      D5          25                   TBD
A3/B3      D6          27                   TBD
A2/B2      D7          28                   TBD
B8         AEN         29                   TBD
B14        IOR#        30                   TBD
B13        IOW#        31                   TBD
B10        IOCHRDY     33                   TBD
B2         RESET DRV   35 (through RC)      TBD
```

### CPLD to ESP32-S3 (through SN74LVC8T245 level shifter)

**Data bus (PD0-PD7): CPLD -> SN74LVC8T245 A-side -> B-side -> ESP32 GPIO**
```
CPLD Pin   Signal   LVC8T245 A    LVC8T245 B    ESP32 GPIO
--------   ------   ----------    ----------    ----------
39         PD0      A1            B1            GPIO4
40         PD1      A2            B2            GPIO5
41         PD2      A3            B3            GPIO6
42         PD3      A4            B4            GPIO7
43         PD4      A5            B5            GPIO8
45         PD5      A6            B6            GPIO9
46         PD6      A7            B7            GPIO10
47         PD7      A8            B8            GPIO11
```

**Control signals: direct wire (CPLD 5V outputs, but ESP32 reads 3.3V via LVC8T245 or second LVC8T245)**

For Phase 0 prototype, you can use a second SN74LVC8T245 for the control signals, or use individual level shifters. The simplest approach for prototyping:

```
CPLD Pin   Signal     ESP32 GPIO   Notes
--------   ------     ----------   -----
49         PA0        GPIO12       Through level shifter
50         PA1        GPIO13       Through level shifter
51         PA2        GPIO14       Through level shifter
52         PA3        GPIO15       Through level shifter
53         PRW        GPIO16       Through level shifter
54         PSTROBE    GPIO17       Through level shifter (critical timing)
55         PREADY     GPIO18       ESP32 output -> CPLD input (3.3V OK for 5V CMOS)
57         PIRQ       GPIO38       ESP32 output -> CPLD input (3.3V OK for 5V CMOS)
58         PBOOT      GPIO21       ESP32 output -> CPLD input (3.3V OK for 5V CMOS)
```

**CRITICAL: GPIO19 and GPIO20 are USB D-/D+ on ESP32-S3.** Do NOT connect anything to these pins. They are used by the USB-JTAG serial console, which is the primary debugging interface. PIRQ uses GPIO38 instead.

**Why 3.3V ESP32 outputs drive 5V CPLD inputs directly:** The ATF1508AS uses 5V CMOS logic with a VIH threshold of 2.0V. The ESP32-S3's 3.3V output high is well above this threshold. No level shifting needed for ESP32-to-CPLD direction.

### Configuration Inputs (directly to CPLD)

```
CPLD Pin   Signal      Connection
--------   ------      ----------
59         ADDR_J0     DIP switch to GND (ON=LOW) with 10K pull-up to 5V
61         ADDR_J1     DIP switch to GND with 10K pull-up
62         ADDR_J2     DIP switch to GND with 10K pull-up
63         SAFE_MODE   Jumper to 5V (HIGH=safe) or GND via 10K pull-down
64         IRQ_SENSE   Jumper to 5V (HIGH=IRQ enabled) or GND via pull-down
65         SLOT16      Connect to ISA pin B1 (IOCS16 detect). Pull HIGH (5V)
                       for 8-bit-only operation during Phase 0.
```

### 16 MHz Oscillator

```
Oscillator Pin 14 (VCC) -> 5V + 100nF bypass cap
Oscillator Pin 7  (GND) -> GND
Oscillator Pin 8  (OUT) -> CPLD Pin 83 (GCLK1)
Oscillator Pin 1  (NC)  -> No connect (or enable, check datasheet)
```

### RESET DRV Filter

```
ISA RESET DRV (B2) --[10K resistor]--+--[100nF cap to GND]--+
                                      |                      |
                                      +-- 74AHC14 input -----+
                                           |
                                      74AHC14 output -> CPLD Pin 35

100K pull-down resistor from RESET DRV to GND (for out-of-machine operation)
```

### IRQ Output (directly from CPLD to ISA)

For Phase 0, pick one IRQ and hard-wire:
```
CPLD Pin 36 (IRQ_OUT) -> ISA pin corresponding to your chosen IRQ:
  IRQ5: ISA pin B21
  IRQ7: ISA pin B23
  IRQ3: ISA pin B25
```

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
