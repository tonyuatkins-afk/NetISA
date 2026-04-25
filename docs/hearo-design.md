# HEARO Design Document

**Version:** 1.0 (Implementation phase)
**Status:** Foundation in progress
**Target Platform:** IBM PC/AT and compatibles, 80286 minimum, DOS 3.3+
**Companion Project:** NetISA (network coprocessor card)
**Companion Document:** `hearo-soundcard-reference.md` (audio detection details)

## 1. Summary

HEARO is a music player for vintage IBM PC compatibles. Native formats (MIDI, WAV, MOD/S3M/XM/IT, chiptunes) decode on the host CPU. Compressed formats (MP3, FLAC, Opus, Vorbis, AAC) and streaming run through the ESP32-S3 on the NetISA card, which delivers PCM back to the host.

Minimum config: 80286 with 1 MB RAM, MDA video, PC Speaker. Maximum is whatever DOS can address.

The core idea: every detected expansion turns on specific features the user can see. The boot screen, the settings panel, and the Hall of Recognition each show what HEARO has noticed about the machine.

## 2. Design rules

- **Visible capability.** When hardware unlocks a feature, it shows up in the UI. ENABLED on the boot screen. Locked features in settings show their requirement. Hall records first-detection date.
- **Format honesty.** Native formats decode on the host. Compressed formats decode on the ESP32. No host MP3 at 1 fps with the word "support".
- **Graceful degradation.** Each video tier from MDA text to 1024x768x256 SVGA gets its own UI rather than a scaled-down VGA layout.
- **No surprise output.** Last selected output device is honoured at boot. Auto-switching only with explicit consent.
- **No achievements.** The Hall is a record, not a scoreboard.

## 3. Run loop

- **First run.** Boot screen names every detected component. Last line shows feature count. Settings panel shows every feature with its lock state.
- **Tenth run.** Defaults adapted from prior use. Recently-browsed directories surface higher.
- **Hundredth run.** You added an FPU. Boot screen leads with "New since last boot: IIT 2C87-12 detected. 6 features unlocked." Hall accumulates.
- **One year.** Boot screen says "One year ago today, you ran HEARO on this machine for the first time."
- **Anniversary.** On the same calendar day in later years, the boot screen acknowledges the original detection date.

## 4. Suite context

HEARO is one of nine NetISA suite applications:

| App | Role |
|-----|------|
| CATHODE | Web browser |
| DISCORD | Chat client |
| CLAUDE | LLM client |
| COURIER | Email (Gmail/Outlook OAuth2) |
| CRATE | Cloud files as DOS drive |
| KIOSK | Package manager |
| CHIME | NTP/NTS time sync |
| HEARO | Music player |
| RADIO | Lightweight streaming client (XT-class) |

Shared conventions:

- INI config (`HEARO.CFG`) using the same section grammar as `NETISA.CFG`.
- Hall of Recognition file format (`*.HAL`); the suite tracks one machine fingerprint across apps.
- Boot screen layout: bold logo, hardware report, ENABLED right-justified, last line is the unlock count.
- Cmdline conventions: `/SAFE` skips probes, `/REDETECT` forces a full scan, `/BENCHMARK` runs the suite benchmark, `/STUBNET` provides a fake card for development.

## 5. Unlock system

Each rule is one row in a table: name, description, requirement string, pure-function check against `hw_profile_t`. Adding a feature is one line.

### 5.1 FPU (287/387/487/integrated)

- 256-bin FFT spectrum analyzer at 60 fps (`UL_FFT_256`)
- Sinc resampling for streaming and playback (`UL_SINC_RESAMPLE`)
- 16+ channel tracker mixing with ramping (`UL_16CH_MIX`)
- Plasma, tunnel, particle, fire, wireframe visualizers (`UL_PLASMA`, `UL_TUNNEL`, `UL_PARTICLE`, `UL_FIRE`, `UL_WIREFRAME`)
- Karaoke vocal removal (`UL_KARAOKE`)
- 10-band parametric EQ (`UL_PARAM_EQ`)
- Convolution reverb, Pentium-class (`UL_CONV_REVERB`)
- Stereo widening (`UL_STEREO_WIDE`)
- Gamma-correct album-art dithering (`UL_GAMMA_DITHER`)
- Software quire, 256-bit accumulator, Posit Standard 2022 technique (`UL_EXACT_MIX`)
- Adaptive-precision CORDIC visualizers (`UL_ADAPTIVE_CORDIC`)
- Log-domain effects, Takum 2024 technique (`UL_LOG_EFFECTS`)
- FPU brand recognition: Intel, IIT 2C87, Cyrix FasMath, ULSI, AMD

### 5.2 Memory

- 4 MB+ XMS: full library indexing for 100K+ tracks (`UL_FULL_LIBRARY`)
- 8 MB+ XMS: all visualizers preloaded (`UL_VIS_PRELOAD`)

### 5.3 Sound cards

| Card | Unlocks |
|------|---------|
| PC Speaker | RealSound PWM (`UL_REALSOUND`) |
| Tandy PSG | 3-voice + noise (`UL_TANDY_PSG`) |
| Covox | 8-bit DAC (`UL_COVOX`) |
| Disney Sound Source | 7 kHz fixed DAC (`UL_DISNEY`) |
| AdLib | OPL2 9-channel FM MIDI (`UL_OPL2_MIDI`) |
| AdLib Gold | OPL3 + 12-bit DAC + surround (`UL_OPL3_MIDI`, `UL_AGOLD_DAC`, `UL_AGOLD_SURROUND`) |
| SB 1.x/2.0 | 8-bit mono PCM, OPL2 (`UL_SB_PCM`, `UL_SB_AUTOINIT`) |
| SB Pro/Pro 2 | 8-bit stereo PCM, OPL2 stereo or OPL3 (`UL_SB_STEREO`) |
| SB16 | 16-bit stereo PCM, OPL3 (`UL_SB16_PCM`, `UL_FULL_DUPLEX`) |
| SB16 ASP | QSound, hardware ADPCM (`UL_SB16_ASP`, `UL_QSOUND`) |
| AWE32 | 32-voice EMU8000, SoundFont (`UL_AWE_SFONT`, `UL_AWE_DSP`, `UL_AWE_32VOICE`, `UL_AWE_RAM`) |
| AWE64 | EMU8000 + WaveSynth + CQM (`UL_AWE_SFONT`, `UL_CQM_FM`) |
| GUS Classic | 32-voice hardware mixing, DRAM tier (`UL_GUS_HW_MIX`, `UL_GUS_32VOICE`, `UL_GUS_DRAM`) |
| GUS MAX | + line-in, 16-bit codec (`UL_GUS_MAX_LINEIN`, `UL_GUS_MAX_CODEC`) |
| GUS ACE | 32-voice (no DMA) (`UL_GUS_HW_MIX`, `UL_GUS_32VOICE`) |
| GUS PnP | InterWave + GM ROM (all GUS unlocks) |
| Ensoniq SoundScape | Wavetable + filters (`UL_ENSONIQ_WT`, `UL_ENSONIQ_DAC`, `UL_ENSONIQ_FILTER`) |
| MPU-401 | External MIDI (`UL_MPU401`, `UL_DAUGHTER`) |
| MT-32 | LA mode (`UL_MT32_MODE`) |
| SC-55 | SoundCanvas mode (`UL_SC55_MODE`) |
| DB50XG | XG mode (`UL_XG_MODE`) |
| PAS-16 | Native 16-bit + OPL3 (`UL_PAS16`) |
| ESS AudioDrive | ESFM + native modes (`UL_ESS_NATIVE`) |
| Turtle Beach MultiSound | 56001 DSP wavetable (`UL_TBEACH`) |

### 5.4 Video

- VESA 2.0 LFB: 1024x768x256 album-art panel (`UL_SVGA_1024`)
- 1 MB+ VRAM: 800x600x256 (`UL_SVGA_800`)
- 512 KB VRAM: 640x480x256 (`UL_SVGA_640`)
- 2 MB+ VRAM: album art at 256 colour (`UL_ART_256`) and high resolution (`UL_ART_HIRES`)
- Chipset-specific accelerated blits (`UL_CHIPSET_ACCEL`)

### 5.5 Network (NetISA path)

- ESP32 streaming via NetISA (`UL_STREAMING`)
- Scrobbling (`UL_SCROBBLE`)
- Cloud library (`UL_CLOUD_LIB`)
- Bluetooth out (`UL_BT_OUT`)
- AirPlay target (`UL_AIRPLAY`)

### 5.6 FPU-less

For machines without an FPU:

- Stochastic particles: probability-based visualizer using bit operations (`UL_STOCHASTIC`).
- 64-bin bipartite-table FFT spectrum (`UL_BIPARTITE_FFT`).

Both are fast enough that a 386SX without an FPU runs them faster than the FPU path runs on a 287.

### 5.7 Boot screen

Each boot, hardware detection runs and the screen shows ENABLED highlights and an unlock count. Last line: "Your machine has unlocked NN features." On the second and later boots, the screen leads with anything new since the previous boot.

### 5.8 Settings panel

Locked features are greyed out with their requirement listed. Category tree. A locked feature looks like:

```
[ ] AWE32 SoundFont loading                           requires AWE32/64
```

Requirement is in dim text.

### 5.9 Hall of Recognition

Persistent timeline of detected hardware with first-seen dates. Plain text, survives reinstalls. Each detection adds an event row. Lifetime tallies update on close.

### 5.10 Dormant-feature whisper

When an unlocked feature has not been used in 60+ days, HEARO can produce a single status-bar-level message:

```
You have an MT-32. There is a Roland synth bank loaded with two unused patches.
```

Rate-limited: one per session, globally. Never blocks, never modal, never animates. Same message never repeats within 90 days.

## 6. System requirements

| Tier | CPU | FPU | RAM | Video | Audio |
|------|-----|-----|-----|-------|-------|
| Minimum | 286/12 | none | 1 MB | MDA | PC Speaker |
| Standard | 386SX/16 | none | 2 MB | EGA | AdLib |
| Enhanced | 386DX/33 | optional | 4 MB | VGA | SB |
| Workstation | 486DX/33 | integrated | 8 MB | SVGA 640 | SB16/GUS |
| Maximum | 486DX2/66+ | integrated | 16 MB | SVGA 1024 | GUS/AWE32 |
| XT-FPU (community) | V20/8 MHz | 8087 | 640 K | CGA/EGA | AdLib/SB |

## 7. Audio devices

PC Speaker, Tandy PSG, Covox, Disney Sound Source, AdLib (OPL2), AdLib Gold (OPL3 + DAC), Sound Blaster 1.x, SB 2.0, SB Pro, SB Pro 2, SB16, SB16 ASP, AWE32, AWE64, GUS Classic, GUS MAX, GUS ACE, GUS PnP / InterWave, Ensoniq SoundScape, Pro Audio Spectrum 16, ESS AudioDrive, Turtle Beach MultiSound, MPU-401 + identity SysEx for MT-32 / CM-32L / SC-55 / SC-55mkII / SC-88 / SC-88 Pro / DB50XG / SCB-55.

Probe protocols: `hearo-soundcard-reference.md`.

## 8. Format support

- **Host:** MIDI (SMF), WAV (PCM 8/16/24), MOD/S3M/XM/IT, SID, AY, NSF, GBS, SPC.
- **ESP32:** MP3, FLAC, Opus, Vorbis, AAC, WMA, ALAC.
- **Streaming:** Icecast/SHOUTcast, Bandcamp, SomaFM, Internet Archive, Nectarine, ModArchive.
- **Output mixing path:** tracker mixing on SB/SB Pro, hardware mix on GUS, MIDI on MPU/AWE/MT-32.

## 9. Video tiers

| Tier | Resolution | UI |
|------|-----------|----|
| Workstation | 1024x768x256 | Four-pane, album-art thumbnails, full visualizer canvas |
| Enhanced | 800x600x256 | Four-pane, tighter thumbnails |
| Standard | 640x480x256 | Three-pane, no thumbnails |
| Classic | 320x200x256 | Two-pane, demoscene aesthetic |
| Legacy | 640x350x16 | EGA, austere palette |
| Minimal | 720x350 text | MDA/Hercules, ASCII spectrum |

Text-mode tier (Minimal) is the v1.0 deliverable. Graphical tiers are scaffolded; real Mode 13h / VESA framebuffer code is v1.3.

## 10. Visualizers

Every visualizer has both a high-quality (FPU) and a low-quality (FPU-less, bipartite/stochastic) implementation. User can choose; defaults follow detected hardware.

- Spectrum analyzer: 16/64/256 bin
- Oscilloscope
- Tracker pattern view (works for all formats, not only trackers)
- VU meters
- Plasma, tunnel, particle, fire, wireframe 3D
- Beat-detection LED panel
- ASCII bars (text mode)

Plugin format: `.HVP` files. Demoscene mode (F11 in graphical UI): visualizer fills the screen, controls hidden until keypress.

## 11. Streaming services

Internet radio (Icecast/SHOUTcast), Bandcamp (purchases + free), SomaFM, Internet Archive (78s, Live Music Archive), Nectarine Demoscene Radio, ModArchive (lookup + stream).

Beyond v1.0: Spotify Connect (target only), YouTube Music (mp3 transcode via ESP32), SoundCloud, podcasts, audiobooks (with chapter resume).

## 12. Library

Library scan with MusicBrainz tagging, Cover Art Archive, lyrics, karaoke mode, smart playlists, ReplayGain. Each track gets a `play_count` and `first_added` date that survives library rebuilds.

## 13. Flags and modes

- `/BENCHMARK` runs a deterministic suite that measures detection speed, decode throughput per format, mixer throughput per channel count, visualizer frame rate. Results appended to `HEARO.STA`.
- `/SAFE` boots without probing audio cards. Only PC Speaker is listed. For diagnosing hangs.
- `/REDETECT` forces a full hardware rescan and replaces the saved profile.
- `/STUBNET` synthesises a present NetISA card.
- `/VERSION`, `/UNLOCKS`, `/HALL` print and exit.

Sleep timer, crossfade, EQ, stereo widening, Discord Rich Presence (when DISCORD app is also running on the same NetISA card).

## 14. Memory model

Real mode: ~540 KB ceiling for `HEARO.EXE`. XMS for sample cache, library index, visualizer tables. EMS supported but not preferred. DPMI for SVGA framebuffer access.

Both `HEARO.EXE` (real mode, 286+) and `HEAROXM.EXE` (32-bit protected mode, 386+) ship. The 32-bit binary is preferred when available; the 16-bit binary is the universal fallback.

## 15. Roadmap

- **v1.0 (current):** Foundation. Native formats, full unlock matrix, boot screen, Hall, text-mode UI.
- **v1.1:** Streaming. ESP32 path, internet radio, Bandcamp, ModArchive.
- **v1.2:** Smart Library. MusicBrainz, scrobbling, cover art, lyrics, ReplayGain, smart playlists.
- **v1.3:** Demoscene polish. Advanced visualizers, skins, karaoke, EQ, graphical tiers (320, 640, 800, 1024).
- **v1.4:** Network audio. Bluetooth out, AirPlay target, Chromecast target, DLNA renderer, multi-room.
- **v1.5:** Services. Spotify Connect target, YouTube Music, podcasts, audiobooks.

## 16. Out of scope

HEARO is not a tracker, DAW, podcast manager, or audio editor. No achievements, streaks, or daily nudges.

## 17. Math library

Six fixed-point arithmetic techniques used to keep visualizers and DSP working across the CPU range from 286 to Pentium without a uniform floating-point assumption.

1. **Adaptive CORDIC.** Iteration count varies with screen size and frame budget. 320x200 plasma at 30 fps on a 386SX = 8 iterations. 1024x768 on a 486DX2 = 24.
2. **Software quire.** 256-bit fixed-point accumulator. Eight 32-bit words, two's-complement, full carry. Posit Standard 2022 quire concept adapted to fixed-point. Used when `UL_EXACT_MIX` is unlocked.
3. **Stochastic particles.** Probability bits + counters. No multiplications.
4. **Bipartite tables.** sin/cos/log/exp from two table lookups + add. ~99% of full precision in 0.3% of the time. ~4 KB of tables.
5. **Log-domain effects.** Compressor/limiter/EQ in log domain (Takum 2024). Avoids many multiplies.
6. **8087 silicon fingerprint.** `FPATAN` and `FYL2X` bit patterns differ across IIT, Cyrix, ULSI, AMD, Intel parts. Used to identify FPU brand without CPUID.

## 18. Run-time flow

A typical run:

1. `cmdline_parse` then `config_load("HEARO.CFG")`.
2. `detect_all` populates the master `hw_profile_t`.
3. `unlock_evaluate` walks the rule table and stamps each entry.
4. `hall_load("HEARO.HAL")`, then diff against current detection, append new events.
5. `boot_screen_render` displays the hardware report.
6. `ui_run` enters the four-pane UI loop until quit.
7. `hall_save` and `config_save` write back state.

The expensive work is detection and the UI loop; everything else is config and persistence.
