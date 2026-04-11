// NetISA Rev A - ATF1508AS CPLD Logic (Verilog)
//
// Quartus II 13.0sp1 target: EPM7128SLC84-15 (pin-compatible with ATF1508AS)
// Convert output .pof to .jed via POF2JED with: JTAG=ON, TDI_PULLUP=ON, TMS_PULLUP=ON
//
// ISA crypto/transport coprocessor bus interface.
// Implements: address decode, data path, IOCHRDY wait-state controller,
//             IRQ state machine, parallel bridge to ESP32-S3, register cache.
//
// Design principles (post red-team, 5 external reviews):
//   - Separate ISA and ESP32 data paths (no bus multiplex)
//   - 2-flop synchronizers on all async ESP32 signals
//   - Fully registered IOCHRDY assert/release
//   - Status flag (XFER_TIMEOUT) on watchdog expiry (no in-band error byte)
//   - Hardware flags merged into Status Register output (bits 3, 5, 6)
//   - Proper IRQ state machine for XT edge + AT level PICs
//
// Target: Microchip ATF1508AS-10AU100 (TQFP-100, 128 MC, 5V, 10ns)
// Clock:  16 MHz external oscillator on GCLK1
//
// NOTE: Pin numbers in port comments below are PLCC-84 assignments from the
// original design. These MUST be remapped to TQFP-100 pin numbers using the
// ATF1508AS datasheet (Table 3-5) before running the Quartus fitter.
// The Quartus target device is EPM7128STC100-15 (TQFP-100 equivalent).
// Logical design is identical between packages; only physical pins change.

module netisa (
    // Clock
    input  wire        CLK,        // pin 83 GCLK1: 16 MHz

    // ISA Bus: Address (full 16-bit decode, A15-A0)
    // A0-A9 decode base + register within the 16-port window.
    // A10-A15 MUST all be LOW for chip_sel. This prevents aliasing at
    // every 1 KB boundary (0x680, 0xA80, 0xE80, 0x1280, ...), which
    // would otherwise collide with AWE32 EMU8000 (0x620/0xA20/0xE20),
    // ECP parallel (0x778), and other I/O above 0x3FF on AT+ systems.
    input  wire        A0,         // pin 4
    input  wire        A1,         // pin 5
    input  wire        A2,         // pin 6
    input  wire        A3,         // pin 9
    input  wire        A4,         // pin 11
    input  wire        A5,         // pin 12
    input  wire        A6,         // pin 13
    input  wire        A7,         // pin 15
    input  wire        A8,         // pin 16
    input  wire        A9,         // pin 17
    input  wire        A10,        // pin TBD (Quartus auto-assigns)
    input  wire        A11,        // pin TBD
    input  wire        A12,        // pin TBD
    input  wire        A13,        // pin TBD
    input  wire        A14,        // pin TBD
    input  wire        A15,        // pin TBD

    // ISA Bus: Data (active-high, accent when read, accept when write)
    inout  wire [7:0]  D,          // pins 18,19,21,23,24,25,27,28

    // ISA Bus: Control (active LOW accent accent accent, accent handled by inversion below)
    input  wire        AEN_n,      // pin 29, active LOW = CPU cycle
    input  wire        IOR_n,      // pin 30, active LOW
    input  wire        IOW_n,      // pin 31, active LOW
    output wire        IOCHRDY,    // pin 33, drive LOW to insert wait states
    input  wire        RESET_n,    // pin 35, active LOW (after RC + Schmitt)
    output wire        IRQ_OUT,    // pin 36, active HIGH
    output wire        IOCS16_n,   // pin 37, active LOW

    // ESP32 Parallel Bus: Data
    inout  wire [7:0]  PD,         // pins 39-47

    // ESP32 Parallel Bus: Control
    output wire [3:0]  PA,         // pins 49-52, register address
    output wire        PRW,        // pin 53, 1=read-from-ESP32
    output wire        PSTROBE,    // pin 54, active LOW pulse
    input  wire        PREADY,     // pin 55, data valid from ESP32
    input  wire        PIRQ,       // pin 57, interrupt request from ESP32
    input  wire        PBOOT,      // pin 58, boot complete from ESP32

    // Configuration
    input  wire [2:0]  ADDR_J,     // pins 59,61,62 DIP switch
    input  wire        SAFE_MODE,  // pin 63, HIGH = safe mode
    input  wire        IRQ_SENSE,  // pin 64, HIGH = IRQ enabled
    input  wire        SLOT16_n,   // pin 65, LOW = 16-bit slot

    // Test Points
    output wire        TP0,        // pin 66
    output wire        TP1         // pin 67
);

// =========================================================================
// Internal signal naming (active-high versions of active-low inputs)
// =========================================================================
wire AEN   = ~AEN_n;
wire IOR   = ~IOR_n;
wire IOW   = ~IOW_n;
wire RESET = ~RESET_n;

// =========================================================================
// ADDRESS DECODE (combinational)
//
// Full 16-bit I/O decode: A15-A10 must all be LOW (all base addresses
// are below 0x400), A9-A4 must match the jumper-selected base, and
// A3-A0 select register within the 16-port window.
//
// IMPORTANT: Do NOT include A3 in chip_sel or registers 0x08-0x0F are
// invisible.
//
// Verified patterns (A9..A4):
//   000: 101000 = 0x280    100: 110000 = 0x300
//   001: 101001 = 0x290    101: 110001 = 0x310
//   010: 101010 = 0x2A0    110: 110010 = 0x320
//   011: 101100 = 0x2C0    111: 110100 = 0x340
//
// Requiring A15..A10 all LOW prevents aliasing at every 1 KB boundary
// (0x680, 0xA80, 0xE80, 0x1280, ...), which on AT+ systems would
// otherwise collide with AWE32 EMU8000 (0x620/0xA20/0xE20), ECP
// parallel (0x778), and any other device with A10 or higher set.
// =========================================================================

wire [5:0] addr_upper = {A9, A8, A7, A6, A5, A4};
wire       upper_zero = ~(A10 | A11 | A12 | A13 | A14 | A15);

reg [5:0] base_pattern;
always @(*) begin
    case (ADDR_J)
        3'b000: base_pattern = 6'b101000; // 0x280
        3'b001: base_pattern = 6'b101001; // 0x290
        3'b010: base_pattern = 6'b101010; // 0x2A0
        3'b011: base_pattern = 6'b101100; // 0x2C0
        3'b100: base_pattern = 6'b110000; // 0x300
        3'b101: base_pattern = 6'b110001; // 0x310
        3'b110: base_pattern = 6'b110010; // 0x320
        3'b111: base_pattern = 6'b110100; // 0x340
    endcase
end

wire base_match = (addr_upper == base_pattern);
wire chip_sel   = base_match & AEN & upper_zero;

// Register select (A3-A0)
wire [3:0] reg_sel = {A3, A2, A1, A0};

wire is_reg00 = chip_sel & (reg_sel == 4'h0); // Status/Command
wire is_reg01 = chip_sel & (reg_sel == 4'h1); // Resp/Cmd Len Lo
wire is_reg02 = chip_sel & (reg_sel == 4'h2); // Resp/Cmd Len Hi
wire is_reg04 = chip_sel & (reg_sel == 4'h4); // Data In/Out
wire is_reg05 = chip_sel & (reg_sel == 4'h5); // Data Hi (16-bit)
wire is_reg06 = chip_sel & (reg_sel == 4'h6); // Error Code
wire is_reg07 = chip_sel & (reg_sel == 4'h7); // FW Major / Reset
wire is_reg08 = chip_sel & (reg_sel == 4'h8); // FW Minor
wire is_reg09 = chip_sel & (reg_sel == 4'h9); // FW Patch
wire is_reg0A = chip_sel & (reg_sel == 4'hA); // Session Count
wire is_reg0B = chip_sel & (reg_sel == 4'hB); // Session Capacity
wire is_reg0C = chip_sel & (reg_sel == 4'hC); // Network Status
wire is_reg0D = chip_sel & (reg_sel == 4'hD); // Signal Quality

// Cached registers: served from CPLD latches, zero wait states
wire cache_hit = is_reg00 | is_reg06 | is_reg07 | is_reg08 | is_reg09
               | is_reg0A | is_reg0B | is_reg0C;

// Reserved registers 0x03, 0x0E, 0x0F are intentionally NOT in cache_hit.
// They fall through to cache_miss_read, which makes the ESP32 the sole
// arbiter of their semantics across firmware versions. In v1 (Session
// Mode) firmware, reads return 0x00. In v2+ (NIC Mode) firmware, they
// carry NIC packet length and TX/RX control without requiring any CPLD
// changes. See architecture spec section 2.6.1 "Driver Modes".

wire cache_miss_read = chip_sel & IOR & ~cache_hit;
wire irq_ack = is_reg00 & IOR;

// =========================================================================
// 2-FLOP SYNCHRONIZERS (16 MHz clock domain)
// =========================================================================

reg pready_meta, pready_sync;
reg pirq_meta,   pirq_sync;
reg pboot_meta,  pboot_sync;

always @(posedge CLK) begin
    pready_meta <= PREADY;
    pready_sync <= pready_meta;
    pirq_meta   <= PIRQ;
    pirq_sync   <= pirq_meta;
    pboot_meta  <= PBOOT;
    pboot_sync  <= pboot_meta;
end

// =========================================================================
// IOW EDGE DETECTOR
// =========================================================================

reg iow_prev;
always @(posedge CLK or posedge RESET) begin
    if (RESET)
        iow_prev <= 1'b0;
    else
        iow_prev <= IOW;
end

wire iow_rising    = IOW & ~iow_prev;
wire write_strobe  = iow_rising & chip_sel;

// =========================================================================
// ISA WRITE DATA LATCH (host -> card)
// =========================================================================

reg [7:0] isa_in_latch;

always @(posedge CLK or posedge RESET) begin
    if (RESET)
        isa_in_latch <= 8'h00;
    else if (write_strobe)
        isa_in_latch <= D;
end

// =========================================================================
// IOCHRDY WAIT STATE CONTROLLER
// =========================================================================

reg        read_pending;
reg        iochrdy_hold;
reg  [7:0] watchdog;

// Watchdog timeout: counter reached 160 (0xA0 = 10us at 16 MHz)
wire wdog_timeout = (watchdog == 8'hA0) & iochrdy_hold;

// ESP32 responded successfully
wire esp_data_valid = read_pending & pready_sync;

always @(posedge CLK or posedge RESET) begin
    if (RESET) begin
        read_pending <= 1'b0;
        iochrdy_hold <= 1'b0;
        watchdog     <= 8'h00;
    end else begin
        // read_pending: set on cache miss, clear on response or timeout
        if (cache_miss_read & ~read_pending)
            read_pending <= 1'b1;
        else if (esp_data_valid | wdog_timeout)
            read_pending <= 1'b0;

        // iochrdy_hold tracks read_pending
        if (cache_miss_read & ~read_pending)
            iochrdy_hold <= 1'b1;
        else if (esp_data_valid | wdog_timeout)
            iochrdy_hold <= 1'b0;

        // Watchdog counter
        if (read_pending & ~wdog_timeout)
            watchdog <= watchdog + 8'h01;
        else
            watchdog <= 8'h00;
    end
end

// IOCHRDY: open-drain style. Drive LOW when holding, tri-state otherwise.
assign IOCHRDY = iochrdy_hold ? 1'b0 : 1'bz;

// =========================================================================
// XFER_TIMEOUT FLAG
// =========================================================================

reg xfer_timeout;

// Host clears by writing 0x20 to command register (base+0x00)
wire cmd_clear_xfer = write_strobe & is_reg00
                    & D[5] & ~D[7] & ~D[6] & ~D[4] & ~D[3] & ~D[2] & ~D[1] & ~D[0];

always @(posedge CLK or posedge RESET) begin
    if (RESET)
        xfer_timeout <= 1'b0;
    else if (wdog_timeout)
        xfer_timeout <= 1'b1;
    else if (cmd_clear_xfer)
        xfer_timeout <= 1'b0;
end

// =========================================================================
// ISA OUTPUT LATCH + STATUS REGISTER HARDWARE FLAG MERGE
//
// The output latch holds ESP32-pushed data for cached register reads.
// When reading the Status Register (port 0x00), hardware flags are
// OR'd into the appropriate bits:
//   Bit 3: SAFE_MODE (from jumper J3)
//   Bit 5: xfer_timeout (CPLD watchdog flag)
//   Bit 6: pboot_sync (ESP32 boot complete)
// =========================================================================

reg [7:0] isa_out_latch;

always @(posedge CLK or posedge RESET) begin
    if (RESET)
        isa_out_latch <= 8'h00;
    else if (esp_data_valid)
        isa_out_latch <= PD;
end

// Merged output: OR hardware flags into status register reads
wire [7:0] status_merged = {
    isa_out_latch[7],
    isa_out_latch[6] | pboot_sync,     // bit 6: BOOT_COMPLETE
    isa_out_latch[5] | xfer_timeout,   // bit 5: XFER_TIMEOUT
    isa_out_latch[4],
    isa_out_latch[3] | SAFE_MODE,      // bit 3: SAFE_MODE
    isa_out_latch[2:0]
};

// Select merged status for reg00, raw latch for all other cached regs
wire [7:0] read_data = is_reg00 ? status_merged : isa_out_latch;

// ISA data bus: drive during reads, tri-state otherwise
assign D = (chip_sel & IOR) ? read_data : 8'bz;

// =========================================================================
// IRQ STATE MACHINE
//
// States: IDLE(00), PENDING(01), PRESENTED(10), DEAD(11)
// Dead state provides 4-cycle (250ns) low time for XT 8259 edge re-trigger.
// =========================================================================

localparam S_IDLE    = 2'b00;
localparam S_PENDING = 2'b01;
localparam S_PRESENT = 2'b10;
localparam S_DEAD    = 2'b11;

reg [1:0] irq_state;
reg [1:0] irq_dead;
reg       pirq_prev;

wire pirq_rising = pirq_sync & ~pirq_prev;

always @(posedge CLK or posedge RESET) begin
    if (RESET) begin
        irq_state <= S_IDLE;
        irq_dead  <= 2'b00;
        pirq_prev <= 1'b0;
    end else begin
        pirq_prev <= pirq_sync;

        case (irq_state)
            S_IDLE: begin
                if (pirq_rising)
                    irq_state <= S_PENDING;
            end
            S_PENDING: begin
                irq_state <= S_PRESENT;
            end
            S_PRESENT: begin
                if (irq_ack)
                    irq_state <= S_DEAD;
            end
            S_DEAD: begin
                irq_dead <= irq_dead + 2'b01;
                if (irq_dead == 2'b11) begin
                    if (pirq_sync)
                        irq_state <= S_PENDING;
                    else
                        irq_state <= S_IDLE;
                end
            end
        endcase

        // Reset dead counter when entering DEAD state
        if (irq_state != S_DEAD)
            irq_dead <= 2'b00;
    end
end

// IRQ output: HIGH during PENDING and PRESENTED only
assign IRQ_OUT = IRQ_SENSE ? ((irq_state == S_PENDING) | (irq_state == S_PRESENT))
                           : 1'bz;

// =========================================================================
// PARALLEL BRIDGE: ESP32-facing signals
// =========================================================================

// Register address (directly from ISA A0-A3 during active cycle)
assign PA = {A3, A2, A1, A0};

// Read/Write direction
assign PRW = IOR & chip_sel;

// ESP32 data bus: drive write latch during writes, tri-state during reads
wire pd_drive = chip_sel & ~PRW;
assign PD = pd_drive ? isa_in_latch : 8'bz;

// Strobe generation (registered, synchronous)
reg strobe_req, strobe_phase;

always @(posedge CLK or posedge RESET) begin
    if (RESET) begin
        strobe_req   <= 1'b0;
        strobe_phase <= 1'b0;
    end else begin
        strobe_req   <= (write_strobe | cache_miss_read) & ~strobe_req;
        strobe_phase <= strobe_req;
    end
end

assign PSTROBE = ~strobe_req;  // Active LOW, one CLK period (62.5ns)

// =========================================================================
// IOCS16# - 16-BIT SLOT SUPPORT
// =========================================================================

wire iocs16_active = chip_sel & ~SLOT16_n & (is_reg04 | is_reg05);
assign IOCS16_n = iocs16_active ? 1'b0 : 1'bz;

// =========================================================================
// TEST POINTS
// =========================================================================

assign TP0 = chip_sel;
assign TP1 = iochrdy_hold;

endmodule
