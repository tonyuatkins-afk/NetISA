# NetISA Phase 0 Bring-Up Playbook

## Purpose

This document tells you exactly what to look for on the logic analyzer during hardware bring-up. Open it on your laptop next to DSView. Each capture has a trigger condition, expected waveform, and "if wrong, check this" guidance.

## Logic Analyzer Setup (DSLogic Plus)

### Channel Assignment (16 channels)

| Channel | Signal | CPLD Pin | Probe Point | Color |
|---------|--------|----------|-------------|-------|
| 0 | CLK | 87 | Oscillator output | White |
| 1 | IOR# | 44 | ISA slot B14 | Yellow |
| 2 | IOW# | 100 | ISA slot B13 | Yellow |
| 3 | AEN | 67 | ISA slot A11 | Orange |
| 4 | TP0 (chip_sel) | 19 | CPLD pin 19 | Green |
| 5 | TP1 (iochrdy_hold) | 99 | CPLD pin 99 | Red |
| 6 | IOCHRDY | 31 | ISA slot A10 | Red |
| 7 | IRQ_OUT | 20 | CPLD pin 20 | Magenta |
| 8 | PSTROBE | 52 | CPLD pin 52 | Cyan |
| 9 | PREADY | 58 | CPLD pin 58 | Cyan |
| 10 | PRW | 24 | CPLD pin 24 | Blue |
| 11 | PBOOT | 28 | CPLD pin 28 | Blue |
| 12 | D0 | 36 | CPLD pin 36 | Gray |
| 13 | D1 | 33 | CPLD pin 33 | Gray |
| 14 | D2 | 37 | CPLD pin 37 | Gray |
| 15 | D3 | 76 | CPLD pin 76 | Gray |

### DSView Settings

- Sample rate: 10 MHz (sufficient for 16 MHz CPLD clock, catches all bus events)
- Threshold: 2.5V (TTL levels)
- Pre-trigger: 20%
- Post-trigger: 80%
- Buffer: maximum available

If you need D4-D7 visibility, swap channels 12-15 to D4-D7 for that capture session.


## Pre-Power Checks (No Logic Analyzer Needed)

Before connecting power, verify with a multimeter:

### Resistance Checks (power off, card NOT in slot)

| Between | Expected | If wrong |
|---------|----------|----------|
| Any VCC to GND | > 1K ohm | Short circuit. Find and fix before powering. |
| ISA GND to ESP32 GND | < 1 ohm | Ground wire missing. Add it. |
| CPLD pin 87 (CLK) to oscillator out | < 5 ohm | Clock not connected. |
| Every buffer OE# to VCC | ~10K ohm | Pull-up resistor missing. |

### First Power-On (card in slot, ESP32 USB connected)

| Measurement | Expected | If wrong |
|-------------|----------|----------|
| ISA +5V rail | 4.75V - 5.25V | PSU weak or short on card. Remove card immediately. |
| ESP32 3.3V pin | 3.15V - 3.45V | ESP32 USB power problem. |
| Level shifter VCCA | 4.75V - 5.25V | 5V not reaching level shifter. |
| Level shifter VCCB | 3.15V - 3.45V | 3.3V not reaching level shifter. |
| CPLD VCC (any pin) | 4.75V - 5.25V | 5V not reaching CPLD. Check traces. |
| Current draw from ISA +5V | < 100mA | Excessive draw. Check for shorts. |


## Capture 1: Clock and Idle State

**Goal:** Verify the oscillator runs and all signals are in their expected idle state.

**Trigger:** None (free run), or trigger on CLK rising edge.

**Expected idle state:**

```
CLK       __|^^|__|^^|__|^^|__|^^|__|^^|__|^^|__|^^|__    16 MHz square wave
IOR#      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^    HIGH (inactive)
IOW#      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^    HIGH (inactive)
AEN       ____________________________________________    LOW (CPU, not DMA)
TP0       ____________________________________________    LOW (not selected)
TP1       ____________________________________________    LOW (not holding)
IOCHRDY   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^    HIGH (pulled up, not driven)
IRQ_OUT   ____________________________________________    LOW (no interrupt)
PSTROBE   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^    HIGH (idle)
PREADY    ____________________________________________    LOW (ESP32 not responding)
PRW       ____________________________________________    LOW (no read)
PBOOT     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^    HIGH (if ESP32 booted)
D0-D3     xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx    Floating (hi-Z)
```

**If CLK is flat:** Oscillator not running. Check VCC to oscillator, check solder joints, try a different oscillator.

**If PBOOT is LOW:** ESP32 hasn't booted. Check USB serial console for crash output. Check PBOOT wire from ESP32 GPIO21 to CPLD pin 28.

**If IOCHRDY is LOW:** Something is holding the bus. Card should NOT assert IOCHRDY when idle. Check that the CPLD is programmed. Check for solder bridges on IOCHRDY pin.

**If IRQ_OUT is HIGH:** Spurious interrupt. Check IRQ_SENSE jumper. Check PIRQ wire from ESP32 GPIO38 to CPLD pin 56.


## Capture 2: Cached Register Read (Status Register)

**Goal:** Verify a read from base+0x00 completes without wait states.

**DOS command:** `DEBUG` then `I 280` (or your base address)

**Trigger:** Falling edge on IOR# (channel 1)

**Expected waveform (one bus cycle, ~375ns at 8 MHz ISA bus):**

```
Time        0ns    125ns   250ns   375ns   500ns   625ns
            |       |       |       |       |       |
CLK       __|^^|__|^^|__|^^|__|^^|__|^^|__|^^|__|^^|__
IOR#      ^^^^|___________________________|^^^^^^^^^^^^   LOW for ~3 BCLK
IOW#      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^   stays HIGH
AEN       _____________________________________________   stays LOW (CPU cycle)
TP0       ____|^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^|______   HIGH while addressed
TP1       _____________________________________________   stays LOW (no wait!)
IOCHRDY   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^   stays HIGH (no wait!)
PSTROBE   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^   stays HIGH (cached, no ESP32 access)
PREADY    _____________________________________________   stays LOW
PRW       ____|^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^|______   HIGH (read direction)
D0-D3     xxxx|====== STATUS BYTE ==============|xxxxxx   driven by CPLD
```

**Key checks:**
- TP1 (iochrdy_hold) must stay LOW the entire time. No wait states for cached reads.
- PSTROBE must stay HIGH. The CPLD serves this from its internal latch without talking to the ESP32.
- D0-D7 should show the status byte. Bit 6 (BOOT_COMPLETE) should be 1 if ESP32 has booted. Bit 3 (SAFE_MODE) matches the jumper.

**If TP1 goes HIGH:** Address decode is routing this to a non-cached path. Check that you're reading base+0x00, not a different register.

**If PSTROBE pulses LOW:** The CPLD is treating this as a cache miss. Check cache_hit logic. May be an address decode issue.

**If D0-D7 are all 0xFF:** Bus is floating. CPLD is not driving data. Check HCT245 buffer direction (IOR# to DIR), check OE#.

**If D0-D7 are all 0x00 and bit 6 is 0:** Stuck-low bus. CPLD may not be programmed. NISATEST.COM will catch this.


## Capture 3: Non-Cached Register Read with IOCHRDY Wait States

**Goal:** Verify IOCHRDY wait-state insertion and ESP32 response handshake.

**DOS command:** `DEBUG` then `I 281` (base+0x01, non-cached register)

**Trigger:** Falling edge on TP1 (channel 5)

**Expected waveform:**

```
Time        0      250ns    500ns    1us      2us      3us      4us      5us
            |       |        |       |        |        |        |        |
CLK       __|^^|__|^^|__|^^|__|^^|__|^^|__|^^|__|^^|__|^^|__|^^|__|^^|__
IOR#      ^^^^|__________________________________________________________________|^^
TP0       ____|^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^|__
TP1       ________|^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^|__________
IOCHRDY   ^^^^^^^^|________________________________________|^^^^^^^^^^^^^^^^^^^^^^^^
PSTROBE   ^^^^^^^^|__|^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^     pulse LOW
PRW       ____|^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^|__
PREADY    _______________________________________|^^^^^^|___________________________
D0-D3     xxxx|============================= RESPONSE BYTE =================|xxxxxx
```

**Sequence of events:**
1. IOR# falls, CPLD decodes address, chip_sel (TP0) goes HIGH
2. Cache miss detected, iochrdy_hold (TP1) goes HIGH, IOCHRDY driven LOW
3. PSTROBE pulses LOW for one CLK period (62.5ns), telling ESP32 "read requested"
4. PRW goes HIGH (read direction)
5. ESP32 ISR fires, reads register address from PA[3:0], fetches data
6. ESP32 drives data onto PD[7:0] and asserts PREADY HIGH
7. CPLD's 2-flop synchronizer captures PREADY (2 CLK delay = 125ns)
8. CPLD latches PD[7:0] into isa_out_latch, drives D[7:0], releases IOCHRDY
9. CPU completes the read cycle

**Expected IOCHRDY hold time:** 1-5us depending on ESP32 ISR latency. Must be < 10us (watchdog) and < 15.6us (NMI timeout on AT systems).

**If IOCHRDY never releases:** ESP32 is not responding. Check:
- ESP32 serial console for errors
- PSTROBE reaching ESP32 GPIO17
- PREADY wire from ESP32 GPIO18 to CPLD pin 58
- ESP32 firmware flashed correctly

**If IOCHRDY releases but D0-D7 are wrong:** Level shifter direction issue. Check LVC8T245 DIR pin. Should be driven by inverted PRW.

**If IOCHRDY hold time is > 10us:** Watchdog fired. Check ESP32 ISR performance. May need to optimize firmware response time.


## Capture 4: ISA Write to Data Port

**Goal:** Verify write path from ISA bus through CPLD to ESP32.

**DOS command:** `DEBUG` then `O 284 55` (write 0x55 to base+0x04)

**Trigger:** Falling edge on IOW# (channel 2)

**Expected waveform:**

```
Time        0ns    125ns   250ns   375ns   500ns   625ns   750ns
            |       |       |       |       |       |       |
CLK       __|^^|__|^^|__|^^|__|^^|__|^^|__|^^|__|^^|__|^^|__
IOR#      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^    stays HIGH
IOW#      ^^^^|___________________________|^^^^^^^^^^^^^^^^    LOW for ~3 BCLK
TP0       ____|^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^|__________    HIGH while addressed
PSTROBE   ^^^^^^^^^^^^^^^^^^^^^^^^^|__|^^^^^^^^^^^^^^^^^^^^^^^^   pulse LOW on IOW rising edge
PRW       _________________________________________________    LOW (write direction)
D0-D3     xxxx|======= 0x55 (0101) ========|xxxxxxxxxxxxxxx    host drives data
```

**Key checks:**
- PSTROBE should pulse LOW shortly after the rising edge of IOW# (write_strobe = iow_rising & chip_sel)
- PRW should be LOW (write direction, not read)
- D0-D3 should show 0101 binary (0x55 = 01010101, low nibble 0101)
- ESP32 serial console should print: "Write reg=0x04 val=0x55"

**If PSTROBE doesn't pulse:** write_strobe is not firing. Check IOW# reaching CPLD pin 100. Check that chip_sel (TP0) is HIGH during the write.

**If ESP32 doesn't log the write:** PSTROBE not reaching GPIO17, or level shifter not passing the signal. Check LVC8T245 #2 connections.


## Capture 5: Watchdog Timeout

**Goal:** Verify the watchdog releases IOCHRDY after ~10us when ESP32 doesn't respond.

**Setup:** Disconnect PREADY wire from ESP32 (leave it floating LOW). This simulates an unresponsive ESP32.

**DOS command:** `DEBUG` then `I 281` (non-cached read with no ESP32 response)

**Trigger:** Falling edge on TP1 (channel 5), capture at least 20us

**Expected waveform:**

```
Time        0      2us      4us      6us      8us      10us     12us
            |       |        |        |        |        |        |
TP1       __|^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^|_________
IOCHRDY   ^^^^|_____________________________________________________|^^
PSTROBE   ^^^^|__|^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
PREADY    _________________________________________________________________  (disconnected, stays LOW)
```

**Key checks:**
- IOCHRDY should be held LOW for exactly 10us (160 cycles at 16 MHz)
- After timeout, IOCHRDY releases HIGH
- TP1 goes LOW (iochrdy_hold cleared)
- The system should NOT hang. The CPU completes the bus cycle with garbage data.

**If the system hangs:** Watchdog is not working. The CPLD is holding IOCHRDY indefinitely. Check that the CPLD is programmed with the correct .jed. This is the most dangerous failure mode.

**If timeout is significantly shorter or longer than 10us:** Count CLK edges during the hold. Should be exactly 160 (0xA0). If wrong, clock frequency may be off.

**After this test:** Read status register (IN base+0x00). Bit 5 (XFER_TIMEOUT) should be set. Clear it by writing 0x20 to base+0x00.

**Reconnect PREADY before proceeding to other tests.**


## Capture 6: IRQ Assert and Acknowledge

**Goal:** Verify the full IRQ cycle: ESP32 triggers, ISA sees it, host acknowledges.

**Setup:** IRQ_SENSE jumper must be installed (enabled). An IRQ handler must be installed in DOS, or just observe on the logic analyzer.

**Trigger:** Rising edge on IRQ_OUT (channel 7)

**Expected waveform (full IRQ lifecycle):**

```
Time        0      250ns    500ns    750ns    1us    .... host reads status ....    +250ns
            |       |        |        |        |           |                         |
PIRQ      __|^^^^^^^^^^^^^^^|___________________________________|_____________________
IRQ_OUT   ________|^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^|_________________
                   PENDING   PRESENTED                              DEAD (4 CLK low)
                   (1 CLK)   (held until ACK)                       |^^^^|____________
                                                                    re-arm or idle
```

**Sequence:**
1. ESP32 asserts PIRQ (GPIO38) HIGH
2. CPLD 2-flop synchronizer captures PIRQ (2 CLK delay)
3. CPLD detects rising edge, enters PENDING state, IRQ_OUT goes HIGH
4. Next CLK: enters PRESENTED state, IRQ_OUT stays HIGH
5. Host reads status register (base+0x00), which acts as IRQ acknowledge
6. CPLD enters DEAD state, IRQ_OUT goes LOW for 4 CLK cycles (250ns at 16 MHz)
7. After DEAD: if PIRQ still HIGH, re-enter PENDING. If LOW, return to IDLE.

**Key checks:**
- IRQ_OUT should go HIGH within 3 CLK cycles (187.5ns) of PIRQ rising
- IRQ_OUT should stay HIGH until the status register is read
- Dead time should be exactly 4 CLK cycles (250ns)
- IRQ_OUT should be LOW during DEAD state

**If IRQ_OUT never goes HIGH:** Check IRQ_SENSE jumper. Check PIRQ wire from ESP32 GPIO38 to CPLD pin 56. Check that PIRQ is at 3.3V (above CPLD's 2.0V TTL threshold).

**If IRQ_OUT stays HIGH forever:** IRQ acknowledge not working. Verify that the status register read (IOR# on base+0x00) is reaching the CPLD. Check that irq_ack = is_reg00 & IOR fires.


## Capture 7: Full Loopback Cycle (NISATEST.COM)

**Goal:** Verify a complete write-then-read loopback for one byte.

**Trigger:** Falling edge on IOW# (channel 2), single-shot

**This captures one iteration of the loopback test: write 0xNN to base+0x04, then read base+0x04.**

**Expected waveform (two bus cycles back to back):**

```
Time    0         500ns       1us        2us        3us        4us       5us
        |          |           |          |          |          |         |
IOW#  ^^|__________|^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
IOR#  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^|_________________________________________|^^
TP0   __|^^^^^^^^^^^^^^^^|_____|^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^|_______
TP1   ________________________________|^^^^^^^^^^^^^^^^^^^^^^^^^|_______________
PSTR  ^^^^^^^^^|__|^^^^^^^^^^^^^^^^^^^^^^^^|__|^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
PRW   ________________________________|^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^|_______
D0-3  xx|= WRITE =|xxxxxxxxxxxxxxxx|============ READ RESPONSE ========|xxxxxxx
```

**Two distinct phases visible:**
1. WRITE: IOW# falls, data driven by host (0x55 or whatever test byte), PSTROBE pulses, PRW stays LOW
2. READ: IOR# falls, cache miss, TP1 goes HIGH, IOCHRDY held, PSTROBE pulses, ESP32 responds with same byte via PREADY, CPLD drives data onto D bus, IOCHRDY releases

**If write works but read returns wrong data:** ESP32 loopback logic issue. Check firmware loopback_byte variable. Check ESP32 serial console.

**If both write and read work but NISATEST reports mismatches:** Timing issue. Capture the failing byte value and compare write vs read on the logic analyzer.


## Troubleshooting Decision Tree

```
Card not detected (NISATEST says "not found")
  |
  +-- Is CLK running?
  |     NO --> Check oscillator, VCC, solder joints
  |     YES
  |
  +-- Does TP0 pulse during I/O access to base address?
  |     NO --> Address decode wrong. Check A0-A15 wiring, ADDR_J jumpers, AEN
  |     YES
  |
  +-- Is D bus driven during reads?
  |     NO --> HCT245 buffer OE# stuck HIGH or DIR wrong
  |     YES
  |
  +-- Does status byte have bit 6 set?
  |     NO --> ESP32 not booted. Check PBOOT wire, check USB console
  |     YES
  |
  +-- Status byte is 0xFF?
        YES --> Bus floating. HCT245 not connected or CPLD not driving


Card detected but loopback fails
  |
  +-- Do writes reach ESP32? (check serial console)
  |     NO --> PSTROBE not reaching ESP32. Check level shifter, GPIO17
  |     YES
  |
  +-- Does ESP32 assert PREADY? (check logic analyzer ch 9)
  |     NO --> Firmware issue. Check ISR. Check PREADY wire GPIO18
  |     YES
  |
  +-- Does IOCHRDY release? (check logic analyzer ch 6)
  |     NO --> Watchdog broken. CPLD programming issue.
  |     YES
  |
  +-- Is read data correct on D bus?
  |     NO --> Level shifter direction wrong. Check LVC8T245 DIR
  |     YES --> Should be passing. Check NISATEST.COM detection logic.


System hangs when card is inserted
  |
  +-- Remove card. Does system boot?
  |     NO --> PSU or motherboard issue, not card-related
  |     YES
  |
  +-- Is IOCHRDY being driven LOW with no bus cycle?
  |     YES --> CPLD driving IOCHRDY at idle. Programming error or solder bridge.
  |     NO
  |
  +-- Is any ISA signal being driven by the card at idle?
  |     YES --> Buffer OE# pull-ups not working. CPLD outputs floating.
  |     NO
  |
  +-- Does the system hang during POST or only during card access?
        POST --> Card is interfering with motherboard I/O decode. Address alias.
        ACCESS --> IOCHRDY held too long. Check ESP32 response, watchdog.
```


## After All Captures Pass

Save each capture as a .dsl file (DSView native format) and export a PNG screenshot. These go in the GitHub repo as evidence and in the first YouTube video as B-roll. Name them:

- `capture01_idle_state.dsl`
- `capture02_cached_read.dsl`
- `capture03_noncached_read.dsl`
- `capture04_write.dsl`
- `capture05_watchdog_timeout.dsl`
- `capture06_irq_cycle.dsl`
- `capture07_loopback.dsl`

Then proceed to Gate 6 (NISATEST.COM full loopback) and Gate 8 (cross-machine validation).
