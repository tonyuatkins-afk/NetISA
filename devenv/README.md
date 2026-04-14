# NetISA DOSBox-X Development Relay

A development relay system that lets you compile, run, and test MS-DOS
programs inside DOSBox-X from the Windows command line or Claude Code.

## Prerequisites

- **DOSBox-X 2024.x or later** — earlier versions don't support `-nopromptfolder` and the `fastbcastv` config option. Tested with DOSBox-X 2026.03.29. Installed and on PATH (or in standard install location).
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

- **`CALL batch > file` does not capture child output.** DOSBox-X's
  COMMAND.COM only applies the redirect to the CALL builtin itself,
  not to commands inside the called batch. Workaround: embed per-command
  `>> _RESULT.TXT` in `_DOSCMD.BAT` rather than redirecting the CALL.

- **Return codes above 10 are quantized.** The ERRORLEVEL cascade in
  `_RELAY.BAT` has 30 thresholds (1-10 exact, then steps of 5/10/25).
  Values between thresholds are rounded down to the nearest lower
  threshold. This is a DOS batch limitation — COMMAND.COM lacks
  `%ERRORLEVEL%` so we can't capture the exact value.

- **ERRORLEVEL reflects the LAST command's exit, and may be clobbered
  by I/O redirect success.** Each user command in `_DOSCMD.BAT` is
  followed by `>> _RESULT.TXT`. On some COMMAND.COM versions, a
  successful redirect resets ERRORLEVEL to 0 before the relay reads
  it, masking a failing command. There is no clean fix in DOS batch
  (COMMAND.COM has no `%ERRORLEVEL%` variable to stash between the
  command and the redirect). For reliable exit-status checking, run
  one command per `dosrun.py` invocation so the captured retcode
  can only come from that single command.

## Automation Gotchas (Lessons from Building This)

Building reliable DOSBox-X automation is harder than it looks. These
lessons apply to both this relay and the GIF capture toolkit in
`dos/tools/capture.ps1`:

1. **Always pass `-nopromptfolder`** to dosbox-x.exe, or you get the
   first-run dialog instead of your app.

2. **Don't pass app arguments via `-c`.** DOSBox-X's `-c "APP arg"` flag
   drops arguments with special characters (e.g., colons in `about:npr`).
   Embed the app launch directly in the generated config's
   `[autoexec]` section instead.

3. **Launch at target state, don't navigate.** Design your DOS apps to
   accept CLI args for initial state (URL, channel, scenario). AUTOTYPE
   keystroke injection is fragile for complex navigation; launching the
   app in the target state is more reliable than trying to navigate
   there with keystrokes.

4. **AUTOTYPE timing is relative to its invocation, not app start.**
   AUTOTYPE runs in autoexec before your app launches. Set the initial
   `-w` delay to at least 2x your app's load time.

5. **CR+LF for all batch files.** DOS batch parses as LF-only but fails
   silently on GOTO labels, IF statements, and control flow. The relay
   normalizes `_RELAY.BAT` to CR+LF when copying.

6. **Absolute paths for all sentinel files.** If commands CD to a
   subdirectory, relative file writes go to the new CWD. All relay
   temp files use `C:\` prefix.

7. **Verify automated captures visually.** A batch run can silently
   produce wrong output — identical GIFs, wrong app, cropped content.
   Always eyeball the first frame.

See `~/.claude/projects/C--Development/memory/feedback_dosbox_automation.md`
for the complete 10-rule reference.
