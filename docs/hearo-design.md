# HEARO Design Document

**Version:** 1.0 (Implementation phase)
**Status:** Foundation in progress; recognition stack and UI scaffolding implemented
**Target Platform:** IBM PC/AT and compatibles, 80286 minimum, DOS 3.3+
**Companion Project:** NetISA (network coprocessor card)
**Companion Document:** `hearo-soundcard-reference.md` (audio detection details)

## 1. Executive Summary

HEARO is a music player for vintage IBM PC compatibles, built around a single design idea: every feature should make the user feel more connected to their specific machine, not less. The audio playback, the streaming, the visualisers, the library, all of it serves that idea.

The technical foundation is a dual path decoder architecture. Native formats (MIDI, WAV, MOD/S3M/XM/IT, chiptunes) are decoded on the host CPU. Compressed formats (MP3, FLAC, Opus, Vorbis, AAC) and streaming protocols are offloaded to the ESP32-S3 on the NetISA card, which delivers raw PCM back to the host.

The minimum supported configuration is an 80286 with 1MB RAM, MDA video, and a PC speaker. The maximum is bounded only by what DOS can address. Between those bounds, every recognized expansion unlocks specific features the user can see.

## 2. Vision: What HEARO Is For

### 2.1 The North Star

HEARO exists to make a vintage PC feel seen. The user's specific machine, with its specific accumulated history of upgrades and choices, is the protagonist. The software's job is to notice everything about it and respond accordingly.

### 2.2 Design Principles

**Recognition density.** Every interaction should give the user new information about how HEARO sees their specific hardware. This is the single most important principle. Every detected card, FPU, video chip, memory expansion, and network adapter shows up in at least three places: the boot screen, the settings panel, and the Hall of Recognition.

**Format honesty.** Native formats decode on the host. Compressed formats decode on the ESP32. We do not ship an MP3 decoder that "technically runs on a 286" at 1fps and call it support. If the host cannot do it well, the ESP32 does it; if the ESP32 is not present, the format is greyed out in the file browser.

**Visible capability.** When hardware unlocks a feature, the unlock is visible. ENABLED lines on the boot screen. Locked features in settings show requirements. The Hall of Recognition records first detection date.

**Graceful degradation.** Every tier from MDA text to 1024x768x256 SVGA gets a real, deliberately designed UI. No tier is a scaled down version of another. The MDA UI is a good MDA UI, not a bad VGA UI.

**Cultural correctness.** PC Speaker output is a feature, not a fallback. GUS support is mandatory. Tracker pattern view appears for non tracker formats too.

**No surprise output.** The user's last selected output device is honoured at boot. Auto switching only happens with explicit consent.

**Anti gamification.** Recognition, not achievements. No streaks. No "you have not played in 7 days" emails.

## 3. The Loop: Why You Open HEARO

### First Run
The boot screen names your machine. Each detected component shows DETECTED or ENABLED. The last line: "Your machine has unlocked 19 features. Press any key to begin." The settings panel shows every feature HEARO supports, with locks beside the ones your hardware does not enable.

### Tenth Run
Defaults have adapted. The audio device picker reordered silently. Directories you browse frequently surface higher. The Hall of Recognition has accumulated entries.

### Hundredth Run
You installed an FPU from eBay. The boot screen led with: "New since last boot: IIT 2C87-12 detected. 6 features unlocked." The Hall of Recognition reads like a timeline of your retro computing journey.

### One Year
Boot screen: "One year ago today, you ran HEARO on this machine for the first time. 1,247 hours played." No streaks, no nudges, just a quiet acknowledgement.

### Anniversary of Your Machine
On the anniversary of the first detection of your fingerprint, the boot screen shows the original detection record beside the current one. If you upgraded the FPU two years ago, the line reads: "Two years ago this week you added the IIT 2C87-12. Welcome back."

## 4. Suite Context

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

HEARO shares conventions with the rest of the suite:

- INI style config (`HEARO.CFG`) with the same section grammar.
- The Hall of Recognition file format (`*.HAL`) is shared; the suite as a whole tracks one machine fingerprint across all apps.
- Boot screen visual language: bold logo, hardware report, ENABLED right justified, last line is the unlock count.
- Command line conventions: `/SAFE` skips probes, `/REDETECT` forces a full scan, `/BENCHMARK` runs the suite benchmark, `/STUBNET` provides a fake NetISA card for development.

## 5. The Unlock System

The structural heart of HEARO. Every recognized expansion in the user's machine unlocks specific features. Unlocks are data driven: each one is a row in a table with a name, description, requirement string, and pure function check against the `hw_profile_t`. Adding a new unlock is a one line change to the table.

### 5.1 FPU Unlocks (287/387/487/Integrated)

- 256 bin FFT spectrum analyser at 60fps (`UL_FFT_256`)
- Sinc resampling for streaming and playback (`UL_SINC_RESAMPLE`)
- 16+ channel tracker mixing with ramping (`UL_16CH_MIX`)
- Plasma, tunnel, particle, fire, wireframe visualisers (`UL_PLASMA`, `UL_TUNNEL`, `UL_PARTICLE`, `UL_FIRE`, `UL_WIREFRAME`)
- Karaoke vocal removal (`UL_KARAOKE`)
- Parametric 10 band EQ (`UL_PARAM_EQ`)
- Convolution reverb (Pentium class) (`UL_CONV_REVERB`)
- Stereo widening (`UL_STEREO_WIDE`)
- Gamma correct album art dithering (`UL_GAMMA_DITHER`)
- Software quire (256 bit accumulator, Posit Standard 2022 technique) (`UL_EXACT_MIX`)
- Adaptive precision CORDIC visualisers (`UL_ADAPTIVE_CORDIC`)
- Log domain effects chain (Takum 2024 technique) (`UL_LOG_EFFECTS`)
- FPU brand recognition: Intel, IIT 2C87, Cyrix FasMath, ULSI, AMD

### 5.2 Memory Unlocks

- 4MB+ XMS: full library indexing for 100K+ tracks (`UL_FULL_LIBRARY`)
- 8MB+ XMS: all visualisers preloaded (`UL_VIS_PRELOAD`)

### 5.3 Sound Card Unlocks

| Card | Unlocks |
|------|---------|
| PC Speaker | RealSound PWM (`UL_REALSOUND`) |
| Tandy PSG | 3 voice + noise (`UL_TANDY_PSG`) |
| Covox | 8 bit DAC (`UL_COVOX`) |
| Disney Sound Source | 7 kHz fixed DAC (`UL_DISNEY`) |
| AdLib | OPL2 9 channel FM MIDI (`UL_OPL2_MIDI`) |
| AdLib Gold | OPL3 + 12 bit DAC + surround (`UL_OPL3_MIDI`, `UL_AGOLD_DAC`, `UL_AGOLD_SURROUND`) |
| SB 1.x/2.0 | 8 bit mono PCM, OPL2 (`UL_SB_PCM`, `UL_SB_AUTOINIT`) |
| SB Pro/Pro 2 | 8 bit stereo PCM, OPL2 stereo or OPL3 (`UL_SB_STEREO`) |
| SB16 | 16 bit stereo PCM, OPL3 (`UL_SB16_PCM`, `UL_FULL_DUPLEX`) |
| SB16 ASP | QSound, hardware ADPCM (`UL_SB16_ASP`, `UL_QSOUND`) |
| AWE32 | 32 voice EMU8000, SoundFont (`UL_AWE_SFONT`, `UL_AWE_DSP`, `UL_AWE_32VOICE`, `UL_AWE_RAM`) |
| AWE64 | EMU8000 + WaveSynth + CQM (`UL_AWE_SFONT`, `UL_CQM_FM`) |
| GUS Classic | 32 voice hardware mixing, DRAM tier (`UL_GUS_HW_MIX`, `UL_GUS_32VOICE`, `UL_GUS_DRAM`) |
| GUS MAX | + line in, 16 bit codec (`UL_GUS_MAX_LINEIN`, `UL_GUS_MAX_CODEC`) |
| GUS ACE | 32 voice (no DMA) (`UL_GUS_HW_MIX`, `UL_GUS_32VOICE`) |
| GUS PnP | InterWave + GM ROM (all GUS unlocks) |
| Ensoniq SoundScape | Wavetable + filters (`UL_ENSONIQ_WT`, `UL_ENSONIQ_DAC`, `UL_ENSONIQ_FILTER`) |
| MPU-401 | External MIDI (`UL_MPU401`, `UL_DAUGHTER`) |
| MT-32 | LA mode (`UL_MT32_MODE`) |
| SC-55 | SoundCanvas mode (`UL_SC55_MODE`) |
| DB50XG | XG mode (`UL_XG_MODE`) |
| PAS-16 | Native 16 bit + OPL3 (`UL_PAS16`) |
| ESS AudioDrive | ESFM + native modes (`UL_ESS_NATIVE`) |
| Turtle Beach MultiSound | 56001 DSP wavetable (`UL_TBEACH`) |

### 5.4 Video Unlocks

- VESA 2.0 LFB: 1024x768x256 album art panel (`UL_SVGA_1024`)
- 1MB+ VRAM: 800x600x256 (`UL_SVGA_800`)
- 512KB VRAM: 640x480x256 (`UL_SVGA_640`)
- 2MB+ VRAM: album art at 256 colour (`UL_ART_256`) and high resolution (`UL_ART_HIRES`)
- Chipset specific accelerated blits (`UL_CHIPSET_ACCEL`)

### 5.5 Network Unlocks (NetISA path)

- ESP32 streaming via NetISA (`UL_STREAMING`)
- Scrobbling (`UL_SCROBBLE`)
- Cloud library (`UL_CLOUD_LIB`)
- Bluetooth out (`UL_BT_OUT`)
- AirPlay target (`UL_AIRPLAY`)

### 5.6 FPU less Exclusives

Some features are deliberately for the FPU less population:

- Stochastic particles: probability based visualiser that uses bit operations to approximate physics (`UL_STOCHASTIC`).
- 64 bin bipartite FFT spectrum (`UL_BIPARTITE_FFT`).

These are not consolation prizes. They run faster on a 386SX without an FPU than the FPU path runs on a 287. They are first class.

### 5.7 Boot Screen as Celebration

Every boot shows hardware detection with ENABLED highlights and unlock counts. The last line: "Your machine has unlocked NN features." On the second and later boots, the screen leads with any new hardware detected since the previous boot.

### 5.8 Settings Panel as Treasure Map

Locked features shown greyed out with requirements listed. Users see exactly what they could unlock. The panel is a category tree. A locked feature looks like:

```
[ ] AWE32 SoundFont loading                           requires AWE32/64
```

with the requirement in dim text. This is the treasure map: it tells you what the next eBay search is for.

### 5.9 Hall of Recognition

Persistent timeline of detected hardware with first seen dates. Records the user's retro computing journey. Survives reinstalls (file is text). Each detection adds an event row. Each lifetime tally is updated on close.

### 5.10 Dormant Feature Whisper

When an unlocked feature has not been used in 60+ days, HEARO may produce a single, status bar level whisper:

```
You have an MT-32. There is a Roland synth bank loaded with two unused patches.
```

Whispers are rate limited: one per session, globally. They never block, never modal, never animate. They never repeat the same message twice in 90 days.

### 5.11 Anti Gamification

No achievements. No streaks. No daily nudges. Recognition, not gamification. Records, not scores.

## 6. System Requirements

| Tier | CPU | FPU | RAM | Video | Audio |
|------|-----|-----|-----|-------|-------|
| Minimum | 286/12 | none | 1MB | MDA | PC Speaker |
| Standard | 386SX/16 | none | 2MB | EGA | AdLib |
| Enhanced | 386DX/33 | optional | 4MB | VGA | SB |
| Workstation | 486DX/33 | integrated | 8MB | SVGA 640 | SB16/GUS |
| Maximum | 486DX2/66+ | integrated | 16MB | SVGA 1024 | GUS/AWE32 |
| XT-FPU (community) | V20/8MHz | 8087 | 640K | CGA/EGA | AdLib/SB |

## 7. Audio Devices

Every period appropriate DOS audio device. PC Speaker listed alongside GUS MAX, deadpan. See `hearo-soundcard-reference.md` for the full probe protocol on each. The cards covered in detail are:

PC Speaker, Tandy PSG, Covox, Disney Sound Source, AdLib (OPL2), AdLib Gold (OPL3 + DAC), Sound Blaster 1.x, SB 2.0, SB Pro, SB Pro 2, SB16, SB16 ASP, AWE32, AWE64, GUS Classic, GUS MAX, GUS ACE, GUS PnP / InterWave, Ensoniq SoundScape, Pro Audio Spectrum 16, ESS AudioDrive, Turtle Beach MultiSound, MPU-401 + identity SysEx for MT-32 / CM-32L / SC-55 / SC-55mkII / SC-88 / SC-88 Pro / DB50XG / SCB-55.

## 8. Format Support

**Host decoded:** MIDI (SMF), WAV (PCM 8/16/24), MOD/S3M/XM/IT trackers, SID, AY, NSF, GBS, SPC.
**ESP32 offloaded:** MP3, FLAC, Opus, Vorbis, AAC, WMA, ALAC.
**Streaming:** Icecast/SHOUTcast, Bandcamp, SomaFM, Internet Archive, Nectarine, ModArchive.
**Output formats:** any HEARO supported audio device, with the appropriate mixing path: tracker mixing on SB/SB Pro, hardware mix on GUS, MIDI on MPU/AWE/MT-32.

## 9. Video Tiers

Six tiers, each with a deliberately designed UI:

| Tier | Resolution | UI |
|------|-----------|----|
| Workstation | 1024x768x256 | Four pane, album art thumbnails, full visualiser canvas |
| Enhanced | 800x600x256 | Four pane, tighter thumbnails |
| Standard | 640x480x256 | Three pane, no thumbnails |
| Classic | 320x200x256 | Two pane, demoscene aesthetic |
| Legacy | 640x350x16 | EGA, austere palette |
| Minimal | 720x350 text | MDA/Hercules, ASCII spectrum |

The text mode UI (Minimal) is the v1.0 deliverable. The graphical tiers are scaffolded but rendered placeholder until v1.3.

## 10. Visualisation

Every visualiser has both a high quality (FPU) and low quality (FPU less, bipartite/stochastic) implementation. The user can choose; defaults follow detected hardware.

- Spectrum analyser: 16/64/256 bin
- Oscilloscope
- Tracker pattern view (works for all formats, not just trackers)
- VU meters
- Plasma, tunnel, particle, fire, wireframe 3D
- Beat detection driven LED panel
- ASCII bars (text mode)

Plugin format: `.HVP` files (HEARO Visualiser Plugin). Demoscene mode (F11 in graphical UI): visualiser fills screen, controls hidden until keypress.

## 11. Streaming Services

Internet radio (Icecast/SHOUTcast), Bandcamp (purchases + free), SomaFM, Internet Archive (78s, Live Music Archive), Nectarine Demoscene Radio, ModArchive (lookup + stream).

Beyond v1.0: Spotify Connect (target only), YouTube Music (mp3 transcode via ESP32), SoundCloud, podcasts, audiobooks (with chapter resume).

## 12. Library Management

Library scan with MusicBrainz tagging, Cover Art Archive, lyrics, karaoke mode, smart playlists, ReplayGain. Library as biography: each track gains a `play_count` and `first_added` date that survives library rebuilds. Album view is "first time you heard this", not "most recently played".

## 13. Flagship Features

Recognition stack (primary), streaming and library (catch up to other players), network audio (frontier), polish (generic but expected).

`/BENCHMARK` mode runs a deterministic suite that measures detection speed, decode throughput per format, mixer throughput per channel count, and visualiser frame rate. Results are appended to `HEARO.STA`.

`/SAFE` boots without probing audio cards: only PC Speaker is listed. Useful for diagnosing hangs.

`/REDETECT` forces a full hardware rescan and replaces the saved profile.

Sleep timer, crossfade, EQ, stereo widening, Discord Rich Presence (when DISCORD app is also running on the same NetISA card).

## 14. Memory Model

Real mode: ~540KB ceiling for HEARO.EXE (16 bit). XMS for sample cache, library index, visualiser tables. EMS supported but not preferred. DPMI for SVGA framebuffer access.

Both `HEARO.EXE` (real mode, 286+) and `HEAROXM.EXE` (32 bit protected mode, 386+) ship. The 32 bit binary is preferred when available; the 16 bit binary is the universal fallback.

## 15. Phased Roadmap

- **v1.0 (current):** Foundation + Recognition. Native formats, full unlock system, boot celebration, Hall, text mode UI.
- **v1.1:** Streaming. ESP32 path, internet radio, Bandcamp, ModArchive.
- **v1.2:** Smart Library. MusicBrainz, scrobbling, cover art, lyrics, ReplayGain, smart playlists.
- **v1.3:** Demoscene Polish. Advanced visualisers, skins, karaoke, EQ, graphical tiers (320, 640, 800, 1024).
- **v1.4:** Network Audio. Bluetooth out, AirPlay target, Chromecast target, DLNA renderer, multi room.
- **v1.5:** Services. Spotify Connect target, YouTube Music, podcasts, audiobooks.

Recognition density carries through every phase. Even in v1.5, the boot screen is still the first thing you see.

## 16. Anti features

Not a tracker. Not a DAW. Not a podcast manager pretending to be a music player. Not gamified. Not generic. Not "minimal" in the sense of absent features; minimal in the sense of no ornamentation that distracts from the music or the machine.

## 17. Novel Arithmetic

Six experimental techniques used in service of recognition density and graceful degradation:

1. **Adaptive precision CORDIC.** Visualisers vary CORDIC iteration count based on screen size and frame budget. A 320x200 plasma at 30 fps on a 386SX runs CORDIC at 8 iterations; the same plasma at 1024x768 on a 486DX2 runs at 24 iterations.
2. **Software quire.** A 256 bit accumulator (Posit Standard 2022 technique) gives bit exact mixing for arbitrary mixers. Used when `UL_EXACT_MIX` is unlocked.
3. **Stochastic computing particles.** For FPU less systems, particle physics is approximated with biased random bits and counters. No multiplications.
4. **Bipartite tables.** Sin/cos/log/exp at 8+ bits accuracy from two table lookups and an add. ~99% of full precision in 0.3% of the time.
5. **Log domain effects.** Compressor/limiter/EQ implemented in log domain (Takum 2024 technique) avoids many multiplies.
6. **8087 silicon fingerprint.** The exact bit pattern of `FPATAN` and `FYL2X` differs across IIT, Cyrix, ULSI, AMD, Intel implementations. We use this to brand the FPU when CPUID is unavailable.

## 18. The Phased Run Loop (engineering)

A typical run goes:

1. `cmdline_parse` and config load.
2. `detect_all` populates the master `hw_profile_t`.
3. `unlock_evaluate` walks the unlock table and sets each entry to unlocked or locked.
4. `hall_load` reads `HEARO.HAL`, computes diff vs current detection, appends new events.
5. `boot_screen_render` displays the celebratory page.
6. `ui_run` enters the four pane UI loop until quit.
7. `hall_save` and `config_save` write back state.

This loop is short by design. The expensive work is detection and the UI loop; everything else is config and persistence.

## Closing Note

HEARO's premise: your machine is interesting, and most software treats it as generic. We don't. The boot screen is a love letter to specifics. The settings panel is a treasure map. The Hall of Recognition is a logbook. Every detected expansion gets noticed, every unlocked feature is visible, every locked feature names its requirement, and every machine, no matter how modest, is the protagonist.
