# HEARO

Music player for IBM PC compatibles (286+, DOS 3.3+). Part of the [NetISA](../README.md) software suite.

## Status

Phase 2 (audio engine) complete. WAV and ProTracker MOD play through Sound Blaster 16 in DOSBox-X. Drivers shipped for the entire Sound Blaster family, AdLib OPL2/OPL3, MPU-401, Gravis UltraSound, and PC Speaker. Decoders shipped for WAV and MOD; S3M/XM/IT/MIDI loaders identify their formats but Phase 3 will land the playback paths.

## Features

Working today:

- WAV (PCM 8/16-bit, mono/stereo) playback via Sound Blaster 1.x through SB16/AWE
- ProTracker MOD (4/6/8 channel) playback with the canonical effect set
- 32-channel software mixer, with a quire (256-bit accumulator) path that lights up when an FPU is present
- Hardware detection across CPU/FPU/memory/video/24 audio devices
- Four-pane text-mode UI, boot screen, Hall of Recognition log
- Six test programs (TESTDET, TESTBOOT, TESTCORD, TESTQUIR, TESTBIP, TESTUI, TESTPLAY)

Planned for Phase 3:

- S3M, XM, IT decoders
- MIDI sequencer driving OPL2/OPL3 FM (GENMIDI bank) and MPU-401 / external synths
- GUS hardware-mixed playback path (samples uploaded to GF1 DRAM, voices triggered directly)
- VGM chip-music format (OPL2/OPL3, SN76489/Tandy)
- Real-iron validation on the 486 dev box

Future:

- Streamed playback via NetISA: MP3, FLAC, Opus, Vorbis, AAC
- Streaming services: Bandcamp, SomaFM, Internet Archive, Nectarine, ModArchive
- MBROLA voice announcements (track titles, system status)

## Requirements

- CPU: 80286 or higher
- RAM: 1 MB minimum
- DOS: 3.3 or higher
- Audio: any (PC Speaker minimum)
- Video: any (MDA minimum)
- NetISA card: optional for v1.0; required for streaming in v1.1+

## Building

Requires Open Watcom V2 and NASM.

```
cd hearo
wmake           # HEARO.EXE + tests (16-bit, 286+)
wmake xm        # HEAROXM.EXE (32-bit, 386+)
wmake clean     # remove build artifacts
```

## DOS 8.3 filename convention

Every shipped file (EXE, BAT, TXT, data fixture) must fit DOS 8.3 naming:
name <= 8 chars, extension <= 3 chars, no hyphens in extensions. Win98's
DOS box hides violations via VFAT, but real-mode DOS (and any program
launched with "Restart in MS-DOS mode") only sees the auto-generated
short alias (e.g. `EVERYTHING.BAT` becomes `EVERYT~1.BAT`), which user
typing of the long name fails to find. Failure mode: file appears to
not exist in MS-DOS mode even though it's present.

Long names that are reasonable inside the source repo (e.g. README.md,
hearo-design.md) are fine because they're never executed under DOS.
Anything that ships in a real-iron bundle must be 8.3.

## Testing in DOSBox-X

```
dosbox-x -conf scripts/test-play-wav.conf       # WAV via SB16
dosbox-x -conf scripts/test-play-mod.conf       # MOD via SB16
dosbox-x -conf scripts/test-pentium.conf        # TESTDET on Pentium config
```

`scripts/screenshot.ps1` captures the boot screen via the DOSBox-X capture toolkit.

## Documents

- [Design document](../docs/hearo-design.md)
- [Soundcard reference](../docs/hearo-soundcard-reference.md)

## License

MIT. See [LICENSE](../LICENSE).

## Author

Tony Atkins ([@tonyuatkins-afk](https://github.com/tonyuatkins-afk))
