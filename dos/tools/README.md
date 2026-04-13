# dos/tools/ — Screenshot and GIF Capture Toolkit

Captures animated GIFs and static screenshots of NetISA DOS applications
running in DOSBox-X. Used to generate media for barelybooting.com and
the GitHub README.

## Requirements

- **DOSBox-X** (installed via winget: `joncampbell123.DOSBox-X`)
- **ffmpeg** (installed via winget: `Gyan.FFmpeg`)

## Usage

```powershell
# From the NetISA repo root:
powershell -ExecutionPolicy Bypass -File dos/tools/capture.ps1 -App <EXE> [options]
```

### Examples

```powershell
# Claude splash + chat (18 seconds covers full splash animation)
.\dos\tools\capture.ps1 -App CLAUDE.EXE -Duration 18

# Discord with live messages (messages auto-inject every ~5s)
.\dos\tools\capture.ps1 -App DISCORD.EXE -Duration 20

# Launcher: navigate to WiFi Setup
.\dos\tools\capture.ps1 -App NETISA.EXE -Duration 12 -AutoType "-w 2000 -p 500 down down enter"

# Cathode browser: start page then navigate to test page
.\dos\tools\capture.ps1 -App CATHODE.EXE -Duration 15 -AutoType "-w 3000 -p 300 tab enter"

# Higher FPS for smoother animation (larger file)
.\dos\tools\capture.ps1 -App CLAUDE.EXE -Duration 18 -FPS 8

# Custom output path
.\dos\tools\capture.ps1 -App CLAUDE.EXE -OutFile C:\path\to\output.gif

# Keep raw frames for inspection
.\dos\tools\capture.ps1 -App DISCORD.EXE -Duration 15 -KeepFrames
```

### Parameters

| Parameter    | Default | Description |
|-------------|---------|-------------|
| `-App`      | required | DOS executable name (e.g. `CLAUDE.EXE`) |
| `-Duration` | 15      | Recording duration in seconds |
| `-FPS`      | 4       | Frames per second (4 = good balance of size/smoothness) |
| `-OutFile`  | auto    | Output path. Defaults to `<website>/img/<app>.gif` |
| `-AutoType` | none    | DOSBox-X AUTOTYPE string for simulating keypresses |
| `-GifWidth` | 640     | Output GIF width in pixels (height scales proportionally) |
| `-KeepFrames` | false | Keep the raw PNG frames after GIF creation |

### AutoType Keys

DOSBox-X AUTOTYPE simulates keypresses. Useful for navigating menus.

- `-w <ms>` — wait before first key
- `-p <ms>` — pause between keys
- Arrow keys: `up`, `down`, `left`, `right`
- `enter`, `tab`, `esc`, `space`
- Regular characters: just type them (e.g. `h e l l o`)

Example: `-w 2000 -p 300 down down enter` waits 2s, then presses down twice and Enter.

## How It Works

1. Creates a temporary DOSBox-X config with `output = surface` for GDI capture
2. Launches DOSBox-X with the target app via `-c` flag
3. Finds the DOSBox-X window via WinAPI `GetWindowRect`
4. Captures cropped frames (removes window chrome) at the specified FPS
5. Assembles frames into an optimized GIF via ffmpeg with palette generation

## Output Locations

- Website images: `C:\Development\tonyuatkins-afk.github.io\img\`
- README images: `C:\Development\NetISA\docs\img\`

Copy GIFs to both locations when updating both repos.

## From Claude Code

```
! powershell -ExecutionPolicy Bypass -File dos/tools/capture.ps1 -App CLAUDE.EXE -Duration 18
```

Or from bash:
```bash
powershell -ExecutionPolicy Bypass -File dos/tools/capture.ps1 -App DISCORD.EXE -Duration 20
```
