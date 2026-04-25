# CHIME

Network time-sync tool for the [NetISA](../README.md) suite. Targets IBM PC compatibles 286+ on DOS 3.3+. Asks the network for the current time, sets the DOS clock.

v1.0 uses HTTPS `HEAD` and parses the response `Date:` header. SNTP/UDP comes in v1.1 once NetISA exposes UDP datagrams.

## Status

Source tree complete; not compiled yet.

## Features (v1.0)

- HTTPS `HEAD` against time-anchor URLs (Cloudflare, Google, worldtimeapi).
- HTTPS `GET` JSON fallback.
- Plain HTTP `HEAD` diagnostic mode.
- One-second precision (HTTP `Date:` header limit).
- Confirmation prompt by default; `/AUTO` for batch use, `/DRYRUN` to test.
- INT 21h date+time set plus CMOS RTC write-through for systems where DOS does not propagate.
- Timezone as fixed offset. DST not handled.
- `/STUBNET` synthesises a present NetISA card so the program runs on a workstation without hardware.

## Requirements

- CPU: 80286 or higher
- RAM: 256 KB minimum
- DOS: 3.3 or higher
- NetISA card with TLS path active (or `/STUBNET` for development)

## Building

Requires Open Watcom V2.

```
cd chime
wmake           # CHIME.EXE   (16-bit, 286+)
wmake xm        # CHIMEXM.EXE (32-bit, 386+)
wmake tests     # test executables
```

## Usage

```
CHIME                       sync, prompt before writing
CHIME /AUTO                 sync, write without prompt
CHIME /DRYRUN               report only, never write
CHIME /SERVER=time.google.com
CHIME /TZ=-08:00            local timezone offset
CHIME /MODE=https-head      force a specific time source
CHIME /VERSION              print version, exit
CHIME /HELP                 print this list, exit
CHIME /STUBNET              synthesise NetISA presence (development)
```

## Documents

- [Design document](../docs/chime-design.md)

## License

MIT. See [LICENSE](../LICENSE).

## Author

Tony Atkins ([@tonyuatkins-afk](https://github.com/tonyuatkins-afk))
