# CATHODE

Text-mode web browser for IBM PC compatibles (8088+, DOS 3.3+). Part of the [NetISA](../README.md) software suite.

Sort of a browser. Renders HTML to the text mode buffer, fetches over the NetISA card's TLS path, handles forms (poorly), follows links (mostly).

## Status

v0.2. Builds clean under Open Watcom V2. 12 of 12 fixture tests pass. Three parser bugs fixed in the last sweep.

## Features

- Text-mode UI with a green-on-black phosphor aesthetic.
- HTML tokenizer + flow renderer with line wrapping at the terminal width.
- URL bar with history.
- Bookmarks, persistent across runs.
- In-page find (Ctrl+F).
- UTF-8 input collapsed to CP437 for display.
- Shortcuts: Tab to next link, F5 to reload, Ctrl+F to find, Esc to quit.
- Stub backend for development without a NetISA card; real backend (`CATHODE_HW.EXE`) runs against the INT 63h API.

## Requirements

- CPU: 8088 or higher
- RAM: 256 KB minimum
- DOS: 3.3 or higher
- Video: any (CGA minimum, EGA/VGA recommended)
- NetISA card: required for `CATHODE_HW.EXE`; not for `CATHODE.EXE` stub build

## Building

Requires Open Watcom V2.

```
cd cathode
wmake           # CATHODE.EXE  (stub backend, runs on any DOS)
wmake hw        # CATHODE_HW.EXE (real INT 63h backend)
wmake clean     # remove build artifacts
```

## Source layout

- `src/main.c` &mdash; entry point
- `src/browser.c` &mdash; navigation, history, error handling
- `src/render.c`, `src/page.c` &mdash; rendering + page state
- `src/htmltok.c`, `src/htmlout.c` &mdash; HTML parser, output emitter
- `src/input.c`, `src/urlbar.c` &mdash; keyboard handling
- `src/fetch.c` &mdash; NetISA TLS session client
- `src/stub_pages.c` &mdash; canned pages for development
- `src/url.c`, `src/utf8.c` &mdash; URL parsing, UTF-8 transcode
- `src/search.c`, `src/bookmark.c` &mdash; in-page find, bookmarks
- `../lib/screen.c`, `../lib/netisa.c`, `../lib/netisa_stub.c` &mdash; shared suite library

## License

MIT. See [LICENSE](../LICENSE).

## Author

Tony Atkins ([@tonyuatkins-afk](https://github.com/tonyuatkins-afk))
