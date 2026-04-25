# HEARO scripts

Tooling around HEARO. Not part of the build itself; run from this directory.

## screenshot.ps1 — boot-screen PNG capture

Drives DOSBox-X to the HEARO boot screen and saves a PNG via the existing
`capture.ps1` toolkit at `C:\Development\Screenshots\capture.ps1`.

### Quick start

```
cd hearo\scripts
.\screenshot.bat
```

That writes `..\..\Screenshots\hearo_boot.png` from a 486DX2 / SVGA / SB16 / GUS
configuration.

### Variants

The default config (`screenshot.conf`) emulates a Workstation tier machine.
Change `[cpu] cputype/cycles` and the `[sblaster]/[gus]` sections to capture
other personalities, then call:

```
.\screenshot.ps1 -Conf .\screenshot-pentium.conf -Tag pentium
.\screenshot.ps1 -Conf .\screenshot-286.conf     -Tag minimum -BootDelay 10
```

The `-Tag` value is suffixed onto the output filename:
`hearo_boot.png` + tag `minimum` → `hearo_boot_minimum.png`.

### How it works

1. Verifies `HEARO.EXE` exists in `..\` (build with `wmake` first).
2. Launches DOSBox-X with `-conf screenshot.conf -nopromptfolder`.
3. The conf's `[autoexec]` mounts the hearo dir as C:, sets BLASTER and
   ULTRASND, then runs `HEARO.EXE`.
4. After `-BootDelay` seconds, the boot screen has rendered and is blocked on
   `INT 16h AH=00h` waiting for a keypress. We call `capture.ps1` to grab the
   client area.
5. We force-close DOSBox-X. The boot screen never advances; we just took the
   picture and walked away.

### Pitfalls

- DOSBox-X path is hard-coded to the WinGet install location. Override with
  `-DosboxExe` if installed elsewhere.
- The `[autoexec]` mount path is absolute (`C:\Development\NetISA\hearo`).
  Edit `screenshot.conf` if your checkout lives elsewhere.
- If `BLASTER`/`ULTRASND` are unset in the conf, audio detection falls back
  to blind probes, which is fine but slower.
