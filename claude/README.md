# CLAUDE

Anthropic API client for IBM PC compatibles (8088+, DOS 3.3+). Part of the [NetISA](../README.md) software suite.

## Status

Builds clean under Open Watcom V2. Stub backend for development; live API requires the NetISA card's TLS path.

## Features

- Chat mode: prompt, scrollable conversation, message compose.
- Agent mode: stream of tool calls + thinking blocks (when API surfaces them).
- VGA splash on launch.
- Conversation history saved to disk.

## Requirements

- CPU: 8088 or higher
- RAM: 256 KB minimum
- DOS: 3.3 or higher
- Video: any (EGA/VGA recommended for the splash)
- NetISA card: required for live use; stub ships for development

## Building

Requires Open Watcom V2.

```
cd claude
wmake           # CLAUDE.EXE
wmake clean     # remove build artifacts
```

## Source layout

- `src/main.c` &mdash; entry point
- `src/claude.c` &mdash; session state
- `src/chat.c` &mdash; chat mode UI
- `src/compose.c` &mdash; message editor
- `src/agent.c` &mdash; agent mode UI
- `src/splash.c` &mdash; launch splash
- `src/stub_claude.c` &mdash; stub backend
- `../lib/screen.c` &mdash; shared suite library

## License

MIT. See [LICENSE](../LICENSE).

## Author

Tony Atkins ([@tonyuatkins-afk](https://github.com/tonyuatkins-afk))
