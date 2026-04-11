# NetISA CPLD test cases: ISA bus edge cases from real hardware

**The ISA bus is an undocumented minefield where every chipset behaves differently, and IOCHRDY mishandling is the single most common cause of system hangs.** This report synthesizes documented failures from PicoMEM, PicoGUS, Graphics Gremlin, XT-IDE, BlueSCSI, and other retro expansion cards to provide a practical reference for building comprehensive test cases for the ATF1508AS CPLD in the NetISA project. The findings span eight critical areas—IOCHRDY timing, IRQ state machines, address decode, bus contention, reset behavior, data bus management, real-world failure modes, and Verilog verification—and are drawn from GitHub issues, Vogons forum threads, and chipset datasheets rather than theory.

---

## IOCHRDY is simultaneously essential and dangerous

IOCHRDY (pin A10) is the mechanism NetISA must use to stall the CPU while the ESP32-S3 processes a request, but its behavior varies so dramatically across chipsets that it represents the highest-risk subsystem in the CPLD design.

The IEEE P996 draft specification defines **125ns minimum low pulse width** and **15,600ns (~15.6μs) maximum hold time**, corresponding to the DRAM refresh period. On the original PC/XT, holding IOCHRDY low indefinitely simply hangs the machine—there is no bus timeout watchdog. On AT-class (286+) systems, exceeding the timeout triggers an **NMI** through the IOCHK# mechanism, though NMI delivery depends on port 0x70 bit 7 and port 0x61 bit 3 both being clear. Data must be valid **74–85ns before IOCHRDY reasserts** on read cycles.

Real projects have documented several critical IOCHRDY failures. The Graphics Gremlin README explicitly warns that if IOCHRDY (ISA pin A10) is stuck low due to a solder bridge or FPGA fault, the PC will never boot—the CPU simply cannot complete any bus cycle. The NE2000 clone freeze bug, documented by mkarcher on Vogons, occurs because the RTL8019/DP8390 pulls IOCHRDY low waiting for data from a register that was never properly configured, creating an indefinite bus stall. PicoGUS firmware v0.7.0 increased the RP2040 clock from 125MHz to 400MHz specifically to hold IOCHRDY low for shorter periods, improving compatibility with faster ISA buses. PicoMEM's IOCHRDY driving code is written in **100% assembly with code in cache memory** to guarantee deterministic timing.

**Test cases the CPLD must handle:**

- IOCHRDY deassert after exactly 125ns (minimum spec), 1μs (typical ESP32 response), 10μs (near timeout), and 14μs (just under NMI threshold)
- Watchdog timeout at 15μs that forces IOCHRDY high and sets an error flag, preventing indefinite bus stalls
- IOCHRDY behavior during back-to-back I/O cycles (REP INSB/OUTSB), where PicoMEM documented corrupted NE2000 data from a 186+ CPU writing "too fast"
- IOCHRDY interaction with DMA cycles—the specification allows IOCHRDY during DMA, with minimum pulse width of one BCLK period
- Verify IOCHRDY is not asserted during non-addressed cycles (AEN=1 or address mismatch)
- NOWS (zero wait state) must never be used simultaneously with IOCHRDY—some bus controllers malfunction if both are active

The Intel PIIX4 (82371AB) datasheet reveals that when the PIIX4 drives IOCHRDY as an output during ISA master access, it asserts for only **70ns including float time**, then releases. Some PCI-to-ISA bridge chipsets reportedly do not properly drive IOCHRDY during bus-master memory access scenarios, confirmed by mkarcher: "AFAIK it isn't [driven] on all ISA bridges."

---

## IRQ state machines must handle two incompatible PIC modes

The XT's single 8259A runs exclusively in **edge-triggered mode**, while the AT's dual-8259A cascade introduces level-triggered capability through the Edge/Level Control Registers (ELCR) at I/O ports **0x4D0 and 0x4D1**. The NetISA CPLD must implement both modes because the card targets 8088 through 486 systems.

In edge-triggered mode, the 8259A detects only LOW→HIGH transitions and requires the IRQ line to be low for at least **100ns** before a rising edge is recognized. This creates two distinct failure modes. If the IRQ line is held high too long and a second interrupt arrives while the line is still asserted, the second edge is lost entirely. If the line pulses too briefly (under 100ns), the PIC misses the edge. ISA IRQ lines are **active HIGH** with motherboard pull-up resistors, and most ISA cards use totem-pole (push-pull) drivers rather than open-collector, which is why ISA IRQ sharing is notoriously unreliable.

Spurious interrupts on IRQ7 (master) and IRQ15 (slave) occur when the IRQ line deasserts before the CPU's INTA cycle completes. The PIC finds no valid interrupt source and returns the default vector for its lowest-priority input. Detection requires reading the In-Service Register: send 0x0B to port 0x20, read port 0x20; if bit 7 is clear, the interrupt is spurious and no EOI should be sent. For IRQ15 spurious interrupts, the slave gets no EOI but the master must receive one (since the cascade input IR2 was legitimately asserted).

PicoGUS documented an IRQ timing problem where demoscene software assumed hardware-speed GUS timing when reading the IRQ source register only once per interrupt. The RP2040 reacts slower than real hardware, causing IRQ queues to desynchronize. The fix was re-raising the IRQ if voices still needed servicing—a pattern NetISA should replicate for its ESP32-S3 interrupt flow.

**Critical test cases for the IRQ state machine:**

- Edge-triggered mode: verify IRQ pulse widths of exactly 100ns (minimum), 500ns (typical), and 50ns (should be rejected)
- Level-triggered mode: verify interrupt remains asserted until the ISR clears it via a register write
- Test IRQ-then-immediate-second-interrupt scenario—the line must return low before the next rising edge
- Verify no spurious interrupts during normal register access sequences
- Test IRQ behavior during IOCHRDY wait states (IRQ should remain stable)
- Verify IRQ line tristates during reset (RESDRV high)

The available IRQ lines on the ISA bus are IRQ3–7 and IRQ9–12, 14–15. IRQ2 is consumed by the slave cascade on AT systems, and IRQ9 is redirected to IRQ2's handler by BIOS. For an 8-bit card, only **IRQ3–7** are available without the 16-bit extension connector.

---

## Address decode failures from AEN, aliasing, and partial decode

The single most common address decode bug in ISA card design is **failing to check AEN**. During DMA and DRAM refresh cycles, the DMA controller places memory addresses on the SA bus, and AEN goes HIGH. If the card's address decoder doesn't gate on AEN=LOW, it will misinterpret DMA addresses as I/O port accesses. The classic symptom: the card interferes with floppy disk DMA transfers (addresses 3F0h–3F7h), causing read/write failures that appear intermittent.

The original PC/XT motherboard only decoded **10 address bits** (A0–A9) for on-board devices, creating a 1KB I/O space with 64 aliases per address. A card at 0x300 would also respond at 0x700, 0xB00, 0xF00, and 60 other addresses. Many ISA expansion cards perpetuated this by only decoding 10 bits. NetISA should decode all **16 I/O address bits** to avoid aliasing on systems with devices above 0x3FF (Sound Blaster AWE's EMU8000 uses 0x620, 0xA20, 0xE20; ECP parallel ports use 0x778).

PicoMEM uses I/O ports 0x2A0–0x2A3, which can conflict with the RTC and some sound cards. XT-IDE at its default 0x300 conflicts with the Leading Edge DC-2010's onboard RTC. The safest ranges for NetISA are **0x300–0x31F** (traditional NIC range, but check for MPU-401 MIDI at 0x330), **0x340–0x36F** (generally safe), or a configurable base address.

For IOCS16# timing, the signal must be asserted **before IOR#/IOW# go active**—the system needs to know the transfer width before the command phase begins. The Ampro/P996 specification gives IOCS16# valid from SA address as minimum **74ns driver, 122ns maximum receiver**. If IOCS16# arrives too late, the bus controller commits to an 8-bit cycle with byte steering, doubling access time. IOCS16# must use an **open-drain driver** since multiple devices can be connected to this line.

**Test cases for address decode:**

- Verify response at the configured base address with AEN=0 (normal CPU cycle)
- Verify NO response at the same address with AEN=1 (DMA cycle)
- Test all 16 address bits to verify no aliasing—specifically test base_address XOR 0x400, 0x800, 0xC00
- Verify IOCS16# asserts within 74ns of valid address on SA bus (before command phase)
- Test addresses ±1 from the decode range to verify clean boundaries
- Verify no response to addresses of standard devices: 0x3F8 (COM1), 0x2F8 (COM2), 0x378 (LPT1), 0x200 (game port)
- Test BALE timing: address is latched on BALE falling edge, SA must be stable for 111ns setup / 26ns hold relative to BALE

---

## The XT-IDE race condition teaches bus contention lessons

The most thoroughly documented ISA bus timing fix comes from the XT-IDE Rev 3→Rev 4 redesign. A race condition existed where the `*IOR` signal, when deasserted from the ISA bus, allowed fast ATA devices (like FPGA-based NetPi-IDE) to stop outputting valid data before the high-byte latch (74LS573) could capture it. The solution was a **74LS04 hex inverter delay chain**: four inverter stages adding ~20ns of gate delay to the IOR signal going to the IDE device, ensuring the latch stores its value **10ns before** the device stops driving. All common 74x04 variants (S, LS, F, standard) were tested and provided sufficient delay.

This illustrates a fundamental ISA bus principle confirmed by multiple sources: **data is not guaranteed valid on the leading edge of IOW#**. The ISA specification guarantees data validity only at the trailing (rising) edge of the command strobe. For write operations, the CPLD should latch data on the **rising edge of IOW#**. For read operations, the CPLD must hold data valid from before IOR# assertion through IOR# deassertion.

The ISA data bus floats high when not driven, due to weak pull-up resistors (typically 4.7kΩ–10kΩ to +5V) on most motherboards. Standard 74LS245 bus buffers tristate in **12–20ns**; 74F245 variants manage 7–10ns. The ATF1508AS pin-to-pin propagation is **7.5ns maximum** at the -7 speed grade, well within ISA requirements. Bus turnaround time is at least one idle BCLK cycle between accesses, giving approximately 120ns at 8.33MHz for tristate transitions.

A critical data bus lesson from VCFed: "You NEED the '245." Designers building ISA cards discovered that strong pull-ups and sometimes totem-pole outputs on motherboards cause indeterministic voltage levels when CPLD outputs directly drive the bus without buffering. A 74LS245 between the CPLD and ISA data bus provides proper bus isolation, ESD protection, and adequate drive strength.

**Test cases for data bus and contention:**

- Verify data bus tristates within 20ns of IOR# deassertion (simulate with contention monitor checking for 'x')
- Verify no data bus drive during write cycles (CPLD data outputs must be hi-Z when IOW# is active)
- Test 16-bit transfer with IOCS16# asserted: verify SD0–SD15 all driven correctly
- Test 16-bit card behavior when IOCS16# is NOT asserted (should behave as 8-bit with byte steering)
- Verify data bus is hi-Z when card is not addressed
- Test data setup timing: data valid at least 74ns before IOCHRDY reasserts on read cycles

---

## Reset glitches and power-up behavior across chipsets

ISA RESDRV (pin B2) is **active HIGH**, derived from the power supply's Power Good signal. Cold reset holds RESDRV high for 100–500ms until power rails stabilize. Warm reset (Ctrl+Alt+Del) generates a brief pulse from the keyboard controller—power never drops, so CPLD registers retain their powered state unless explicitly cleared.

Real-world reset failures are extensively documented. The OPTi 82C602 ISA bus controller on 486 boards has been found with RESDRV stuck at ~2.5V due to corroded traces between the controller's pin 76 and the ISA slot, leaving devices in an indeterminate state. A UMC 82C206 system (ASUS ISA-386U3Q) required **holding reset for 30 seconds during power-on** to POST successfully—multiple power cycles without this hold consistently failed. The same board worked with an AT PSU at 5.2V but failed entirely with an ATX adapter at 4.9V, a mere 0.3V difference that pushed marginal circuits below threshold.

The ATF1508AS has a **power-up reset** feature with configurable hysteresis (Small or Large). With Large hysteresis, if VCC falls below 2.0V, the device must shut off completely before reinitializing. During power-up, pin keeper circuits hold previous state—for first power-up with no prior state, pins are effectively high-impedance. The CPLD's Global Tristate (GTS) pin should be connected to RESDRV to force all outputs into hi-Z during reset, preventing the card from driving any ISA bus signals before initialization completes.

**Test cases for reset behavior:**

- Verify all CPLD outputs tristate during RESDRV=HIGH
- Test warm reset (brief RESDRV pulse): verify all state machines return to idle
- Test cold reset (extended RESDRV): verify clean initialization
- Test RESDRV glitch (sub-microsecond pulse): verify the CPLD either ignores it or resets cleanly
- Verify register contents are cleared after reset
- Test behavior with slow-rising RESDRV (ramp from 0V to 5V over 1ms—simulates weak drive)
- Verify IOCHRDY is not driven low during or immediately after reset

---

## PicoMEM and PicoGUS reveal the MCU-bridge timing budget

The parallel bridge between the ATF1508AS and ESP32-S3 is the most latency-critical path in NetISA. Both PicoMEM and PicoGUS demonstrate that the bridge architecture fundamentally determines compatibility across chipsets.

PicoMEM achieves **zero wait states** only for its 128KB internal SRAM (responding within one 4MHz bus cycle, ~250ns). Its external PSRAM adds **6 wait states** at 4MHz through SPI at 120–130MHz. PicoGUS's RP2040 at 125MHz default got only ~6 CPU cycles per half ISA clock period at 8MHz—insufficient for many operations. Increasing to 400MHz improved IOCHRDY release time enough to handle 10MHz ISA buses.

The Vogons community consensus for MCU-based ISA cards is the **flip-flop/latch bridge pattern**: writes from the ISA bus get stored in an 8-bit flip-flop (74x574) and optionally generate an interrupt to the MCU, while reads return whatever the MCU previously wrote to another latch. This decouples bus timing from MCU response time entirely—the CPLD handles all ISA timing constraints while the ESP32-S3 operates asynchronously. NetISA's register cache in the CPLD implements exactly this pattern.

The status register with hardware flag merging is critical because the ESP32-S3 will update flags asynchronously to ISA bus reads. The CPLD must implement proper synchronization: a two-flip-flop synchronizer for each flag crossing from the ESP32 clock domain to the ISA bus clock domain (or combinational domain). For the ATF1508AS, which has limited flip-flops, this means carefully budgeting macrocells for synchronizers.

**Test cases for the parallel bridge and register cache:**

- Write a register from ISA, verify ESP32 side sees correct value (with realistic ESP32 read latency of 50–500ns)
- Write from ESP32 side, read from ISA side (verify register cache serves data immediately without IOCHRDY)
- Test simultaneous ISA write and ESP32 read of same register (race condition)
- Test IOCHRDY timeout when ESP32 doesn't respond (watchdog must fire at 15μs)
- Test back-to-back ISA reads with ESP32 updating between them
- Test status register flag merging: set flags from ESP32, verify ISA read shows merged flags
- Inject metastable transitions on ESP32-to-CPLD signals and verify synchronizers resolve correctly

---

## Chipset compatibility demands testing across bus speeds

The ISA bus was **never formally standardized**. The IEEE P996 draft was never completed or approved. As a result, chipset vendors implement a consensus standard with minor variations. The Ampro PC/104 timing document puts it plainly: "This has resulted in minor variations in signal interpretation and timing among the various PC chipset vendors."

PicoMEM's compatibility table reveals the scope of the problem. The Pocket386 has a bug in its ISA bus that prevents any BIOS-based expansion card from booting. The Schneider EuroPC1 won't detect the card at either 9.54MHz or 7.15MHz bus clock. The Book8088 has non-standard 250ns MEMR timing (vs. the standard 500ns). A Pentium 133MHz PCChips M530 board shows no sign of life. Amstrad PPC models may need 100μF low-ESR filtering capacitors on the ISA power pins.

ISA bus clock varies from **4.77MHz** (original PC) to **8.33MHz** (AT specification maximum) to **12–16MHz** on aggressive clone boards. The SiS 496/497 chipset user on Vogons reported needing to add +1 wait state for 16-bit I/O operations at 17.5MHz ISA clock to fix HDD errors, suggesting the chipset's IOCHRDY sampling becomes marginal at higher speeds. OPTi chipsets (82C495, 82C895) are "widely reported as problematic" on Vogons for general timing and compatibility issues.

**Test cases across bus speeds:**

- Run all testbench scenarios at 4.77MHz, 8MHz, 8.33MHz, 10MHz, and 12MHz BCLK
- Verify IOCHRDY timing margins at each speed (125ns minimum pulse is ~1 BCLK at 8.33MHz)
- Test with 33% duty cycle BCLK (ISA specification) and 50% duty cycle (some clone boards)
- Verify address decode meets setup requirements at fastest bus speed: SA setup to IOR# minimum 91ns (receiver)
- Test IOCS16# timing at 12MHz where the 122ns maximum receiver requirement leaves almost no margin

---

## Verilog testbench architecture for the ATF1508AS

The testbench should use a layered architecture with an ISA Bus Functional Model (BFM) generating cycles, monitors detecting violations, and coverage tracking FSM states.

**Bus contention detection** is the highest-value automated check. In Verilog simulation, when two drivers fight on a `wire` net, the result resolves to `x`. The testbench must declare the data bus as `wire` (never `reg`) and include a continuous monitor:

```verilog
always @(SD) begin
    if (^SD === 1'bx)
        $display("ERROR: Bus contention @ %0t, SD=%b", $time, SD);
end
```

A `pullup(IOCHRDY)` statement models the motherboard's pull-up resistor on the open-drain IOCHRDY line. The BCLK generator must use **33% duty cycle** (not 50%) per the ISA specification: 40ns high, 80ns low at 8.33MHz.

**Timing checks** belong in `specify` blocks using the ATF1508AS datasheet values. For the -7 speed grade: pin-to-pin propagation **7.5ns max**, setup to clock **3.5ns**, hold **0ns**. The `$setup`, `$hold`, and `$width` system tasks catch violations automatically during simulation. The `$width(negedge IOR_n, 100)` check ensures command pulses meet minimum active duration.

**FSM coverage** requires tracking every state visited and every transition taken using hierarchical references into the DUT (`dut.iochrdy_state`, `dut.irq_state`). At end-of-simulation, report any unvisited states or untaken transitions. For the IOCHRDY controller, verify the state machine visits IDLE→ACTIVE→WAIT→TIMEOUT and IDLE→ACTIVE→WAIT→DONE paths. For the IRQ state machine, verify both edge-triggered and level-triggered flows.

**cocotb** (Python-based) is the recommended framework for complex ISA bus test sequences. It works with Icarus Verilog (free, open-source) and makes parameterized testing straightforward—sweep all register addresses, inject random data patterns, and vary bus clock frequency from a single Python test function. No existing ISA bus driver exists in the cocotb-bus package, but a custom driver is approximately 60 lines of Python.

**Metastability modeling** for the ESP32-to-CPLD interface should inject random delays on asynchronous signals crossing between the ESP32 clock domain and ISA bus domain. A behavioral model that adds 0–90% of a clock period delay to signals passing through synchronizer flip-flops will reveal timing closure failures.

---

## Conclusion: the six tests that catch 90% of real failures

The research across dozens of ISA hardware projects converges on a surprisingly compact set of failure modes. First, **IOCHRDY stuck low** is the most catastrophic single failure—it hangs the entire system with no recovery except power cycle, making the watchdog timeout the most critical safety feature in the CPLD. Second, **AEN not checked in address decode** causes mysterious floppy and DMA failures that appear intermittent and are maddening to debug. Third, **data bus contention from late tristate** creates transient bit errors that corrupt data without obviously crashing. Fourth, **IOCS16# asserted too late** silently halves throughput by forcing byte steering. Fifth, **IRQ pulse width violations** cause lost interrupts or spurious IRQ7/15 that confuse driver software. Sixth, **reset state leakage** where CPLD outputs drive the bus before initialization can prevent POST entirely.

The ATF1508AS is well-suited for this application: its **native 5V operation** eliminates level-shifting complexity, its **7.5ns pin-to-pin propagation** provides comfortable margin against ISA's 125ns clock period, and its **128 macrocells** are sufficient for the address decoder, IOCHRDY controller, IRQ state machine, register cache, and bus buffer control logic. The primary constraint is flip-flop count for CDC synchronizers on the ESP32 interface—budget at least 2 flip-flops per asynchronous signal crossing.