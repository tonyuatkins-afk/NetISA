# NetISA

**The first WiFi and Ethernet ISA card.** Open-source secure networking for vintage PCs.

NetISA is an 8/16-bit ISA expansion card that provides TLS 1.3 cryptographic offloading, TCP/IP networking, WiFi, and Ethernet connectivity to IBM PC/XT/AT/386/486 systems running DOS. The host PC handles application logic; the card handles encryption, transport, DNS, and certificates.

## Status

**Phase 0: Parts ordered, awaiting hardware.** Architecture spec complete. CPLD logic fits 94/128 macrocells (Quartus II 13.0sp1, EPM7128STC100-15). JEDEC file generated via POF2JED. ESP32-S3 firmware builds clean on ESP-IDF v5.5.4. DOS loopback test assembled. Reviewed by five AI reviewers. All build artifacts ready to flash.

Next step: solder breakout boards, wire prototype, walk the 9-gate validation checklist.

## Hardware

- **Bus logic:** Microchip ATF1508AS CPLD (TQFP-100, 128 macrocells, 5V native)
- **MCU:** Espressif ESP32-S3-WROOM-1U-N8R8 (WiFi, hardware AES/SHA/RSA/ECC, 8MB flash, 8MB PSRAM)
- **Ethernet:** Wiznet W5500 (v1.5, optional)
- **Antenna:** External U.FL to bracket-mount RP-SMA (mandatory for metal PC cases)

## Software

- DOS TSR driver (~2KB resident) providing INT 63h API
- SDK: NETISA.H + NETISA.LIB (OpenWatcom) + NETISA_TC.LIB (Turbo C)
- PC/TCP Packet Driver for mTCP/WATTCP compatibility (v1.5)

## Repository Structure

```
docs/
  netisa-architecture-spec.md    Full architecture specification (2,800+ lines)
phase0/
  cpld/
    netisa.v                     Verilog source (Quartus II path, recommended)
    netisa.pld                   CUPL source (WinCUPL path, alternative)
  firmware/
    main/main.c                  ESP32-S3 Phase 0 loopback firmware
  dos/
    nisatest.asm                 DOS loopback test program (NASM)
  README.md                      Phase 0 wiring guide and validation checklist
```

## Building

See [Phase 0 README](phase0/README.md) for build instructions and wiring guide.

## License

MIT (software) / CERN-OHL-P (hardware). See [LICENSE](LICENSE).

## Author

Tony Atkins ([@tonyuatkins-afk](https://github.com/tonyuatkins-afk))
