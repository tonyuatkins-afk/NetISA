# HEARO Sound Card Reference

**Companion document:** `hearo-design.md`
**Audience:** developers working on `src/detect/audio.c` and the boot screen.
**Scope:** probe protocol for every period-appropriate DOS audio device HEARO targets.

Each device section covers:

1. Identity
2. Detection method (environment first, then I/O probe)
3. Ports and probe sequence
4. Capabilities reported in `hw_profile_t`
5. Boot screen line
6. Unlocks (`unlock_id_t` IDs)
7. Known false positives and pitfalls

Some devices share probe space (Covox vs Disney on LPT, AdLib vs AdLib Gold, SB16 vs AWE32 vs AWE64). Probe order matters and is called out where relevant.

---

## 0. Probe order

Three rounds:

1. **Always present.** PC Speaker. No probe; list unconditionally.
2. **Environment hinted.** `BLASTER`, `ULTRASND`, `MIDI`, `SOUND` strings point us at the right base. Confirm by I/O.
3. **Blind probes.** Tandy port C0h, AdLib at 388h, MPU 330h/300h, LPT (Covox/Disney), SoundScape, PAS-16, ESS, Turtle Beach, AdLib Gold extensions.

Environment first keeps probes targeted: a blind write to a port can hang machines that have nothing on it but happen to decode the address.

Reporting:

- Environment + I/O agree: `detected`.
- I/O only, no env hint: `detected (probe)`.
- Env claims a device but I/O fails: `not responding (BLASTER claims ...)`, AUD flag not set.

---

## 1. PC Speaker

**Identity.** Programmable Interval Timer channel 2 driving the on-board speaker. Universal on every IBM PC compatible from 1981.

**Detection.** None required. `AUD_PCSPEAKER` is set unconditionally.

**Capabilities.**
- 1 bit square wave at any frequency the PIT can produce.
- PWM, RealSound style, by toggling the speaker bit at high rate.
- 6 bit Sample Channel via Fast Sample Driver technique (Mode 3 PIT).

**Boot line.**

```
          PC Speaker (PIT ch2)                       detected
            RealSound PWM                              ENABLED
```

**Unlocks.** `UL_REALSOUND` (always available).

**Pitfalls.** None for detection. Pitfalls all live in the playback engine: PIT rate vs CPU clock, NMI safety, jitter on slow CPUs.

---

## 2. Tandy / PCjr 3 Voice PSG (SN76496)

**Identity.** Texas Instruments SN76496 in IBM PCjr (1984) and Tandy 1000 series (1984+). Three square wave voices plus a noise channel. Not present on standard PC compatibles.

**Detection.**

1. BIOS signature at `F000:FFFEh`: byte `FDh` indicates PCjr; byte `FFh` with the Tandy startup string elsewhere in ROM indicates Tandy 1000.
2. Confirm by writing the latch byte for tone 0 attenuation 0Fh (silence) to port C0h. Read back is undefined; hang detection means timing the write loop.

**Port.** `C0h` write only.

**Capabilities.**
- 3 tone voices + 1 noise.
- Frequency 110 Hz to 22 kHz.
- 16 attenuation steps per voice.

**Boot line.**

```
          Tandy / PCjr 3 voice PSG                    detected
            Tone + noise mixer                        ENABLED
```

**Unlocks.** `UL_TANDY_PSG`.

**Pitfalls.** Some XT clones decode C0h to the DMA controller. The BIOS signature gate is mandatory.

---

## 3. Covox Speech Thing

**Identity.** A passive 8 bit DAC that hangs off the parallel port data lines, sold by Covox Inc starting in 1986. Many homebrew clones exist.

**Detection.**

1. Read LPT base from BIOS data area at `0040:0008h` (LPT1) and `040Ah` (LPT2). Skip if zero.
2. Save current data port value.
3. Write a known pattern (e.g. `AAh`, `55h`, `00h`, `FFh`) to data port.
4. Read back via the printer port loopback (data port acts as latched output and input on most parallels). If readback matches, the bus is at least passively driven.
5. There is no positive Covox identification possible. We mark this as "probable" and let the user confirm in settings.

**Capabilities.**
- 8 bit unsigned mono PCM.
- Sample rate limited only by host CPU.

**Boot line.**

```
          Covox Speech Thing on LPT1 (probable)       detected
```

**Unlocks.** `UL_COVOX`.

**Pitfalls.** Covox cannot be distinguished from "nothing connected to LPT" by software alone. Only mark detected when the user opted in via config or `/COVOX=378` on the command line.

---

## 4. Disney Sound Source

**Identity.** Disney Software's parallel port DAC (1990), distinct from Covox in two ways: it has a 16 byte FIFO buffer, and its status bits are readable through the printer status register.

**Detection.**

1. Read LPT base.
2. Reset the device by writing `04h` to the control port (LPT base + 2).
3. Send 16 bytes to the data port with the strobe bit toggled in the control port.
4. After the 16th byte, read the status port (LPT base + 1). Bit 6 should report FIFO full.
5. After 100 ms, bit 6 should clear (FIFO drained at 7 kHz).

**Capabilities.**
- 8 bit unsigned mono PCM.
- 7 kHz fixed sample rate.

**Boot line.**

```
          Disney Sound Source on LPT1                 detected
```

**Unlocks.** `UL_DISNEY`.

**Pitfalls.** A bidirectional EPP/ECP port can also drive bit 6 of the status register. Cross check by toggling strobe rapidly and observing the FIFO bit.

---

## 5. AdLib (Yamaha YM3812 / OPL2)

**Identity.** The original Ad Lib Music Synthesizer Card, 1987. Yamaha YM3812 (OPL2): 9 channels of 2 operator FM synthesis.

**Detection.** Classic OPL timer test:

1. Reset both timers: write 60h to register 04h, then 80h to register 04h.
2. Read status (port 388h). It should read 00h.
3. Set Timer 1 to a known value: write FFh to register 02h, then 21h to register 04h (start Timer 1, mask Timer 2).
4. Wait at least 80 microseconds.
5. Read status again. Bits 6 and 7 should be set, with bit 5 clear (no Timer 2 overflow).
6. Reset timers.

If steps 5 yields the expected pattern, an OPL chip is present. To distinguish OPL2 vs OPL3, see next section.

**Port.** `388h` (status read, register write), `389h` (data write).

**Capabilities.**
- 9 channels FM, 2 operators each (or 6+5 percussion mode).

**Boot line.**

```
          AdLib (Yamaha YM3812 OPL2)                  detected
            9 channel FM synthesis                    ENABLED
```

**Unlocks.** `UL_OPL2_MIDI`.

**Pitfalls.** Many SB family cards include an OPL2 or OPL3. The probe says "an OPL is present", not "an AdLib card is present". Final disambiguation comes from the SB DSP probe.

---

## 6. AdLib Gold (Yamaha YMF262 OPL3 + 12 bit DAC)

**Identity.** Ad Lib Gold 1000 (1992), the unsuccessful follow up. OPL3 plus a Yamaha YMZ263 12 bit stereo DAC, optional surround module.

**Detection.**

1. Run the AdLib timer test.
2. Probe extended register set: write `04h` to register `05h` of the Yamaha at base+02h, then read back. OPL3 mirrors are at base+02h/03h.
3. AdLib Gold specific: probe the CT1703 control surface at base+8h..base+Fh. The AdLib Gold mixer occupies that range. A successful read of the mixer ID register identifies the Gold.

**Ports.** `388h..38Bh` for OPL3, `38Ch..38Fh` for AdLib Gold mixer (when present).

**Capabilities.**
- OPL3: 18 channels FM, 2 operators (or 6 channel 4-op + percussion).
- 12 bit stereo PCM at up to 44.1 kHz.
- Optional surround daughterboard.

**Boot line.**

```
          AdLib Gold (YMF262 + YMZ263 DAC)            detected
            18 channel FM + 12 bit stereo             ENABLED
            Surround module                            ENABLED
```

**Unlocks.** `UL_OPL3_MIDI`, `UL_AGOLD_DAC`, `UL_AGOLD_SURROUND` if surround module probes positive.

**Pitfalls.** Almost no software ever shipped with AdLib Gold support. Detection works but DAC programming requires a custom driver path.

---

## 7. Sound Blaster 1.0 / 1.5 (DSP 1.x)

**Identity.** Creative Labs Sound Blaster, 1989. CT1320, CT1320A. OPL2 + 8 bit mono DSP.

**Detection (full SB family flow).**

1. Parse `BLASTER` env var: `A220 I7 D1 H5 P330 T6`. `A` = base port, `I` = IRQ, `D` = 8 bit DMA, `H` = 16 bit DMA, `P` = MPU base, `T` = card type.
2. If no env var, blind probe bases 220h, 240h, 260h, 280h.
3. DSP reset: write 1 to base+06h, wait 3 microseconds, write 0 to base+06h.
4. Wait for DSP buffer status (bit 7 of base+0Eh) to set, max 100 ms.
5. Read base+0Ah; expect AAh.
6. DSP get version: write E1h to base+0Ch (after waiting for busy clear), read two bytes from base+0Ah: major, minor.
7. Major=1 -> SB 1.x, Major=2 -> SB 2.0, Major=3 -> SB Pro family, Major=4 -> SB16 family.

**Capabilities (1.x).**
- 8 bit mono PCM up to 23 kHz playback, 12 kHz record.
- OPL2 FM.

**Boot line.**

```
          Sound Blaster 1.5 (DSP 1.05)                 detected
            at 220h, IRQ 7, DMA 1
            8 bit mono PCM                             ENABLED
```

**Unlocks.** `UL_SB_PCM`, `UL_OPL2_MIDI`.

---

## 8. Sound Blaster 2.0 (DSP 2.x)

**Identity.** Creative Labs CT1350. Same hardware feature set as 1.5 plus auto initialise DMA and high speed DMA modes.

**Detection.** As above, DSP version major=2.

**Capabilities.** 8 bit mono, up to 44.1 kHz playback in high speed DMA.

**Boot line.**

```
          Sound Blaster 2.0 (DSP 2.01)                 detected
            at 220h, IRQ 5, DMA 1
            8 bit mono, auto init DMA                  ENABLED
```

**Unlocks.** `UL_SB_PCM`, `UL_SB_AUTOINIT`, `UL_OPL2_MIDI`.

---

## 9. Sound Blaster Pro / Pro 2

**Identity.** SB Pro (CT1330, 1991) had two YM3812 (OPL2 stereo). SB Pro 2 (CT1600, 1992) replaced them with a single YMF262 (OPL3). Both add stereo PCM and a CT1345 mixer.

**Detection.**

- DSP major = 3.
- OPL3 detect: probe register 05h on the secondary OPL bank at base+02h/03h. If OPL3 mirror is responsive, this is Pro 2; otherwise Pro 1.
- Mixer at base+04h/05h.

**Capabilities.**
- 8 bit stereo PCM up to 22.05 kHz.
- OPL2 stereo (Pro) or OPL3 (Pro 2).
- Hardware mixer.

**Boot line.**

```
          Sound Blaster Pro 2 (DSP 3.02, OPL3)         detected
            at 220h, IRQ 5, DMA 1
            8 bit stereo PCM                           ENABLED
            18 channel FM (OPL3)                       ENABLED
```

**Unlocks.** `UL_SB_PCM`, `UL_SB_STEREO`, `UL_OPL2_MIDI` or `UL_OPL3_MIDI`.

---

## 10. Sound Blaster 16 (DSP 4.x)

**Identity.** Creative Labs CT1740/CT1750 (1992). 16 bit PCM, OPL3, CT1745 mixer, optional ASP/CSP, optional Wave Blaster header.

**Detection.**

- DSP major = 4.
- 16 bit DMA on `H` channel from BLASTER.
- Mixer base+04h/05h, with extended register set.

**Capabilities.**
- 16 bit stereo PCM up to 44.1 kHz.
- OPL3 FM.
- Hardware mixer with bass/treble.

**Boot line.**

```
          Sound Blaster 16 (DSP 4.05)                  detected
            at 220h, IRQ 5, DMA 1+5
            16 bit stereo PCM                          ENABLED
```

**Unlocks.** `UL_SB16_PCM`, `UL_SB_STEREO`, `UL_OPL3_MIDI`, `UL_FULL_DUPLEX` (DSP 4.13+).

---

## 11. Sound Blaster 16 ASP / CSP

**Identity.** SB16 with the optional CT1748 Advanced Signal Processor (or rebranded CSP). Allows hardware decode of compressed streams and effects like QSound.

**Detection.**

- After SB16 detect, query DSP for ASP version: write 04h to mixer index 83h, read mixer data.
- Or DSP command 0Fh / 0Dh sequence to enter ASP mode and read its ID.

**Capabilities.** SB16 + on chip CSP for QSound or 4:1 ADPCM.

**Boot line.**

```
          Sound Blaster 16 ASP (DSP 4.05, CT1748 ASP)  detected
            QSound 3D positional audio                 ENABLED
```

**Unlocks.** `UL_SB16_PCM`, `UL_SB16_ASP`, `UL_QSOUND`, `UL_OPL3_MIDI`.

---

## 12. Sound Blaster AWE32 (CT2760 / CT3900)

**Identity.** SB16 + EMU8000 wavetable. 1994. 32 voice GM/GS via 1MB ROM, additional voices via 512KB optional RAM.

**Detection.**

- SB16 base detect succeeds.
- EMU8000 probe at base+620h..base+623h. Issue the EMU8000 init sequence: write to ports 620h, 622h, 624h with known values, read back.
- Read EMU8000 version register; non zero confirms.
- ROM identifies as 1MB by reading the patch table.

**Capabilities.**
- SB16 PCM and FM.
- 32 voice EMU8000 wavetable.
- SoundFont 1.0 / 2.0 loadable.
- Up to 28MB SIMM RAM (CT3900 SIMM slots).

**Boot line.**

```
          Sound Blaster AWE32 (DSP 4.13, EMU8000)     detected
            32 voice wavetable                        ENABLED
            8MB SIMM RAM                              ENABLED
            SoundFont loading                         ENABLED
```

**Unlocks.** All SB16 unlocks plus `UL_AWE_SFONT`, `UL_AWE_DSP`, `UL_AWE_32VOICE`, `UL_AWE_RAM` (when SIMMs present).

**Pitfalls.** AWE32 ROM is 1MB. AWE64 ROM is the same plus 4MB compressed. The presence of EMU8000 + WaveSynth WaveGuide on AWE64 is the differentiator.

---

## 13. Sound Blaster AWE64 (CT4520 / CT4540 / CT4620)

**Identity.** 1996. EMU8000 + EMU8011 (WaveSynth WaveGuide PCM). CQM synthesis instead of OPL3 for FM.

**Detection.**

- SB16 base detect succeeds.
- EMU8000 detect succeeds.
- OPL timer test should fail (no real OPL3). Presence of CQM software emulator.
- DSP version 4.16.

**Capabilities.**
- SB16 PCM (16 bit stereo).
- EMU8000 32 voice + WaveSynth WaveGuide additional voices via host CPU.
- CQM FM (software OPL3 emulation).

**Boot line.**

```
          Sound Blaster AWE64 (DSP 4.16, EMU8000+CQM) detected
            32 voice wavetable + 32 WaveSynth         ENABLED
            CQM FM synthesis                          ENABLED
```

**Unlocks.** `UL_AWE_SFONT`, `UL_AWE_32VOICE`, `UL_CQM_FM`.

---

## 14. Gravis UltraSound Classic

**Identity.** Advanced Gravis UltraSound (1992). GF1 chip, 256 KB to 1 MB DRAM, no ADC.

**Detection.**

1. Parse `ULTRASND` env var: `220,5,5,11,7` -> base, DMA1, DMA2, IRQ1, IRQ2. Bases 210h/220h/230h/240h/250h/260h/270h.
2. GF1 reset: write 4Ch (reset register) at base+103h with bit 0 cleared, wait, set bits 0 and 1 (DAC enable + master reset off).
3. DRAM size detect: write `0x55, 0xAA, 0x33, 0xCC` to 256KB boundaries (0, 256K, 512K, 768K). Read back. Wraparound indicates the actual installed size.
4. Daughterboard detect: probe the General MIDI daughterboard slot (different chip select).

**Ports.** `2X0h..2XFh` mostly, with `3X3h..3X7h` for GF1 voice control.

**Capabilities.**
- 32 voice hardware mixing, no CPU cost.
- 14 bit DAC, 8/16 bit samples.
- 192 kHz divide tree.
- 256KB to 1MB DRAM.

**Boot line.**

```
 Audio    Gravis UltraSound (GF1 v3.7)                 detected
            at 240h, IRQ 11, DMA 1, 1024KB DRAM
            32 voice hardware mixing                  ENABLED
```

**Unlocks.** `UL_GUS_HW_MIX`, `UL_GUS_32VOICE`, `UL_GUS_DRAM`, `UL_DAUGHTER` (if daughterboard).

---

## 15. Gravis UltraSound MAX

**Identity.** GUS Classic + Crystal Semiconductor CS4231 codec. Adds 16 bit input, line in, mic in.

**Detection.**

- GUS Classic detect succeeds.
- Probe CS4231 codec at base+10Ch..base+10Fh.
- Codec ID register reads as 0x80 + revision.

**Capabilities.**
- All GUS Classic capabilities.
- CS4231 16 bit stereo recording, 16 bit playback, line in passthrough.

**Boot line.**

```
 Audio    Gravis UltraSound MAX (GF1, CS4231)         detected
            at 240h, IRQ 11, DMA 1, 1024KB DRAM
            32 voice hardware mixing                  ENABLED
            16 bit line in / passthrough              ENABLED
```

**Unlocks.** All GUS unlocks plus `UL_GUS_MAX_LINEIN`, `UL_GUS_MAX_CODEC`.

---

## 16. Gravis UltraSound ACE

**Identity.** Audio Card Enhancer, 1995. GUS Classic without DMA, smaller (256KB DRAM only). Designed to coexist with another sound card for DMA duties.

**Detection.**

- GUS detect succeeds but with no IRQ2 in `ULTRASND`.
- DRAM detect caps at 256KB.

**Boot line.**

```
 Audio    Gravis UltraSound ACE (GF1, 256KB)          detected
            32 voice mixing (DMA via host card)       ENABLED
```

**Unlocks.** `UL_GUS_HW_MIX`, `UL_GUS_32VOICE`. Not `UL_GUS_DRAM` (insufficient).

---

## 17. Gravis UltraSound PnP / InterWave

**Identity.** AMD InterWave AM78C201 chip. 1996. Plug and Play, 32 voice + GM ROM, optional CS4231.

**Detection.** ISA Plug and Play, EISA ID `GRV` series. Fall back to GUS Classic detect for legacy mode.

**Boot line.**

```
 Audio    Gravis UltraSound PnP (InterWave)            detected
            at 220h, IRQ 5, DMA 1, 4096KB DRAM
            32 voice + GM ROM                         ENABLED
```

**Unlocks.** All GUS unlocks plus optional GUS MAX class extras.

---

## 18. MPU-401 (UART or intelligent mode)

**Identity.** Roland MPU-401, 1984. The de facto MIDI port standard. Most cards implement a UART subset.

**Detection.**

1. From BLASTER `Pxxx` if present, else probe 330h, 300h, 320h, 340h.
2. Reset: write FFh to status port (base+1).
3. ACK: status port should report FEh (DRR clear, command ACK) within ~10 ms.
4. Enter UART mode: write 3Fh to status port. Expect FEh ACK.
5. UART mode confirmed; the MPU is now ready to forward MIDI.

**Capabilities.** 31250 baud MIDI in/out.

**Boot line.**

```
          Roland MPU-401 UART at 330h                 detected
```

**Unlocks.** `UL_MPU401`.

---

## 19. MIDI Synth Identity (over MPU)

After MPU detection, send a SysEx Identity Request:

```
F0 7E 7F 06 01 F7
```

Wait up to 200 ms for a 14 byte response. Parse manufacturer ID at byte 5 and model ID at byte 6:

| Manufacturer | Model | Synth |
|--------------|-------|-------|
| 41h Roland | 16h | MT-32 |
| 41h Roland | 45h | SC-55 |
| 41h Roland | 47h | SC-55mkII |
| 41h Roland | 49h | SC-88 |
| 41h Roland | 4Ah | SC-88 Pro |
| 43h Yamaha | 4Ch | DB50XG / MU series |
| 41h Roland | 5Bh | SCB-55 daughterboard |

Devices that do not respond: assume General MIDI capable, list as "GM device".

**Boot line examples.**

```
          Roland MT-32 (LAPC-I)                       detected
            LA synthesis, 32 partials                 ENABLED
```

```
          Roland SC-55 SoundCanvas                    detected
            GS, GM, drum maps                         ENABLED
```

```
          Yamaha DB50XG daughterboard                 detected
            XG, 676 voices                            ENABLED
```

**Unlocks.** `UL_MT32_MODE`, `UL_SC55_MODE`, `UL_XG_MODE`, `UL_DAUGHTER` as appropriate.

---

## 20. Ensoniq SoundScape

**Identity.** Ensoniq SoundScape (1994), GM wavetable using OTTO ES5506 chip + onboard 68000. 32 voices, ROM samples.

**Detection.**

1. Probe base 330h, 220h, 240h, 320h, 340h for the OTTO chip ID register.
2. Read 68000 status port; cards return a known boot signature.

**Boot line.**

```
          Ensoniq SoundScape (OTTO ES5506)            detected
            32 voice wavetable                        ENABLED
            On board 68000 sequencer                  ENABLED
```

**Unlocks.** `UL_ENSONIQ_WT`, `UL_ENSONIQ_DAC`, `UL_ENSONIQ_FILTER`.

---

## 21. Pro Audio Spectrum 16 (Media Vision)

**Identity.** PAS-16 (1991), MV101 chip. SB compatible, plus 16 bit PCM, OPL3 stereo.

**Detection.**

1. PAS mixer signature read: write 80h to PAS index, read PAS data; expected magic 50h ("P").
2. SB compatibility mode lives at the BLASTER address.

**Boot line.**

```
          Media Vision Pro Audio Spectrum 16          detected
            16 bit stereo PCM                         ENABLED
            OPL3 + Sound Blaster compatibility         ENABLED
```

**Unlocks.** `UL_PAS16`, `UL_OPL3_MIDI`, `UL_SB16_PCM` (compat).

---

## 22. ESS AudioDrive (ES688 / ES1688 / ES1868)

**Identity.** ESS Technology AudioDrive series, 1992 onwards. SB Pro / SB16 compatible plus native 6 bit ADPCM and ESFM synthesis.

**Detection.**

1. SB DSP reset succeeds at 220h (or BLASTER base).
2. DSP command E7h returns ESS chip ID. Major byte distinguishes ES688 (68h), ES1688 (68h or 69h), ES1868 (68h with extended), etc.

**Boot line.**

```
          ESS AudioDrive ES1868 (Sound Blaster Pro)   detected
            16 bit stereo PCM                         ENABLED
            ESFM synthesis                            ENABLED
```

**Unlocks.** `UL_ESS_NATIVE`, `UL_OPL3_MIDI`, plus inherited SB unlocks.

---

## 23. Turtle Beach MultiSound

**Identity.** Turtle Beach MultiSound Classic (1992). Motorola 56001 DSP, ICS2115 wavetable. High end audio.

**Detection.** Probe DSP host port at one of 250h, 260h, 290h, 320h. Issue MultiSound boot ROM read; the 56001 returns its banner.

**Boot line.**

```
          Turtle Beach MultiSound Classic              detected
            56001 DSP, 24 bit accumulator             ENABLED
            ICS2115 wavetable                         ENABLED
```

**Unlocks.** `UL_TBEACH`, `UL_DAUGHTER` (if daughter card present).

---

## 24. Tandy SX/TX, Hercules, IBM Speech Adapter (notes)

These are listed for completeness; HEARO does not target them in v1.0.

- IBM Speech Adapter: 9 bit DAC on a card. Listed in unlocks roadmap.
- Hercules InColor / GB: video, not audio.
- Disney Mickey Mouse cartridges: see Disney Sound Source.

---

## Capability Summary Table

| Device | PCM | FM | Wavetable | Hardware mix | Recording |
|--------|-----|-----|-----------|--------------|-----------|
| PC Speaker | 1 bit / RealSound | none | none | no | no |
| Tandy PSG | none | none | none (square waves) | no | no |
| Covox | 8 bit mono | none | none | no | no |
| Disney | 8 bit mono | none | none | FIFO 16 byte | no |
| AdLib | none | OPL2 | none | no | no |
| AdLib Gold | 12 bit stereo | OPL3 | none | no | no |
| SB 1.x | 8 bit mono | OPL2 | none | no | 12 kHz |
| SB 2.0 | 8 bit mono | OPL2 | none | auto init | 15 kHz |
| SB Pro | 8 bit stereo | OPL2 stereo | none | mixer | yes |
| SB Pro 2 | 8 bit stereo | OPL3 | none | mixer | yes |
| SB16 | 16 bit stereo | OPL3 | none | mixer | 16 bit |
| SB16 ASP | 16 bit stereo | OPL3 | none | mixer + ASP | 16 bit |
| AWE32 | 16 bit stereo | OPL3 | EMU8000 | EMU8000 | 16 bit |
| AWE64 | 16 bit stereo | CQM | EMU8000 + WG | EMU8000 | 16 bit |
| GUS Classic | 8/16 bit | none | GF1 | 32 voice | no |
| GUS MAX | 8/16 bit | none | GF1 | 32 voice | 16 bit |
| GUS ACE | 8/16 bit | none | GF1 | 32 voice | no |
| GUS PnP | 8/16 bit | none | InterWave | 32 voice | optional codec |
| MPU-401 | none | none | external | external | no |
| Ensoniq | 16 bit | none | OTTO | 32 voice | no |
| PAS-16 | 16 bit stereo | OPL3 stereo | none | mixer | 16 bit |
| ESS | 16 bit stereo | ESFM | none | mixer | 16 bit |
| TBeach | 16 bit | none | ICS2115 | 56001 | yes |

---

## Boot screen format

Each card's marketing name on one line. Configuration details (port, IRQ, DMA, RAM) on the indented line beneath. ENABLED entries right-justified in the status column. PC Speaker is listed in the same column as GUS MAX.
