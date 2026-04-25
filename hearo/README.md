# HEARO

**Every format. Every era. One player.** Music for vintage PCs.

HEARO is a unified music player for IBM PC compatibles (286+, DOS 3.3+), part of the [NetISA](../README.md) software suite. It plays native formats on the host CPU, streams modern formats via the ESP32-S3 on the NetISA card, and celebrates the specific hardware in your machine through a recognition system that makes every upgrade feel noticed.

## Status

**Pre-implementation.** Design documents complete. Codebase scaffolding in progress.

## Key Features (Planned)

- Native playback: MIDI, WAV, MOD/S3M/XM/IT, SID, AY, NSF, GBS, SPC
- Streamed playback (via NetISA): MP3, FLAC, Opus, Vorbis, AAC
- Streaming services: Bandcamp, SomaFM, Internet Archive, Nectarine, ModArchive
- Six video tiers: MDA text through 1024x768x256 SVGA
- Hardware recognition: every detected expansion earns a boot-screen celebration
- Unlock system: FPU, sound cards, video chipsets unlock specific features
- Hall of Recognition: persistent timeline of your machine's hardware journey
- Novel arithmetic: software quire (Posit Standard 2022), adaptive CORDIC, stochastic computing
- PC Speaker output listed deadpan alongside Gravis Ultrasound MAX

## Requirements

- CPU: 80286 or higher
- RAM: 1MB minimum
- DOS: 3.3 or higher
- Audio: any (PC speaker at minimum)
- Video: any (MDA at minimum)
- NetISA card: optional for v1.0 (required for streaming in v1.1+)

## Building

Requires Open Watcom V2 and NASM.

```
cd hearo
wmake           # Build HEARO.EXE (16-bit, 286+)
wmake xm        # Build HEAROXM.EXE (32-bit, 386+)
wmake tests     # Build test executables
```

## Design Documents

- [HEARO Design Document](../docs/hearo-design.md)
- [Novel Arithmetic Additions](../docs/hearo-design-additions.md)
- [Scaffold Prompt](../docs/hearo-scaffold-prompt.md)

## License

MIT. See [LICENSE](../LICENSE).

## Author

Tony Atkins ([@tonyuatkins-afk](https://github.com/tonyuatkins-afk))
