# Executive Summary  
AC’97-era PCI sound chips (Yamaha YMF7xx, ESS Solo-1/ES18xx, Crystal CS4xxx/CS46xx, etc.) combined a legacy Sound Blaster–compatible block with an AC’97 host and codec. Initializing them requires both powering up the AC’97 codec link and any vendor-specific controls in PCI config space. In practice this means (1) enabling the PCI device and disabling its reset lines (Yamaha’s DS-1 Control reg, ESS/Crystal equivalents), (2) de-asserting AC’97 reset and clocks, (3) writing the AC’97 Global Control reg (e.g. 0x2) to take the codec out of reset【44†L318-L326】, and (4) programming codec mixers (master volume reg 0x02→0)【75†L19-L22】. Vendors add quirks: e.g. YMF7xx PCI config bits CRST/XRST must be cleared (CRST=XRST=0)【69†L1189-L1196】, clocks enabled, and power-down bits cleared. ESS and Crystal chips have analogous reset and power registers. After AC’97 wake-up, typical steps include setting PCM sample rates and volumes, then starting the AC’97 bus-master DMA. We present concrete init code examples (pseudocode/C) with exact registers, a compatibility matrix of features/quirks, flow diagrams of the init sequence, and links to reference manuals and driver code.  

## Target Hardware and AC’97 Roles  
- **Yamaha YMF7xx series (e.g. YMF724F/DS-1)** – PCI audio controller with two blocks: a “Legacy Audio” SBPro/OPL3/MPU401 block and a PCI audio engine. It interfaces to an external AC’97 codec via the AC-Link【33†L23-L32】. The PCI config register 0x44 (DS-1 Control) contains CRST (bit0) and XRST (bit1); both default to “1” (assert reset)【69†L1189-L1196】. To initialize, clear these bits (CRST=0, XRST=0) to bring AC’97 and the chip out of reset. Its PCI reg 0x4A (DS-1 Power Control) has AC’97 power bits PR0–PR3; clear them (0=normal) to power up ADC/DAC/mixers【69†L1199-L1207】. Ensure DMC=0 to enable the 24.576 MHz master clock. After that, use the AC’97 controller to reset the codec (see below). Yamaha’s chips emulate SBPro/MPU in real mode (for DOS), but AC’97 audio is fully PCI master.  

- **ESS Solo-1 / ES18xx series** – PCI AC’97 controllers (e.g. on ASUS/Acer OEM boards) often marketed as “ESS Solo-1 AudioDrive” (ES1868F) or similar. These provide SB16/OPL3 compatibility and an AC’97 codec. They have PCI config registers to control the AC’97 link (vendor-specific). (In DOS, they appear as ESS16SX/OAK; software like ESSVIB.DRV handles them.) Initialization is similar: disable any reset bit (often in the PCI I/O or config space), then write AC’97 globals. ESS chips typically use offset 0x04/0x06 for SB reset/status (legacy), and their AC’97 registers via PCI base addresses.  

- **Crystal CS4xxx/CS46xx series (SoundFusion)** – These PCI chips (e.g. CS4236, CS4248, CS4610 DSP + CS4297AC97 codec) also support AC’97 codecs. For purely AC’97 audio, the key is toggling the controller reset (often at a control register in PCI space) and the codec reset. For example, CS461x datasheet shows connecting to a CS4297 AC’97 codec. Legacy SB support often used a separate CS423x codec. For CS46xx, chipset manuals (if available) must be consulted for reset bits. Generally, the AC’97 init steps match the OSDev guide【44†L318-L326】.  

## PCI Probing and Configuration  
To locate these devices in DOS/early Windows:  
1. **PCI Class/ID** – Scan PCI bus (e.g. via INT 1Ah PCI services or _FindAdapter callbacks) for class 0x04 (Multimedia), subclass 0x01 (audio)【44†L318-L326】. Check VendorID/DeviceID: Yamaha=0x1073 (YMF724F), ESS=0x125D, Crystal=0x1013, etc.  
2. **Base Registers** – Read PCI config BARs (0x10/0x14) for I/O or memory bases. For AC’97 controllers, BAR0 often points to the “AC97 legacy audio” registers (SB/OPLI/O), BAR1/BAR2 for AC’97 DMA (NAM/NABM) or configuration registers. For example, YMF724 uses BAR0 for Legacy I/O and BAR1 for AC’97 mixers/DMA (see datasheet). Map these into the CPU’s address space.  

3. **Disable Legacy Reset** – Many boards have a jumper or BIOS setting to enable “legacy ISA emulation.” For direct programming, ensure the chip is not held in reset by external logic. Then, in PCI config space:  
   - **Yamaha YMF724 (DS-1):** Write to PCI Config **0x44** (CRST=0, XRST=0)【69†L1189-L1196】. Example: `outportb(PCI_CFG_ADDR, 0x44); outportw(PCI_CFG_DATA, val & ~0x0003);` where bits 0-1 clear resets.  
   - **ESS Solo-1:** (for example ES1868F) Datasheet needed; typically a PCI I/O register (like base+0x80) controls ACLink reset. The ESS AudioDrive datasheet (not freely found) should be consulted for the AC’97 global control bit.  
   - **Crystal CS4236 (PnP) or CS4610:** Use vendor spec: e.g. on CS4610 the AC’97 reset might be a control bit via PCI config or a GPIO. If unknown, use the AC’97 register approach below and assume reset lines are inactive by default.  

## AC’97 Global Initialization  
Once the controller is out of reset:  
- **Codec Reset/Power-Up:** Write AC’97 **Global Control** (codec reg 0x26 usually) to power up the link. Typically write `0x0002` (Resume Codec, no interrupts) or `0x0003` (with interrupts)【44†L318-L326】. This is done by writing to the AC’97 codec register via the controller’s I/O. For PCI AC’97, this usually means:  
  - Set the “index” for codec reg 0x26 (the Global Control reg) at the controller’s AC97 address port.  
  - Write the value 0x0002 to the controller’s AC97 data port.  
  - (Specific I/O ports vary by chipset; consult chip datasheet or use generic PCI AC97 index/data ports if defined.)  

- **Master Clock:** Ensure the AC’97 bit clock is running. On YMF7xx, this was DMC=0 in PCI reg (above). On ESS/Crystal, verify any “enable ACLink clock” bit is set.  

- **Wait Delays:** After de-asserting resets, allow ~0.2–1ms for clocks to stabilize. AC’97 specification suggests 100µs–200µs. For safety, delay ~1ms.  

- **Global Status:** Optionally read the AC’97 Global Status reg (0x00) to verify alive.  

## Mixer and Volume Setup  
- **Master Volume:** Set the AC’97 Master Volume register (codec reg 0x02) to 0x0000 for full volume【75†L19-L22】. Likewise, set PCM Output Volume (reg 0x18) to 0x0000 (codec offset 0x18), and unmute by clearing any mute bit (bit15 of each volume reg). Many drivers do this to avoid silence on boot.  
- **Record Source:** Set codec input select (0x1A) and MIC volume (0x0E) as needed (often default to MIC off).  
- **Line/Headphone:** If separate line-out, set that volume (codec reg 0x04) similarly.  

Example (pseudocode):  
```c
// Write to AC97 codec via controller I/O (example ports 0xF2C,0xF2E on YMF724)
outp(CTRL_ADDR, 0x02);               // select Master Vol reg
outp(CTRL_DATA, 0x00);
outp(CTRL_DATA, 0x00);               // 0x0000 = max vol
outp(CTRL_ADDR, 0x18);               // select PCM Out Vol reg
outp(CTRL_DATA, 0x00);
outp(CTRL_DATA, 0x00);
```  
*(Replace `CTRL_ADDR/CTRL_DATA` with the actual I/O ports for the chipset.)*  

## DMA/Buffer Setup  
After AC’97 is awake, enable audio streams:  
- Write zeros to all **AC’97 PCM Bass/Treble** registers if desired.  
- Set up PCI bus-master DMA (Buffer Descriptor Lists). For simplicity, one can use Legacy ISA-mode emulation or direct PCM-out programming. For true AC’97 DMA: use the Descriptor List format from OSDev【44†L293-L302】. At minimum, program the first buffer entry address/length in the controller’s NIC (NAM) registers, set “Last Entry” and set the “Run” bit. See OSDev or chipset guide.  
- **Note:** If DOS SB-compat is needed, the vendor’s legacy SB DSP emulation must be initialized (typically automatic after PCI BIOS or a small TSR). This is complex; often use the vendor’s DOS TSR. We focus on AC’97 path.  

## Vendor-Specific Quirks  
- **YMF7xx:** After the above PCI writes, polling the ACLink’s BUSY bit may be needed (bit13 of AC’97 global) before setting volumes【69†L1189-L1196】. The YMF724 also supports “PC/PCI” DMA bridging; ensure BIOS/CS does not disable it.  
- **ESS Solo-1 (ES18xx):** Some boards require setting a “legacy base address” via PCI config (I/O decode for legacy SB ports). In DOS, the ESS DOS driver normally does this. Without it, SB compatibility may fail. AC’97 init should follow the chipset datasheet (which may use standard Global Control=0x2).  
- **Crystal CS4236/CS4610:** The CS4610 datasheet shows ACLink to CS4297; that codec’s global reg is at offset 0x26 as usual. Ensure the chip’s PCI power-down bits (sometimes in CS4610 Page 4 reg) are cleared (if any). Some notebooks needing AC97 reset use a GPIO: drivers often toggle a “codec reset” bit twice.  

## Initialization Checklist and Diagnostics  
- **PCI Detect:** Verify the chipset is found (correct VID/DID).  
- **Power State:** Bring device to D0 (bus master, memory decode) if not already.  
- **Reset Release:** Write PCI config to clear legacy reset (as above).  
- **ACLink Clocks:** Enable AC97 bit clock (commonly automatic once out of reset).  
- **Global Wake:** Write 0x2 to AC’97 global control. Read back to ensure not stuck at “reset”.  
- **Volumes:** Program master and PCM volumes to 0x0000 (and clear mute).  
- **Wave test:** Send a simple wave (e.g. using a BIOS call or AC’97 DMA test) and verify output.  
- **Legacy SB Test:** If needed, load the vendor’s SB TSR and run `soundblaster.com` or similar.  

## Compatibility Matrix  

| Chipset / Feature      | AC’97 Codec Support | SB/SBPro Emulation | PCI Config Quirks           | Common Pitfall                                   |
|------------------------|---------------------|--------------------|-----------------------------|--------------------------------------------------|
| **YMF724 (Yamaha)**    | External AC’97 codec; 18-bit PCM | Yes (SBPro, MPU401 UART)【33†L23-L32】 | PCI 0x44: CRST/XRST reset bits (set 0)【69†L1189-L1196】; 0x4A: clear PR0-PR3 for power | Must disable CRST/XRST after PCI reset; ensure clocks (DMC=0) |
| **YMF740/744/754**     | Integrated AC’97 with resampler | Yes (SB16 DSP, 64-voice XG) | Similar DS-1 regs; check Vendor datasheet      | Resampler can introduce latency; have sample rate changes |
| **ESS Solo-1 (ES1868F)**| External AC’97 codec   | Yes (ESS16/C4 comp., MPU-401 UART) | Likely PCI bit for ACLink reset; enable PCI I/O space | Early DOS drivers needed; IRQ5/DA7 default conflicts |
| **ESS ES1888/889**     | Embedded AC’97 via software | Yes (ESS16 compat, OPL3-SA2) | BIOS often sets map; AC’97 reg via I/O port 0x20/0x22 | Requires ESSADR.DRV or WDM for DOS; known OPL3 conflicts |
| **Crystal CS4236**     | External AC’97 codec (CS4237/38/47) | Yes (SBPro, OPL3 in CS423x codec) | Possibly use PCI bit to enable AC’97 link (PLD); mixer via CS423x reg map | Some chips need “hidden” register unlock for mixer |
| **Cirrus CS4610+CS4297**| Uses CS4297 AC’97 codec | Legacy via CS423x (optional)  | CS4610 DSP control; CS4297 through ACLink | DSP must be loaded; codec I/O from DSP memory maps |
| **General (Others)**   | (Realtek, Analog Devices AC’97, etc.) | Depends on board | Standard AC97 Global/Status regs at AC’97 index/data | Ensure to follow AC’97 spec. Some require unplugging HP first. |

## Example Initialization Sequence  

```c
// Example pseudocode for YMF724 PCI AC'97 bring-up (DOS real mode)
// 1. PCI BIOS: find YMF724 device, get PCI config I/O port
int pcibios = 0x0CF8;  // write PCI address, then read at 0xCFC
// 2. Disable YMF7xx resets:
uint32_t address = (1<<31) | (bus<<16) | (slot<<11) | (func<<8) | 0x44;
outp(pcibios, address);
uint16_t ctrl = inpw(0xCFC);         // DS-1 Control reg
ctrl &= ~0x0003;                     // clear CRST and XRST bits
outp(pcibios, address);
outpw(0xCFC, ctrl);
// 3. Enable AC'97 clock (if needed) and power up codec (clearing PR bits)
address = (1<<31)|(bus<<16)|(slot<<11)|(func<<8)|0x4A;
outp(pcibios, address);
uint16_t power = inpw(0xCFC);
power &= ~(0x0F00);                  // clear PR0,PR1,PR2,PR3 (keep DMC=0)
outp(pcibios, address);
outpw(0xCFC, power);
// 4. Delay for reset de-assertion:
delay_ms(1);
// 5. Write AC'97 Global Control = 0x0002 via controller's AC97 I/O
outp(AC97_ADDR_PORT, 0x26);          // select AC'97 global ctrl reg (offset 0x26)
outp(AC97_DATA_PORT, 0x02);
outp(AC97_DATA_PORT, 0x00);
// 6. Delay for codec ready
delay_ms(1);
// 7. Set Master Volume to maximum (0x0000)
outp(AC97_ADDR_PORT, 0x02);
outp(AC97_DATA_PORT, 0x00);
outp(AC97_DATA_PORT, 0x00);
// 8. (Continue with PCM rate regs etc, then start DMA)
```

(Note: `AC97_ADDR_PORT`/`DATA_PORT` are chip-specific I/O ports. On YMF724 these are at e.g. 0xF2C/0xF2E by default; ESS might use 0x20/0x22.)  

## Initialization Flowchart  

```mermaid
flowchart TD
    A[Power on / Cold reset] --> B[PCI Reset de-asserted];
    B --> C[Locate audio PCI device (class 0x0401)];
    C --> D{Vendor ID?};
    D -->|Yamaha 0x1073| E[YMF7xx init: clear PCI CRST/XRST];
    D -->|ESS 0x125D| F[ESS Solo-1 init: clear reset bit];
    D -->|Crystal 0x1013| G[Crystal init: clear ACLink reset];
    E --> H[Clear AC'97 Power-down bits (PR0-3)];
    F --> H;
    G --> H;
    H --> I[Delay ~1ms];
    I --> J[Write AC'97 Global Control = 0x2 (wake codec)];
    J --> K[Delay ~1ms];
    K --> L[Set Codec Master Volume=0x0];
    L --> M[Program PCM sample rates/volumes];
    M --> N[Setup DMA Buffer Descriptor List];
    N --> O[Enable AC'97 playback (set run bit)];
    O --> P[Sound should play]; 

classDef found fill:#afa;
classDef reset fill:#faa;
classDef config fill:#ffd;
classDef done fill:#dfd;
class H,J,K,L,M,N,O,P found;
class E,F,G config;
```

## Example References  
- **Yamaha DS-1 / YMF724 Datasheet:** Shows PCI config bits CRST/XRST【69†L1189-L1196】 and AC’97 interface. (Chip 0x1073 ID, base regs)【69†L1189-L1196】.  
- **AC’97 Programming Guide (Intel):** Describes Global Control = 0x2 to resume codecs and setting volume regs【44†L318-L326】【75†L19-L22】.  
- **Open-source Drivers:** ALSA OSS (`snd-yamaha`, `snd-esssolo1`, `snd-oxygen`) and Linux kernel sources contain init sequences (search for `AC97_RST` patterns). E.g., Yamaha’s AC’97 reg writes in *sound/isa/via82cxxx.c*.  
- **Phil’s Computer Lab and Vogons:** Community write-ups on YMF724 DOS drivers highlight that the card uses PCI and requires SB-Emulation (or SBEMU) for DOS.

## Testing and Diagnostics  
- Use a PCI bus viewer (e.g. Linux lspci) to verify device present.  
- In DOS, tools like `PCI.EXE` (U-BIOS) can be used to poke PCI config.  
- After init, try simple sound (e.g. BIOS speaker tone via AC’97) to confirm.  
- For SB compatibility, run `DIAGS` or `SOUND.COM` TSRs included with the card’s DOS driver.  

By following the above sequences and checking each step (reset bit, clock, codec response, volumes), one can reliably bring up AC’97 PCI audio on DOS/Win9x platforms. Always refer to the specific chipset datasheet for register offsets and bit meanings【69†L1189-L1196】【44†L318-L326】. 

