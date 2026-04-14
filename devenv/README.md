# NetISA DOSBox-X Development Relay

A development relay system that lets you compile, run, and test MS-DOS
programs inside DOSBox-X from the Windows command line or Claude Code.

## Prerequisites

- **DOSBox-X** -- installed and on PATH (or in standard install location)
- **OpenWatcom 2.0** -- `C:\WATCOM` with `BINNT64\wcc.exe` and `wlink.exe`
- **NASM** -- on PATH or in `C:\Program Files\NASM\`
- **Python 3.10+** -- on PATH

## Quick Start

```bat
REM Run a DOS command and capture output
python devenv\dosrun.py "VER"
python devenv\dosrun.py "DIR C:\" "ECHO Hello from DOS"

REM Assemble and run NISATEST
python devenv\dosbuild.py --asm phase0\dos\nisatest.asm --run NISATEST.COM --cwd \phase0\dos

REM Compile C and run
python devenv\dosbuild.py --cc foo.c --link FOO.EXE --run FOO.EXE
```

## Architecture

```
Host (Windows)                    DOSBox-X (DOS)
===============                   ==============
dosrun.py                         dev.conf autoexec
  |-- writes _DOSCMD.BAT             |-- mounts project as C:
  |-- copies _RELAY.BAT              |-- calls _RELAY.BAT
  |-- launches DOSBox-X               |   |-- runs _DOSCMD.BAT > _RESULT.TXT
  |-- polls for _DONE.TXT             |   |-- writes _RETCODE.TXT
  |-- reads _RESULT.TXT               |   |-- writes _DONE.TXT
  |-- kills on timeout                 |-- EXIT
  |-- cleans up temp files
```

## Tools

### dosrun.py

Runs arbitrary DOS commands inside DOSBox-X and captures stdout.

```
python devenv\dosrun.py [OPTIONS] "COMMAND1" ["COMMAND2" ...]

Options:
  --timeout N     Kill DOSBox-X after N seconds (default: 30)
  --cwd DIR       DOS-side working directory
  --work-dir DIR  Host directory to mount as C: (default: project root)
  --mount D: PATH Additional mount point
  -v, --verbose   Debug output
```

### dosbuild.py

Compiles source files natively on Windows, then optionally runs the
result in DOSBox-X.

```
python devenv\dosbuild.py [OPTIONS]

Source files:
  --asm FILE        NASM assembly source
  --cc FILE         C source (repeatable)

Build options:
  --cc-flags FLAGS  OpenWatcom flags (default: "-0 -ms -s -ox -w4 -zq -bt=dos")
  --asm-flags FLAGS NASM flags (default: "-f bin")
  --link NAME       Link objects into executable
  --link-system SYS wlink system (default: "dos com")
  -o, --output FILE Output path for single-source builds

Run options:
  --run EXE         Run executable in DOSBox-X after building
  --run-args ARGS   Arguments for the DOS executable
  --cwd DIR         DOS-side working directory
  --timeout N       DOSBox-X timeout (default: 30)
```

## Temp Files

The relay uses these files in the mounted work directory:

| File | Purpose | Cleaned up? |
|------|---------|-------------|
| `_DOSCMD.BAT` | Commands to execute | Yes |
| `_RELAY.BAT` | Relay script (copied) | Yes |
| `_RESULT.TXT` | Captured stdout | Yes |
| `_RETCODE.TXT` | DOS exit code | Yes |
| `_DONE.TXT` | Completion sentinel | Yes |

All temp files are cleaned up after every run, even on failure.

## Known Limitations

- **STDERR from DOS commands is lost.** DOSBox-X's `COMMAND.COM` does
  not support `2>&1` redirection. Using `2>&1` creates a literal file
  named `&1` instead of merging streams. There is no workaround within
  the relay; only stdout is captured via `>> _RESULT.TXT`. If a DOS
  program writes error output to stderr, that output will not appear in
  the captured results.
