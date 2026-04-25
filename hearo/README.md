# HEARO

Music player for IBM PC compatibles (286+, DOS 3.3+). Part of the [NetISA](../README.md) software suite.

## Status

Pre-implementation. Source tree complete; not compiled yet.

## Features (planned)

- Native playback: MIDI, WAV, MOD/S3M/XM/IT, SID, AY, NSF, GBS, SPC.
- Streamed playback via NetISA: MP3, FLAC, Opus, Vorbis, AAC.
- Streaming services: Bandcamp, SomaFM, Internet Archive, Nectarine, ModArchive.
- Six video tiers: MDA text through 1024x768x256 SVGA.
- 24 audio devices, PC Speaker through Gravis UltraSound MAX.
- Detected hardware enables specific features. Boot screen lists what is on, settings panel lists what is off and what would unlock it.
- Hall of Recognition: persistent text log of detected hardware with first-seen dates.
- Math library: software quire (Posit Standard 2022), adaptive CORDIC, stochastic computing, bipartite tables.

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
wmake           # HEARO.EXE (16-bit, 286+)
wmake xm        # HEAROXM.EXE (32-bit, 386+)
wmake tests     # test executables
```

## Documents

- [Design document](../docs/hearo-design.md)
- [Soundcard reference](../docs/hearo-soundcard-reference.md)

## License

MIT. See [LICENSE](../LICENSE).

## Author

Tony Atkins ([@tonyuatkins-afk](https://github.com/tonyuatkins-afk))
