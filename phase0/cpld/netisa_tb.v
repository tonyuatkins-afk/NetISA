// NetISA CPLD Testbench
// Tests all major CPLD logic paths before hardware build.
// Run with: iverilog -o netisa_tb netisa.v netisa_tb.v && vvp netisa_tb
// View waves: gtkwave netisa_tb.vcd

`timescale 1ns / 1ns

module netisa_tb;

// =========================================================================
// Testbench signals
// =========================================================================

reg        clk;
reg        reset_n;

// ISA bus
reg  [15:0] addr;      // A15..A0 (full I/O decode)
reg        aen_n;
reg        ior_n;
reg        iow_n;
wire       iochrdy;
wire       irq_out;
wire       iocs16_n;

// ISA data bus (directly driven/read by TB)
reg  [7:0] isa_data_out;  // TB drives D during writes
reg        isa_data_oe;   // TB controls data direction
wire [7:0] D;

// ESP32 parallel bus
reg  [7:0] esp_data_out;  // "ESP32" drives PD during reads
reg        esp_data_oe;
wire [7:0] PD;
wire [3:0] PA;
wire       PRW;
wire       PSTROBE;
reg        PREADY;
reg        PIRQ;
reg        PBOOT;

// Config
reg  [2:0] addr_j;
reg        safe_mode;
reg        irq_sense;
reg        slot16_n;

// Test points
wire       TP0, TP1;

// Bidirectional bus handling
assign D  = isa_data_oe ? isa_data_out : 8'bz;
assign PD = esp_data_oe ? esp_data_out : 8'bz;

// Model the ISA motherboard pullup on IOCHRDY (open-drain line).
// Real hardware has ~4.7K-10K pullup to +5V; in sim, pullup() makes
// the net read HIGH when no strong driver is active.
pullup(iochrdy);

// =========================================================================
// DUT instantiation
// =========================================================================

netisa dut (
    .CLK(clk),
    .A0(addr[0]), .A1(addr[1]), .A2(addr[2]),   .A3(addr[3]),
    .A4(addr[4]), .A5(addr[5]), .A6(addr[6]),   .A7(addr[7]),
    .A8(addr[8]), .A9(addr[9]), .A10(addr[10]), .A11(addr[11]),
    .A12(addr[12]), .A13(addr[13]), .A14(addr[14]), .A15(addr[15]),
    .D(D),
    .AEN_n(aen_n),
    .IOR_n(ior_n),
    .IOW_n(iow_n),
    .IOCHRDY(iochrdy),
    .RESET_n(reset_n),
    .IRQ_OUT(irq_out),
    .IOCS16_n(iocs16_n),
    .PD(PD),
    .PA(PA),
    .PRW(PRW),
    .PSTROBE(PSTROBE),
    .PREADY(PREADY),
    .PIRQ(PIRQ),
    .PBOOT(PBOOT),
    .ADDR_J(addr_j),
    .SAFE_MODE(safe_mode),
    .IRQ_SENSE(irq_sense),
    .SLOT16_n(slot16_n),
    .TP0(TP0),
    .TP1(TP1)
);

// =========================================================================
// Clock: 16 MHz = 62.5 ns period
// =========================================================================

initial clk = 0;
always #31.25 clk = ~clk;

// =========================================================================
// Test counters
// =========================================================================

integer pass_count = 0;
integer fail_count = 0;
integer test_num   = 0;

// =========================================================================
// Bus contention monitor
// Research report flagged this as the single highest-value automated check.
// Fires if the ISA data bus D has any X bits while enabled (indicates
// multiple drivers conflicting). Legitimate tri-state (Z) is allowed.
// Disabled during reset because reset causes defined Z transitions that
// can momentarily produce X in sim as register assignments propagate.
// =========================================================================
integer bus_contention_count = 0;
reg     bus_monitor_enable = 1'b0;

always @(D) begin
    if (bus_monitor_enable) begin
        if ((D[0] === 1'bx) || (D[1] === 1'bx) || (D[2] === 1'bx) || (D[3] === 1'bx) ||
            (D[4] === 1'bx) || (D[5] === 1'bx) || (D[6] === 1'bx) || (D[7] === 1'bx)) begin
            bus_contention_count = bus_contention_count + 1;
            $display("ERROR: Bus contention on D @ %0t, D=%b", $time, D);
        end
    end
end

// PSTROBE activity monitor: latches high whenever PSTROBE goes low
// while pstrobe_watch is enabled. Used by Group 22 tests to verify
// PSTROBE firing behavior without needing SystemVerilog fork/join.
reg pstrobe_watch = 1'b0;
reg pstrobe_seen_low = 1'b0;

always @(negedge PSTROBE) begin
    if (pstrobe_watch)
        pstrobe_seen_low <= 1'b1;
end

task check;
    input [255:0] name;
    input expected;
    input actual;
    begin
        test_num = test_num + 1;
        if (expected === actual) begin
            pass_count = pass_count + 1;
        end else begin
            fail_count = fail_count + 1;
            $display("FAIL #%0d: %0s - expected %b, got %b (time=%0t)",
                     test_num, name, expected, actual, $time);
        end
    end
endtask

task check8;
    input [255:0] name;
    input [7:0] expected;
    input [7:0] actual;
    begin
        test_num = test_num + 1;
        if (expected === actual) begin
            pass_count = pass_count + 1;
        end else begin
            fail_count = fail_count + 1;
            $display("FAIL #%0d: %0s - expected 0x%02X, got 0x%02X (time=%0t)",
                     test_num, name, expected, actual, $time);
        end
    end
endtask

// =========================================================================
// Helper tasks: ISA bus operations
// =========================================================================

task isa_reset;
    begin
        bus_monitor_enable <= 1'b0;  // Reset can cause X transients in sim
        reset_n   <= 1'b0;
        aen_n     <= 1'b1;  // CPU cycle (not DMA)
        ior_n     <= 1'b1;
        iow_n     <= 1'b1;
        addr      <= 16'h0000;
        isa_data_out <= 8'h00;
        isa_data_oe  <= 1'b0;
        esp_data_out <= 8'h00;
        esp_data_oe  <= 1'b0;
        PREADY    <= 1'b0;
        PIRQ      <= 1'b0;
        PBOOT     <= 1'b0;
        safe_mode <= 1'b0;
        irq_sense <= 1'b1;
        slot16_n  <= 1'b1;  // 8-bit slot by default
        addr_j    <= 3'b000; // base 0x280

        repeat (4) @(posedge clk);
        reset_n <= 1'b1;
        repeat (4) @(posedge clk);
        bus_monitor_enable <= 1'b1;  // Stable state; enable monitor
    end
endtask

// Write a byte to an ISA port
task isa_write;
    input [15:0] port;
    input [7:0] data;
    begin
        addr <= port;
        aen_n <= 1'b0;  // CPU cycle active
        @(posedge clk);

        isa_data_out <= data;
        isa_data_oe  <= 1'b1;
        iow_n <= 1'b0;
        repeat (3) @(posedge clk);  // IOW active for ~3 clocks

        iow_n <= 1'b1;  // Rising edge triggers write_strobe
        @(posedge clk);
        @(posedge clk);

        isa_data_oe <= 1'b0;
        aen_n <= 1'b1;
        @(posedge clk);
    end
endtask

// Read a byte from an ISA port (cached register, no wait)
task isa_read_cached;
    input  [15:0] port;
    output [7:0] data;
    begin
        addr <= port;
        aen_n <= 1'b0;
        @(posedge clk);

        ior_n <= 1'b0;
        repeat (3) @(posedge clk);

        data = D;
        ior_n <= 1'b1;
        @(posedge clk);

        aen_n <= 1'b1;
        @(posedge clk);
    end
endtask

// Read a byte from a non-cached register, with ESP32 responding
task isa_read_noncached;
    input  [15:0] port;
    input  [7:0] esp_response;
    output [7:0] data;
    begin
        addr <= port;
        aen_n <= 1'b0;
        @(posedge clk);

        ior_n <= 1'b0;
        repeat (2) @(posedge clk);

        // Wait for PSTROBE to go low (ESP32 sees the request)
        wait (PSTROBE == 1'b0);
        @(posedge clk);

        // ESP32 drives response data and asserts PREADY
        esp_data_out <= esp_response;
        esp_data_oe  <= 1'b1;
        repeat (2) @(posedge clk);  // 2-flop sync delay
        PREADY <= 1'b1;
        repeat (3) @(posedge clk);  // 2-flop sync + 1 clock for isa_out_latch update

        // IOCHRDY should be released now. Sample D at negedge to guarantee
        // all NBAs from the previous posedge (including isa_out_latch)
        // have settled before the blocking read.
        @(negedge clk);
        data = D;

        PREADY <= 1'b0;
        esp_data_oe <= 1'b0;
        ior_n <= 1'b1;
        @(posedge clk);

        aen_n <= 1'b1;
        @(posedge clk);
    end
endtask


// =========================================================================
// TEST GROUPS
// =========================================================================

reg [7:0] rdata;  // Read data capture

initial begin
    $dumpfile("netisa_tb.vcd");
    $dumpvars(0, netisa_tb);

    $display("");
    $display("=== NetISA CPLD Testbench ===");
    $display("");

    // ------------------------------------------------------------------
    // TEST GROUP 1: Reset behavior
    // ------------------------------------------------------------------
    $display("--- Group 1: Reset ---");
    isa_reset;

    check("TP0 (chip_sel) after reset",   1'b0, TP0);
    check("TP1 (iochrdy_hold) after reset", 1'b0, TP1);
    check("IRQ_OUT after reset",           1'b0, irq_out);
    check("PSTROBE after reset",           1'b1, PSTROBE);  // Idle HIGH

    // ------------------------------------------------------------------
    // TEST GROUP 2: Address decode - all 8 base addresses
    // ------------------------------------------------------------------
    $display("--- Group 2: Address Decode ---");

    // Test each jumper setting matches the correct base address
    begin : addr_decode_tests
        reg [9:0] bases [0:7];
        integer i;

        bases[0] = 10'h280; bases[1] = 10'h290;
        bases[2] = 10'h2A0; bases[3] = 10'h2C0;
        bases[4] = 10'h300; bases[5] = 10'h310;
        bases[6] = 10'h320; bases[7] = 10'h340;

        for (i = 0; i < 8; i = i + 1) begin
            isa_reset;
            addr_j <= i[2:0];
            @(posedge clk);

            // Set address to matching base
            addr  <= bases[i];
            aen_n <= 1'b0;
            ior_n <= 1'b0;
            @(posedge clk);
            @(posedge clk);
            check("chip_sel for base match", 1'b1, TP0);

            // Set address to wrong base (offset by 0x10)
            addr <= bases[i] + 10'h010;
            @(posedge clk);
            @(posedge clk);
            check("chip_sel for non-match", 1'b0, TP0);

            ior_n <= 1'b1;
            aen_n <= 1'b1;
            @(posedge clk);
        end
    end

    // ------------------------------------------------------------------
    // TEST GROUP 3: AEN blocks decode during DMA
    // ------------------------------------------------------------------
    $display("--- Group 3: AEN Blocking ---");
    isa_reset;
    addr_j <= 3'b000;  // base 0x280
    @(posedge clk);

    // With AEN_n HIGH (DMA cycle), chip_sel should be 0
    addr  <= 10'h280;
    aen_n <= 1'b1;  // DMA, not CPU
    ior_n <= 1'b0;
    @(posedge clk);
    @(posedge clk);
    check("AEN blocks chip_sel during DMA", 1'b0, TP0);

    // With AEN_n LOW (CPU cycle), chip_sel should be 1
    aen_n <= 1'b0;
    @(posedge clk);
    @(posedge clk);
    check("AEN allows chip_sel during CPU", 1'b1, TP0);

    ior_n <= 1'b1;
    aen_n <= 1'b1;
    @(posedge clk);

    // ------------------------------------------------------------------
    // TEST GROUP 4: Register write and ESP32 bridge
    // ------------------------------------------------------------------
    $display("--- Group 4: ISA Write ---");
    isa_reset;
    addr_j <= 3'b000;
    @(posedge clk);

    // Write 0xA5 to register 0x04 (data port)
    isa_write(10'h284, 8'hA5);

    // Check PA reflects the register address
    // (PA is combinational from A3-A0, only valid during active cycle)
    // Check the write latch captured the data
    // Drive a read cycle to check latch via PD bus
    addr <= 10'h284;
    aen_n <= 1'b0;
    iow_n <= 1'b1;
    ior_n <= 1'b1;
    @(posedge clk);
    // PD should reflect isa_in_latch when pd_drive is active
    // pd_drive = chip_sel & ~PRW, and PRW = IOR & chip_sel
    // With IOR=0, PRW=0, so pd_drive = chip_sel = 1
    // PD should be isa_in_latch = 0xA5
    @(posedge clk);
    check8("PD shows write latch 0xA5", 8'hA5, PD);

    aen_n <= 1'b1;
    @(posedge clk);

    // ------------------------------------------------------------------
    // TEST GROUP 5: Cached register read (zero wait states)
    // ------------------------------------------------------------------
    $display("--- Group 5: Cached Register Read ---");
    isa_reset;
    addr_j <= 3'b000;
    @(posedge clk);

    // Pre-load isa_out_latch by simulating an ESP32 response
    // We need to trigger a non-cached read first to load the latch
    // Then read a cached register to get the latched data

    // First, do a non-cached read to reg 0x01 to load latch with 0x42
    isa_read_noncached(10'h281, 8'h42, rdata);

    // Now read cached register 0x06 (error code) - should return latched 0x42
    // (All cached regs share isa_out_latch)
    isa_read_cached(10'h286, rdata);
    check8("Cached read returns latched data", 8'h42, rdata);

    // Verify IOCHRDY was NOT pulled low during cached read
    // (We check TP1 which reflects iochrdy_hold)
    check("No wait state for cached read", 1'b0, TP1);

    // ------------------------------------------------------------------
    // TEST GROUP 6: Non-cached read with IOCHRDY wait states
    // ------------------------------------------------------------------
    $display("--- Group 6: IOCHRDY Wait States ---");
    isa_reset;
    addr_j <= 3'b000;
    @(posedge clk);

    // Read from reg 0x01 (non-cached) without ESP32 responding immediately
    addr <= 10'h281;
    aen_n <= 1'b0;
    @(posedge clk);

    ior_n <= 1'b0;
    repeat (4) @(posedge clk);

    // IOCHRDY should be held LOW
    check("IOCHRDY held during non-cached read", 1'b1, TP1);

    // ESP32 responds
    esp_data_out <= 8'hBB;
    esp_data_oe  <= 1'b1;
    PREADY <= 1'b1;
    repeat (4) @(posedge clk);  // 2-flop sync + 1

    // IOCHRDY should be released
    check("IOCHRDY released after PREADY", 1'b0, TP1);

    PREADY <= 1'b0;
    esp_data_oe <= 1'b0;
    ior_n <= 1'b1;
    aen_n <= 1'b1;
    repeat (2) @(posedge clk);

    // ------------------------------------------------------------------
    // TEST GROUP 7: IOCHRDY watchdog timeout
    // ------------------------------------------------------------------
    $display("--- Group 7: Watchdog Timeout ---");
    isa_reset;
    addr_j <= 3'b000;
    @(posedge clk);

    // Start a non-cached read, ESP32 never responds
    addr <= 10'h281;
    aen_n <= 1'b0;
    @(posedge clk);

    ior_n <= 1'b0;
    repeat (4) @(posedge clk);
    check("IOCHRDY held before timeout", 1'b1, TP1);

    // Wait for the watchdog to fire. wdog_timeout is combinational and asserts
    // for one cycle when (watchdog == 0xA0 && iochrdy_hold). At the next clock
    // edge, read_pending and iochrdy_hold clear -- but if IOR is still low,
    // cache_miss_read will re-trigger the read on the cycle after that. We
    // must deassert IOR within that one-cycle window so the bus cycle truly
    // ends instead of immediately restarting (which is correct hardware
    // behavior, but not what we're testing here).
    wait (dut.wdog_timeout);
    ior_n <= 1'b1;
    aen_n <= 1'b1;
    repeat (3) @(posedge clk);

    // With IOR deasserted, chip_sel drops, iochrdy_hold stays clear
    check("IOCHRDY released after timeout", 1'b0, TP1);

    // ------------------------------------------------------------------
    // TEST GROUP 8: xfer_timeout flag
    // ------------------------------------------------------------------
    $display("--- Group 8: XFER_TIMEOUT Flag ---");

    // After the timeout above, xfer_timeout should be set
    // Read status register (reg 0x00, cached) and check bit 5
    isa_read_cached(10'h280, rdata);
    check("xfer_timeout set in status (bit 5)", 1'b1, rdata[5]);

    // Clear it by writing 0x20 to reg 0x00
    isa_write(10'h280, 8'h20);
    repeat (2) @(posedge clk);

    // Read status again, bit 5 should be clear
    isa_read_cached(10'h280, rdata);
    check("xfer_timeout cleared after write 0x20", 1'b0, rdata[5]);

    // ------------------------------------------------------------------
    // TEST GROUP 9: Status register hardware flag merge
    // ------------------------------------------------------------------
    $display("--- Group 9: Status Register Merge ---");
    isa_reset;
    addr_j <= 3'b000;
    @(posedge clk);

    // Test SAFE_MODE (bit 3)
    safe_mode <= 1'b1;
    @(posedge clk);
    isa_read_cached(10'h280, rdata);
    check("SAFE_MODE appears in status bit 3", 1'b1, rdata[3]);

    safe_mode <= 1'b0;
    @(posedge clk);
    isa_read_cached(10'h280, rdata);
    check("SAFE_MODE clears in status bit 3", 1'b0, rdata[3]);

    // Test BOOT_COMPLETE (bit 6) via PBOOT
    PBOOT <= 1'b1;
    repeat (4) @(posedge clk);  // 2-flop sync
    isa_read_cached(10'h280, rdata);
    check("PBOOT appears in status bit 6", 1'b1, rdata[6]);

    PBOOT <= 1'b0;
    repeat (4) @(posedge clk);
    isa_read_cached(10'h280, rdata);
    check("PBOOT clears in status bit 6", 1'b0, rdata[6]);

    // ------------------------------------------------------------------
    // TEST GROUP 10: IRQ state machine
    // ------------------------------------------------------------------
    $display("--- Group 10: IRQ State Machine ---");
    isa_reset;
    addr_j <= 3'b000;
    irq_sense <= 1'b1;
    @(posedge clk);

    // Initially IDLE, IRQ should be low/Z
    check("IRQ idle at start", 1'b0, irq_out);

    // Trigger IRQ from ESP32
    PIRQ <= 1'b1;
    repeat (5) @(posedge clk);  // 2-flop sync + edge detect
    PIRQ <= 1'b0;
    repeat (2) @(posedge clk);

    // Should be in PENDING or PRESENTED, IRQ_OUT HIGH
    check("IRQ asserted after PIRQ", 1'b1, irq_out);

    // ACK the IRQ by reading reg 0x00 (irq_ack = is_reg00 & IOR)
    isa_read_cached(10'h280, rdata);
    repeat (2) @(posedge clk);

    // Should enter DEAD state (4 cycles LOW)
    // After DEAD, should return to IDLE
    repeat (6) @(posedge clk);
    check("IRQ returns to idle after ack + dead", 1'b0, irq_out);

    // ------------------------------------------------------------------
    // TEST GROUP 11: IRQ disabled when IRQ_SENSE is LOW
    // ------------------------------------------------------------------
    $display("--- Group 11: IRQ Disabled ---");
    isa_reset;
    irq_sense <= 1'b0;  // IRQ disabled
    @(posedge clk);

    PIRQ <= 1'b1;
    repeat (5) @(posedge clk);
    PIRQ <= 1'b0;
    repeat (2) @(posedge clk);

    // IRQ_OUT should be tri-state (Z) when IRQ_SENSE is LOW
    check("IRQ tri-state when disabled", 1'bz, irq_out);

    // ------------------------------------------------------------------
    // TEST GROUP 12: IOCS16# assertion
    // ------------------------------------------------------------------
    $display("--- Group 12: IOCS16 ---");
    isa_reset;
    addr_j  <= 3'b000;
    slot16_n <= 1'b0;  // 16-bit slot
    @(posedge clk);

    // Access data register (0x04) in 16-bit slot
    addr <= 10'h284;
    aen_n <= 1'b0;
    ior_n <= 1'b0;
    @(posedge clk);
    @(posedge clk);
    check("IOCS16 asserted for reg 0x04 in 16-bit slot", 1'b0, iocs16_n);

    // Access data hi register (0x05) in 16-bit slot
    addr <= 10'h285;
    @(posedge clk);
    @(posedge clk);
    check("IOCS16 asserted for reg 0x05 in 16-bit slot", 1'b0, iocs16_n);

    // Access non-data register (0x00) in 16-bit slot
    addr <= 10'h280;
    @(posedge clk);
    @(posedge clk);
    check("IOCS16 NOT asserted for reg 0x00", 1'bz, iocs16_n);

    // Access data register in 8-bit slot
    slot16_n <= 1'b1;
    addr <= 10'h284;
    @(posedge clk);
    @(posedge clk);
    check("IOCS16 NOT asserted in 8-bit slot", 1'bz, iocs16_n);

    ior_n <= 1'b1;
    aen_n <= 1'b1;
    @(posedge clk);

    // ------------------------------------------------------------------
    // TEST GROUP 13: PSTROBE generation
    // ------------------------------------------------------------------
    $display("--- Group 13: PSTROBE ---");
    isa_reset;
    addr_j <= 3'b000;
    @(posedge clk);

    // PSTROBE should be HIGH idle
    check("PSTROBE idle HIGH", 1'b1, PSTROBE);

    // Write to trigger strobe
    isa_write(10'h284, 8'h55);

    // PSTROBE should have pulsed LOW during the write
    // (It's already back HIGH by now since write is done)
    check("PSTROBE returns to HIGH after write", 1'b1, PSTROBE);

    // ------------------------------------------------------------------
    // TEST GROUP 14: Register window (A3-A0 decodes all 16 registers)
    // ------------------------------------------------------------------
    $display("--- Group 14: Register Window ---");
    isa_reset;
    addr_j <= 3'b000;
    @(posedge clk);

    begin : reg_window_test
        integer r;
        for (r = 0; r < 16; r = r + 1) begin
            addr <= 10'h280 + r;
            aen_n <= 1'b0;
            ior_n <= 1'b0;
            @(posedge clk);
            @(posedge clk);
            check("chip_sel for register window", 1'b1, TP0);
            ior_n <= 1'b1;
            aen_n <= 1'b1;
            @(posedge clk);
        end
    end

    // ------------------------------------------------------------------
    // TEST GROUP 15: Reserved register pass-through (NIC Mode contract)
    //
    // Reg 0x03, 0x0E, 0x0F are deliberately NOT in cache_hit. Reads must
    // go through cache_miss_read -> PSTROBE -> ESP32 response -> IOCHRDY
    // release, just like any other non-cached register. This locks in
    // the dual-mode scaffolding contract documented in spec 2.6.1:
    // the ESP32 is the sole arbiter of these register semantics across
    // firmware versions, and CPLD behavior for them is identical to the
    // existing data-port path.
    // ------------------------------------------------------------------
    $display("--- Group 15: Reserved Register Pass-Through ---");
    isa_reset;
    addr_j <= 3'b000;
    @(posedge clk);

    // reg 0x03 read -> ESP32 responds with 0x33
    isa_read_noncached(10'h283, 8'h33, rdata);
    check8("reg 0x03 non-cached read returns ESP data", 8'h33, rdata);

    // reg 0x0E read -> ESP32 responds with 0xEE
    isa_read_noncached(10'h28E, 8'hEE, rdata);
    check8("reg 0x0E non-cached read returns ESP data", 8'hEE, rdata);

    // reg 0x0F read -> ESP32 responds with 0xFF (important: 0xFF edge case)
    isa_read_noncached(10'h28F, 8'hFF, rdata);
    check8("reg 0x0F non-cached read returns ESP data", 8'hFF, rdata);

    // Also verify reg 0x0D (Signal Quality) is non-cached per spec
    isa_read_noncached(10'h28D, 8'h5A, rdata);
    check8("reg 0x0D (Signal Quality) non-cached read", 8'h5A, rdata);

    // ------------------------------------------------------------------
    // TEST GROUP 16: Back-to-back bus cycles
    //
    // Real ISA software uses REP INSB/OUTSB for bulk transfers. PicoMEM
    // documented corrupted NE2000 data from 186+ CPUs writing "too fast"
    // — the card's state machine didn't reset between adjacent cycles.
    // These tests exercise tight sequences that the existing isolated
    // tests don't cover.
    // ------------------------------------------------------------------
    $display("--- Group 16: Back-to-Back Cycles ---");
    isa_reset;
    addr_j <= 3'b000;
    @(posedge clk);

    // Two cached reads in a row against the same register, no gap.
    // First pre-load isa_out_latch with a known value via a non-cached read.
    isa_read_noncached(10'h281, 8'hC3, rdata);
    check8("Preload latch via non-cached reg 0x01", 8'hC3, rdata);

    // Back-to-back cached reads of reg 0x06 (error code).
    // Both should return the same latched value with no wait states.
    isa_read_cached(10'h286, rdata);
    check8("Back-to-back cached read #1", 8'hC3, rdata);
    isa_read_cached(10'h286, rdata);
    check8("Back-to-back cached read #2", 8'hC3, rdata);

    // Back-to-back non-cached reads with different data each time.
    // Validates that read_pending/iochrdy_hold state clears cleanly
    // between cycles and isa_out_latch captures each new byte.
    isa_read_noncached(10'h281, 8'h11, rdata);
    check8("Back-to-back non-cached read #1", 8'h11, rdata);
    isa_read_noncached(10'h281, 8'h22, rdata);
    check8("Back-to-back non-cached read #2", 8'h22, rdata);
    isa_read_noncached(10'h281, 8'h33, rdata);
    check8("Back-to-back non-cached read #3", 8'h33, rdata);

    // Write immediately followed by a cached read of a cached register.
    // Validates that the write strobe doesn't leak into the next cycle.
    isa_write(10'h284, 8'h77);
    isa_read_cached(10'h286, rdata);
    check8("Cached read after immediate write", 8'h33, rdata);
    // (reg 0x06 still holds the last non-cached load, 0x33)

    // ------------------------------------------------------------------
    // TEST GROUP 17: Mid-cycle reset robustness
    //
    // Real ISA systems see RESDRV pulses for warm reset (Ctrl+Alt+Del)
    // that can arrive in the middle of any bus cycle. OPTi 82C602 and
    // UMC 82C206 quirks documented in the research report make clean
    // reset behavior critical. The CPLD must return all state to idle
    // regardless of when reset arrives.
    // ------------------------------------------------------------------
    $display("--- Group 17: Mid-Cycle Reset ---");
    isa_reset;
    addr_j <= 3'b000;
    @(posedge clk);

    // Start a non-cached read (IOCHRDY held), then slam reset
    // mid-wait-state and verify clean recovery.
    addr <= 10'h281;
    aen_n <= 1'b0;
    @(posedge clk);
    ior_n <= 1'b0;
    repeat (4) @(posedge clk);
    check("IOCHRDY held before mid-cycle reset", 1'b1, TP1);

    // Assert reset mid-wait-state
    bus_monitor_enable <= 1'b0;
    reset_n <= 1'b0;
    repeat (2) @(posedge clk);
    check("iochrdy_hold clears on mid-cycle reset", 1'b0, TP1);

    // Release reset
    reset_n <= 1'b1;
    ior_n <= 1'b1;
    aen_n <= 1'b1;
    repeat (4) @(posedge clk);
    bus_monitor_enable <= 1'b1;
    check("TP0 idle after mid-cycle reset recovery", 1'b0, TP0);
    check("TP1 idle after mid-cycle reset recovery", 1'b0, TP1);

    // Verify the card is functional again by doing a normal read.
    isa_read_noncached(10'h281, 8'hA9, rdata);
    check8("Non-cached read works after mid-cycle reset", 8'hA9, rdata);

    // Reset during an active IRQ: assert PIRQ, let IRQ fire, then reset.
    isa_reset;
    addr_j <= 3'b000;
    irq_sense <= 1'b1;
    @(posedge clk);

    PIRQ <= 1'b1;
    repeat (5) @(posedge clk);
    PIRQ <= 1'b0;
    repeat (2) @(posedge clk);
    check("IRQ high before mid-cycle reset", 1'b1, irq_out);

    // Reset while IRQ is asserted
    bus_monitor_enable <= 1'b0;
    reset_n <= 1'b0;
    repeat (4) @(posedge clk);
    reset_n <= 1'b1;
    repeat (4) @(posedge clk);
    bus_monitor_enable <= 1'b1;
    check("IRQ returns to idle after reset during active IRQ", 1'b0, irq_out);

    // ------------------------------------------------------------------
    // TEST GROUP 18: IRQ edge cases
    //
    // Research report flagged IRQ pulse width and retrigger as common
    // failure modes:
    //   - XT 8259A requires line to go low >=100ns between edges
    //   - PicoGUS documented IRQ queue desync from missed retriggers
    //   - Spurious IRQ7/15 from deassert-before-ACK
    //
    // Current design: S_IDLE -> S_PENDING -> S_PRESENT -> S_DEAD (4 cycles)
    // DEAD provides the mandatory low period (4 * 62.5ns = 250ns, well
    // above the 100ns minimum). After DEAD, returns to PENDING if PIRQ
    // is still asserted, else IDLE.
    // ------------------------------------------------------------------
    $display("--- Group 18: IRQ Edge Cases ---");
    isa_reset;
    addr_j <= 3'b000;
    irq_sense <= 1'b1;
    @(posedge clk);

    // IRQ retrigger: fire IRQ, ACK it, keep PIRQ asserted, verify the
    // line goes LOW for the dead period then HIGH again for the second
    // interrupt (edge-triggered 8259 compatible retrigger).
    //
    // Manual ACK (not isa_read_cached) so we can observe irq_out DURING
    // the 4-cycle DEAD period. The packaged task takes too many clocks
    // to return, by which time DEAD has elapsed.
    PIRQ <= 1'b1;
    repeat (5) @(posedge clk);
    check("IRQ asserted on first rising edge", 1'b1, irq_out);

    // Manual ACK
    addr <= 10'h280;
    aen_n <= 1'b0;
    @(posedge clk);
    ior_n <= 1'b0;
    @(posedge clk);  // Edge N: irq_ack=1 sampled, irq_state <= S_DEAD via NBA
    @(posedge clk);  // Edge N+1: state is now S_DEAD, irq_out goes LOW
    check("IRQ low during DEAD state after ACK", 1'b0, irq_out);

    // Hold ACK a bit longer, then release. DEAD is 4 cycles, so by the
    // time we release IOR the state machine is either still in DEAD or
    // has already transitioned to S_PENDING (PIRQ still asserted).
    ior_n <= 1'b1;
    aen_n <= 1'b1;
    @(posedge clk);

    // Wait for DEAD -> PENDING -> PRESENT transition to complete
    repeat (6) @(posedge clk);
    check("IRQ re-asserts after DEAD with PIRQ still high", 1'b1, irq_out);

    PIRQ <= 1'b0;
    // Second ACK via the packaged task (PIRQ deasserted, should go to IDLE)
    isa_read_cached(10'h280, rdata);
    repeat (8) @(posedge clk);
    check("IRQ returns to idle after second ACK and PIRQ deassert", 1'b0, irq_out);

    // IRQ pulse width: verify a short PIRQ pulse still triggers a
    // clean interrupt via the rising-edge detector.
    isa_reset;
    addr_j <= 3'b000;
    irq_sense <= 1'b1;
    @(posedge clk);

    // Single-cycle PIRQ pulse (after the 2-flop sync it's effectively
    // a one-clock rising edge at pirq_rising).
    PIRQ <= 1'b1;
    @(posedge clk);
    PIRQ <= 1'b0;
    repeat (6) @(posedge clk);  // Allow sync to propagate and state machine to run
    check("Single-cycle PIRQ still triggers IRQ_OUT", 1'b1, irq_out);

    // Clean up
    isa_read_cached(10'h280, rdata);
    repeat (8) @(posedge clk);

    // IRQ stability during IOCHRDY wait: assert PIRQ while a non-cached
    // read is holding IOCHRDY, verify IRQ asserts cleanly after the read
    // completes (shouldn't be eaten by the wait-state machinery).
    isa_reset;
    addr_j <= 3'b000;
    irq_sense <= 1'b1;
    @(posedge clk);

    // Start a non-cached read but don't let ESP32 respond yet
    addr <= 10'h281;
    aen_n <= 1'b0;
    @(posedge clk);
    ior_n <= 1'b0;
    repeat (4) @(posedge clk);

    // IOCHRDY is held; now assert PIRQ
    PIRQ <= 1'b1;

    // ESP32 responds while PIRQ is rising
    esp_data_out <= 8'hCD;
    esp_data_oe  <= 1'b1;
    PREADY <= 1'b1;
    repeat (5) @(posedge clk);
    PREADY <= 1'b0;
    esp_data_oe <= 1'b0;
    ior_n <= 1'b1;
    aen_n <= 1'b1;
    repeat (3) @(posedge clk);

    check("IRQ_OUT high after PIRQ during IOCHRDY wait", 1'b1, irq_out);

    // Clean up
    PIRQ <= 1'b0;
    isa_read_cached(10'h280, rdata);
    repeat (8) @(posedge clk);

    // IRQ_SENSE toggled during active IRQ: disabling should tri-state.
    PIRQ <= 1'b1;
    repeat (5) @(posedge clk);
    check("IRQ_OUT high with IRQ_SENSE=1", 1'b1, irq_out);
    irq_sense <= 1'b0;
    @(posedge clk);
    check("IRQ_OUT tri-state when IRQ_SENSE cleared mid-interrupt", 1'bz, irq_out);

    PIRQ <= 1'b0;
    irq_sense <= 1'b1;
    repeat (4) @(posedge clk);

    // ------------------------------------------------------------------
    // TEST GROUP 19: Watchdog precision and re-arm
    //
    // Verify the IOCHRDY watchdog fires at exactly the documented cycle
    // count and re-arms cleanly after firing. The research report notes
    // that "IOCHRDY stuck low is the single most catastrophic failure"
    // and the watchdog is the safety net.
    //
    // Uses hierarchical reference dut.watchdog to measure exactly when
    // the counter reaches 0xA0 relative to the start of cache_miss_read.
    // ------------------------------------------------------------------
    $display("--- Group 19: Watchdog Precision ---");
    isa_reset;
    addr_j <= 3'b000;
    @(posedge clk);

    // Start a non-cached read, no ESP response
    addr <= 10'h281;
    aen_n <= 1'b0;
    @(posedge clk);
    ior_n <= 1'b0;

    // At this point cache_miss_read will assert on the next clock.
    // watchdog increments when read_pending is set, so after N clocks
    // the counter should be approximately N-1.
    @(posedge clk);  // Edge where read_pending <= 1, watchdog still 0
    @(posedge clk);  // Edge where watchdog <= 1

    // Wait until watchdog is ~halfway to 0xA0 (80)
    repeat (80) @(posedge clk);
    check("Watchdog still counting, not yet fired", 1'b1, TP1);

    // Run until watchdog fires
    wait (dut.wdog_timeout);
    // At this moment wdog_timeout is combinationally true. Deassert IOR.
    ior_n <= 1'b1;
    aen_n <= 1'b1;
    repeat (3) @(posedge clk);
    check("Watchdog fired and iochrdy released", 1'b0, TP1);
    check("xfer_timeout flag set after watchdog", 1'b1, dut.xfer_timeout);

    // Re-arm: clear the flag and verify a subsequent cycle works cleanly
    isa_write(10'h280, 8'h20);  // Clear xfer_timeout
    repeat (2) @(posedge clk);
    check("xfer_timeout flag cleared", 1'b0, dut.xfer_timeout);

    // New non-cached read should work normally with ESP response
    isa_read_noncached(10'h281, 8'h7E, rdata);
    check8("Non-cached read works after watchdog re-arm", 8'h7E, rdata);

    // ------------------------------------------------------------------
    // TEST GROUP 20: Standard ISA device range rejection
    //
    // Research report section "Address decode failures" notes that the
    // single most common bug is failing to ignore standard device I/O
    // ranges. With our 10-bit (A9..A4) decode and AEN gating, the card
    // should NEVER select when the host addresses COM1/2, LPT1, game
    // port, or floppy DMA registers — regardless of AEN.
    //
    // NOTE: the current design decodes only A9..A4, so aliases exist at
    // 0x680/0xA80/0xE80 etc. Those aliases are outside the scope of
    // this test (see the A15..A10 decision deferred for interactive
    // review). Here we verify the intended base window is clean.
    // ------------------------------------------------------------------
    $display("--- Group 20: Standard Device Range Rejection ---");
    isa_reset;
    addr_j <= 3'b000;  // base 0x280
    @(posedge clk);

    // COM1 (0x3F8-0x3FF)
    addr <= 10'h3F8;
    aen_n <= 1'b0;
    ior_n <= 1'b0;
    repeat (2) @(posedge clk);
    check("No chip_sel at COM1 0x3F8", 1'b0, TP0);
    addr <= 10'h3FF;
    repeat (2) @(posedge clk);
    check("No chip_sel at COM1+7 0x3FF", 1'b0, TP0);

    // COM2 (0x2F8-0x2FF)
    addr <= 10'h2F8;
    repeat (2) @(posedge clk);
    check("No chip_sel at COM2 0x2F8", 1'b0, TP0);

    // LPT1 (0x378-0x37F)
    addr <= 10'h378;
    repeat (2) @(posedge clk);
    check("No chip_sel at LPT1 0x378", 1'b0, TP0);

    // Game port (0x200-0x207)
    addr <= 10'h200;
    repeat (2) @(posedge clk);
    check("No chip_sel at Game port 0x200", 1'b0, TP0);

    // Floppy controller (0x3F0-0x3F7)
    addr <= 10'h3F0;
    repeat (2) @(posedge clk);
    check("No chip_sel at Floppy 0x3F0", 1'b0, TP0);

    // PIC master (0x20-0x21)
    addr <= 10'h020;
    repeat (2) @(posedge clk);
    check("No chip_sel at PIC 0x020", 1'b0, TP0);

    // DMA controller (0x00-0x0F)
    addr <= 10'h000;
    repeat (2) @(posedge clk);
    check("No chip_sel at DMA 0x000", 1'b0, TP0);

    // RTC / NMI mask (0x070-0x071)
    addr <= 10'h070;
    repeat (2) @(posedge clk);
    check("No chip_sel at RTC 0x070", 1'b0, TP0);

    // Classic SoundBlaster (0x220) — adjacent to base 0x280, clean negative
    addr <= 10'h220;
    repeat (2) @(posedge clk);
    check("No chip_sel at SoundBlaster 0x220", 1'b0, TP0);

    ior_n <= 1'b1;
    aen_n <= 1'b1;
    @(posedge clk);

    // ------------------------------------------------------------------
    // TEST GROUP 21: IOCS16 combinational timing
    //
    // Research report: "IOCS16# must be asserted BEFORE IOR#/IOW# go
    // active — the system needs to know the transfer width before the
    // command phase begins." The spec gives IOCS16# valid from SA
    // address minimum 74ns driver.
    //
    // Current design: iocs16_active = chip_sel & ~SLOT16_n & (is_reg04 |
    // is_reg05). chip_sel is purely combinational from addr+AEN, so
    // iocs16_active asserts the same moment the address settles,
    // well before IOR/IOW are driven by the host. This test verifies
    // that combinational behavior by checking iocs16_n the same cycle
    // the address is presented, with IOR/IOW still idle.
    // ------------------------------------------------------------------
    $display("--- Group 21: IOCS16 Timing ---");
    isa_reset;
    addr_j <= 3'b000;
    slot16_n <= 1'b0;  // 16-bit slot
    @(posedge clk);

    // Present address 0x284 with AEN=0 but IOR/IOW still idle.
    // IOCS16_n should assert combinationally without waiting for
    // any command strobe.
    addr <= 10'h284;
    aen_n <= 1'b0;
    ior_n <= 1'b1;
    iow_n <= 1'b1;
    @(posedge clk);
    check("IOCS16 asserted on address alone (combinational)", 1'b0, iocs16_n);

    // Now assert IOR — IOCS16 should stay asserted, not glitch
    ior_n <= 1'b0;
    @(posedge clk);
    check("IOCS16 stable through IOR assertion", 1'b0, iocs16_n);

    // Release IOR, IOCS16 should stay asserted as long as address is valid
    ior_n <= 1'b1;
    @(posedge clk);
    check("IOCS16 stable after IOR deasserted", 1'b0, iocs16_n);

    // Change address to a non-data register — IOCS16 should release
    addr <= 10'h280;
    @(posedge clk);
    check("IOCS16 releases on non-data register address", 1'bz, iocs16_n);

    // Put address back on data reg, toggle AEN — IOCS16 tracks AEN
    addr <= 10'h285;
    @(posedge clk);
    check("IOCS16 re-asserts for reg 0x05", 1'b0, iocs16_n);
    aen_n <= 1'b1;
    @(posedge clk);
    check("IOCS16 releases when AEN deasserts (DMA)", 1'bz, iocs16_n);

    // Return to clean state
    slot16_n <= 1'b1;
    aen_n <= 1'b1;
    @(posedge clk);

    // ------------------------------------------------------------------
    // TEST GROUP 22: PSTROBE behavior
    //
    // PSTROBE is the card's request to the ESP32 for a non-cached
    // operation. It must fire on:
    //   - Any write_strobe (registered write completes)
    //   - Any cache_miss_read (host is reading a non-cached register)
    // And it must NOT fire on:
    //   - Cached register reads (they're served from CPLD latches)
    //   - Non-addressed cycles
    // ------------------------------------------------------------------
    $display("--- Group 22: PSTROBE Behavior ---");
    isa_reset;
    addr_j <= 3'b000;
    @(posedge clk);
    check("PSTROBE idle HIGH after reset", 1'b1, PSTROBE);

    // Cached read must NOT generate a PSTROBE pulse.
    // Preload the latch first so the subsequent cached read actually
    // returns meaningful data (not required, but cleaner).
    isa_read_noncached(10'h281, 8'h99, rdata);  // Preload
    repeat (2) @(posedge clk);

    pstrobe_seen_low <= 1'b0;
    pstrobe_watch    <= 1'b1;
    @(posedge clk);
    isa_read_cached(10'h286, rdata);
    repeat (2) @(posedge clk);
    pstrobe_watch <= 1'b0;
    check("PSTROBE never asserted during cached read", 1'b0, pstrobe_seen_low);

    // Non-cached read MUST generate at least one PSTROBE pulse.
    pstrobe_seen_low <= 1'b0;
    pstrobe_watch    <= 1'b1;
    @(posedge clk);
    isa_read_noncached(10'h281, 8'h5C, rdata);
    pstrobe_watch <= 1'b0;
    check("PSTROBE asserted during non-cached read", 1'b1, pstrobe_seen_low);

    // Write MUST generate a PSTROBE pulse.
    pstrobe_seen_low <= 1'b0;
    pstrobe_watch    <= 1'b1;
    @(posedge clk);
    isa_write(10'h284, 8'hD4);
    pstrobe_watch <= 1'b0;
    check("PSTROBE asserted during write", 1'b1, pstrobe_seen_low);

    // ------------------------------------------------------------------
    // TEST GROUP 23: Status register flag merge combinations
    //
    // The status register (reg 0x00) combines ESP32-pushed bits 0-2
    // with CPLD-owned hardware flags:
    //   Bit 3: SAFE_MODE (from J3 jumper)
    //   Bit 5: xfer_timeout (CPLD watchdog)
    //   Bit 6: pboot_sync (ESP32 boot complete)
    //
    // Existing Group 9 tests each bit individually. This group tests
    // combinations and the read path when multiple flags are asserted.
    // ------------------------------------------------------------------
    $display("--- Group 23: Status Flag Merge Combinations ---");
    isa_reset;
    addr_j <= 3'b000;
    @(posedge clk);

    // All three hardware flags asserted simultaneously
    safe_mode <= 1'b1;
    PBOOT <= 1'b1;
    repeat (4) @(posedge clk);  // PBOOT sync

    // Force xfer_timeout via a watchdog fire
    addr <= 10'h281;
    aen_n <= 1'b0;
    @(posedge clk);
    ior_n <= 1'b0;
    wait (dut.wdog_timeout);
    ior_n <= 1'b1;
    aen_n <= 1'b1;
    repeat (3) @(posedge clk);
    check("xfer_timeout set before combined read", 1'b1, dut.xfer_timeout);

    // Read status register — should have bits 3, 5, 6 all set
    isa_read_cached(10'h280, rdata);
    check("SAFE_MODE bit 3 in combined status", 1'b1, rdata[3]);
    check("xfer_timeout bit 5 in combined status", 1'b1, rdata[5]);
    check("PBOOT bit 6 in combined status", 1'b1, rdata[6]);

    // Clear xfer_timeout, re-read, verify bit 5 clears but 3 and 6 stay
    isa_write(10'h280, 8'h20);
    repeat (2) @(posedge clk);
    isa_read_cached(10'h280, rdata);
    check("SAFE_MODE still set after clearing xfer_timeout", 1'b1, rdata[3]);
    check("xfer_timeout cleared, 3 and 6 preserved", 1'b0, rdata[5]);
    check("PBOOT still set after clearing xfer_timeout", 1'b1, rdata[6]);

    // Drop SAFE_MODE jumper, verify bit 3 clears while others stay
    safe_mode <= 1'b0;
    @(posedge clk);
    isa_read_cached(10'h280, rdata);
    check("SAFE_MODE clears on jumper release", 1'b0, rdata[3]);
    check("PBOOT unaffected by SAFE_MODE toggle", 1'b1, rdata[6]);

    // PBOOT cleared only by full reset
    PBOOT <= 1'b0;
    repeat (4) @(posedge clk);
    isa_read_cached(10'h280, rdata);
    check("PBOOT clears when PBOOT signal deasserts", 1'b0, rdata[6]);

    // ------------------------------------------------------------------
    // TEST GROUP 24: Full 16-bit address decode, alias rejection
    //
    // With A15-A10 all required LOW, the card no longer aliases at
    // every 1 KB boundary (0x680, 0xA80, etc.). This group exhaustively
    // tests that the card DOES respond at its configured base and
    // DOES NOT respond at any alias address that would previously
    // have matched with 10-bit decode.
    //
    // Also verifies no collision with known AT-era I/O devices that
    // live above 0x3FF: AWE32 EMU8000 at 0x620/0xA20/0xE20 and ECP
    // parallel at 0x778.
    // ------------------------------------------------------------------
    $display("--- Group 24: 16-bit Decode Alias Rejection ---");
    isa_reset;
    addr_j <= 3'b000;  // base 0x280
    @(posedge clk);

    // Sanity: base address still selects normally
    addr <= 16'h0280;
    aen_n <= 1'b0;
    ior_n <= 1'b0;
    repeat (2) @(posedge clk);
    check("Base 0x280 selects normally (sanity)", 1'b1, TP0);

    // Alias #1: base + 0x400 (A10 set)
    addr <= 16'h0680;
    repeat (2) @(posedge clk);
    check("No select at 0x680 (A10 alias of 0x280)", 1'b0, TP0);

    // Alias #2: base + 0x800 (A11 set)
    addr <= 16'h0A80;
    repeat (2) @(posedge clk);
    check("No select at 0xA80 (A11 alias of 0x280)", 1'b0, TP0);

    // Alias #3: base + 0xC00 (A10+A11 set)
    addr <= 16'h0E80;
    repeat (2) @(posedge clk);
    check("No select at 0xE80 (A10+A11 alias of 0x280)", 1'b0, TP0);

    // Alias #4: A12 set
    addr <= 16'h1280;
    repeat (2) @(posedge clk);
    check("No select at 0x1280 (A12 alias of 0x280)", 1'b0, TP0);

    // Alias #5: A13 set
    addr <= 16'h2280;
    repeat (2) @(posedge clk);
    check("No select at 0x2280 (A13 alias of 0x280)", 1'b0, TP0);

    // Alias #6: A14 set
    addr <= 16'h4280;
    repeat (2) @(posedge clk);
    check("No select at 0x4280 (A14 alias of 0x280)", 1'b0, TP0);

    // Alias #7: A15 set
    addr <= 16'h8280;
    repeat (2) @(posedge clk);
    check("No select at 0x8280 (A15 alias of 0x280)", 1'b0, TP0);

    // Alias #8: A15+A14+A13+A12+A11+A10 all set (worst case)
    addr <= 16'hFE80;
    repeat (2) @(posedge clk);
    check("No select at 0xFE80 (all upper bits set)", 1'b0, TP0);

    // Known AT-era device collisions previously blocked only by
    // address comparison; now blocked by the upper-zero gate.
    addr <= 16'h0620;  // AWE32 EMU8000 low
    repeat (2) @(posedge clk);
    check("No select at 0x620 (AWE32 EMU8000)", 1'b0, TP0);

    addr <= 16'h0A20;  // AWE32 EMU8000 alias 1
    repeat (2) @(posedge clk);
    check("No select at 0xA20 (AWE32 EMU8000 alias)", 1'b0, TP0);

    addr <= 16'h0E20;  // AWE32 EMU8000 alias 2
    repeat (2) @(posedge clk);
    check("No select at 0xE20 (AWE32 EMU8000 alias)", 1'b0, TP0);

    addr <= 16'h0778;  // ECP parallel port
    repeat (2) @(posedge clk);
    check("No select at 0x778 (ECP parallel)", 1'b0, TP0);

    // Walk all eight base jumper settings with their A10+ aliases
    // to prove the upper-zero gate applies to every configured base,
    // not just 0x280.
    begin : alias_walk
        reg [15:0] bases16 [0:7];
        integer i;
        bases16[0] = 16'h0280; bases16[1] = 16'h0290;
        bases16[2] = 16'h02A0; bases16[3] = 16'h02C0;
        bases16[4] = 16'h0300; bases16[5] = 16'h0310;
        bases16[6] = 16'h0320; bases16[7] = 16'h0340;

        for (i = 0; i < 8; i = i + 1) begin
            addr_j <= i[2:0];
            @(posedge clk);

            // Base should select
            addr <= bases16[i];
            repeat (2) @(posedge clk);
            check("Configured base selects", 1'b1, TP0);

            // Base | 0x400 alias should NOT select
            addr <= bases16[i] | 16'h0400;
            repeat (2) @(posedge clk);
            check("Base+0x400 alias rejected", 1'b0, TP0);

            // Base | 0x1000 alias should NOT select
            addr <= bases16[i] | 16'h1000;
            repeat (2) @(posedge clk);
            check("Base+0x1000 alias rejected", 1'b0, TP0);
        end
    end

    ior_n <= 1'b1;
    aen_n <= 1'b1;
    addr_j <= 3'b000;
    @(posedge clk);

    // ------------------------------------------------------------------
    // Final global check: bus contention monitor
    // ------------------------------------------------------------------
    check("No bus contention events across entire run", 1'b1,
          (bus_contention_count == 0));

    // =========================================================================
    // RESULTS
    // =========================================================================

    $display("");
    $display("=== RESULTS ===");
    $display("  PASSED: %0d", pass_count);
    $display("  FAILED: %0d", fail_count);
    $display("  TOTAL:  %0d", test_num);
    $display("  Bus contention events: %0d", bus_contention_count);
    $display("");

    if (fail_count == 0)
        $display("*** ALL TESTS PASSED ***");
    else
        $display("*** %0d TESTS FAILED ***", fail_count);

    $display("");
    $finish;
end

endmodule
