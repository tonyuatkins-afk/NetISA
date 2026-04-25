# DISCORD

Chat client for IBM PC compatibles (8088+, DOS 3.3+). Part of the [NetISA](../README.md) software suite.

## Status

v2 (ground-up rebuild). Builds clean under Open Watcom V2.

## Features

- 8 channels with unread counts.
- 128 messages per channel on far-heap storage.
- Multi-line compose (Shift+Enter).
- In-message find (Ctrl+F) with match highlighting.
- 8 hash-based author colors.
- CP437 reaction display.
- Thread reply indicators.
- User list overlay (Alt+U).
- PC speaker notifications (4 types, F9 toggle).
- VGA palette fade in/out.
- DISCORD.CFG settings persistence.

## Requirements

- CPU: 8088 or higher
- RAM: 256 KB minimum
- DOS: 3.3 or higher
- Video: any (CGA minimum, EGA/VGA recommended)
- NetISA card: required for live use (stub backend ships for development)

## Building

Requires Open Watcom V2.

```
cd discord
wmake           # DISCORD.EXE
wmake clean     # remove build artifacts
```

## Source layout

- `src/main.c` &mdash; entry point
- `src/discord.c` &mdash; channel + message state
- `src/render_dc.c` &mdash; rendering
- `src/input_dc.c` &mdash; keyboard handling
- `src/audio_dc.c` &mdash; PC speaker notification cues
- `src/config_dc.c` &mdash; `DISCORD.CFG` reader/writer
- `src/search_dc.c` &mdash; in-message find
- `src/stub_discord.c` &mdash; stub backend for development
- `../lib/screen.c` &mdash; shared suite library

## License

MIT. See [LICENSE](../LICENSE).

## Author

Tony Atkins ([@tonyuatkins-afk](https://github.com/tonyuatkins-afk))
