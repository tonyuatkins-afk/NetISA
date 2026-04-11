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
reg  [9:0] addr;       // A9..A0
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

// =========================================================================
// DUT instantiation
// =========================================================================

netisa dut (
    .CLK(clk),
    .A0(addr[0]), .A1(addr[1]), .A2(addr[2]), .A3(addr[3]),
    .A4(addr[4]), .A5(addr[5]), .A6(addr[6]), .A7(addr[7]),
    .A8(addr[8]), .A9(addr[9]),
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
        reset_n   <= 1'b0;
        aen_n     <= 1'b1;  // CPU cycle (not DMA)
        ior_n     <= 1'b1;
        iow_n     <= 1'b1;
        addr      <= 10'h000;
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
    end
endtask

// Write a byte to an ISA port
task isa_write;
    input [9:0] port;
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
    input  [9:0] port;
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
    input  [9:0] port;
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
        repeat (3) @(posedge clk);  // Let sync propagate

        // IOCHRDY should be released now
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

    // =========================================================================
    // RESULTS
    // =========================================================================

    $display("");
    $display("=== RESULTS ===");
    $display("  PASSED: %0d", pass_count);
    $display("  FAILED: %0d", fail_count);
    $display("  TOTAL:  %0d", test_num);
    $display("");

    if (fail_count == 0)
        $display("*** ALL TESTS PASSED ***");
    else
        $display("*** %0d TESTS FAILED ***", fail_count);

    $display("");
    $finish;
end

endmodule
