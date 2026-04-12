# NetISA Phase 0 Build Log

## 2026-04-11: Pre-Hardware Preparation Complete

### Toolchain Validated

| Tool | Version | Status | Notes |
|------|---------|--------|-------|
| Quartus II | 13.0sp1 Web Edition | Working | Requires Windows Security Smart App Control OFF. Add `C:\altera` to Defender exclusions. |
| POF2JED | 4.45.1 (Mar 2018) | Working | Settings: JTAG=ON, TDI_PULLUP=ON, TMS_PULLUP=ON |
| ESP-IDF | v5.5.4 | Working | Required firmware fixes for v5.5 GPIO API changes |
| NASM | 3.01 | Working | Installs to `C:\Program Files\NASM`, not in PATH by default |
| KiCad | 10 | Installed | Not needed until Phase 6 |
| Git | Installed | Working | Repo: https://github.com/tonyuatkins-afk/NetISA |

### Build Artifacts Ready

| Artifact | Path | Size | Notes |
|----------|------|------|-------|
| CPLD JEDEC | phase0/cpld/output_files/netisa.jed | - | 95/128 macrocells (74%), EPM7128STC100-15 target |
| CPLD POF | phase0/cpld/output_files/netisa.pof | - | Quartus output, input to POF2JED |
| ESP32 firmware | phase0/firmware/build/netisa_phase0.bin | 208KB | 80% partition free |
| DOS test | phase0/dos/nisatest.com | 1,093 bytes | .COM file, transfer to floppy/CF |

### ESP-IDF v5.5 Compatibility Fixes Applied

The firmware was originally written for ESP-IDF v5.x (pre-5.5). Two breaking changes in v5.5.4:

1. **GPIO register API:** `GPIO.in.val` changed to `GPIO.in` (`.val` sub-member removed from all GPIO register structs). Affected 9 locations in main.c. Fix: remove `.val` from all `GPIO.in`, `GPIO.out_w1ts`, `GPIO.out_w1tc`, `GPIO.enable_w1ts`, `GPIO.enable_w1tc` accesses.

2. **Core dump API:** `esp_core_dump_summary_t` struct and `esp_core_dump_get_summary()` were restructured. Fix: replaced `init_crashlog()` with a stub that directs users to `idf.py coredump-info`. Non-essential for Phase 0.

3. **sdkconfig.defaults:** `CONFIG_ESP_SYSTEM_PANIC` renamed in v5.5 (warning only). `CONFIG_PARTITION_TABLE_CUSTOM=y` requires a `partitions.csv` that doesn't exist (error). Fix: removed both lines, default partition table is fine for Phase 0.

### Windows 11 25H2 Gotchas

- **Smart App Control** blocks unsigned DLLs in Quartus II 13.0sp1 (`atm_a7k.dll`, `cbx_lpm_counter.dll`). Defender folder exclusions do NOT help. Must disable Smart App Control entirely: Windows Security > App & browser control > Smart App Control > Off. This is permanent (cannot re-enable without resetting Windows).

- **PowerShell 7.x** does not have `Add-MpExclusion` cmdlet. Use the Windows Security GUI instead: Virus & threat protection > Manage settings > Exclusions.

### Quartus Compilation Results

- **Device:** EPM7128STC100-15 (TQFP-100, register-compatible with ATF1508AS-10AU100)
- **Macrocells:** 95 of 128 used (74%) (post 16-bit address decode)
- **I/O pins:** 61 of 84 used
- **Shareable expanders:** 7
- **Errors:** 0
- **Warnings:** 5 (all harmless: 4 unused signals, 1 missing SDC)
- **Timing:** +39.5ns setup slack, +5.0ns hold slack, +26.25ns min pulse width at 16 MHz (SDC constraint loaded). Zero total negative slack.

### Synthesis Warnings (Expected)

| Warning | Signal | Reason |
|---------|--------|--------|
| assigned but never read | is_reg01 | Reserved for non-cached read path, used implicitly via !cache_hit |
| assigned but never read | is_reg02 | Same as above |
| assigned but never read | is_reg0D | Same as above |
| assigned but never read | strobe_phase | One-cycle delay, ended up unused in Phase 0 |

### Parts Ordered

| Source | Order Date | Items | Est. Delivery |
|--------|-----------|-------|---------------|
| DigiKey | 2026-04-11 | CPLDs, ESP32 devkit, level shifters, buffers, oscillator, passives, JTAG programmer, breakout boards | 2-3 days |
| TexElec | 2026-04-11 | 2x 8-bit ISA Prototype Card v1.0 | ~1 week |
| Amazon | 2026-04-11 | QFP breakout boards, breadboard kit, antenna, ribbon cables, headers, jumper caps | 1-2 days |
| DreamSourceLab | 2026-04-11 | DSLogic Plus logic analyzer (16ch, 400MHz) | 1-2 weeks |

### Through-Hole Strategy

All passives (resistors, capacitors, TVS diode) ordered as through-hole to simplify prototyping. Only three SMD soldering jobs required:

1. ATF1508AS-10AU100 (TQFP-100, 0.5mm pitch) onto QFP breakout board
2. SN74LVC8T245PWR (TSSOP-24, 0.65mm pitch) onto PA0036 breakout board
3. SN74LVC8T245PWR (TSSOP-24, 0.65mm pitch) onto PA0036 breakout board

### Code Review Summary (Claude Code)

Full review passed. No bugs found. Three issues flagged, none blocking Phase 0:

1. PLCC-84 pin numbers in Verilog comments (informational only, Quartus auto-assigns TQFP-100 pins)
2. CUPL status register dead node (Verilog is primary path, CUPL is backup)
3. Missing .sdc file (added post-review)

### Next Steps (When Parts Arrive)

1. Read CPLD pin assignments from Quartus fitter report
2. Solder TSSOP-24 level shifters onto PA0036 breakout boards (2 jobs)
3. Solder TQFP-100 CPLD onto QFP breakout board (1 job)
4. Wire prototype per phase0/README.md wiring guide
5. Program CPLD via ATDH1150USB JTAG
6. Flash ESP32 firmware: `idf.py -p COMx flash monitor`
7. Boot DOS machine, run NISATEST.COM
8. Walk the 9-gate validation checklist with logic analyzer
