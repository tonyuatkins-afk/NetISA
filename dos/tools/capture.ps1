<#
.SYNOPSIS
    Capture animated GIFs of NetISA DOS applications running in DOSBox-X.

.DESCRIPTION
    Launches DOSBox-X with a generated config, runs a DOS app, captures
    frames via PrintWindow WinAPI, converts to GIF via ffmpeg palette.

    IMPORTANT: App launch is embedded in the config's [autoexec] section,
    NOT passed via DOSBox-X's -c flag. The -c flag silently drops
    arguments with special characters (colons, spaces), so launching
    "CATHODE.EXE about:npr" via -c loses the URL argument. Embedding in
    autoexec bypasses this.

    For reliable captures:
    - Pass -nopromptfolder to avoid DOSBox-X first-run dialog
    - Design apps to accept CLI args for initial state (URL, channel,
      scenario) — launch at target state instead of navigating via
      AUTOTYPE
    - AUTOTYPE -w (initial wait) should be >= 2x app load time
    - Verify every new capture visually — identical GIFs indicate broken
      argument passing

    See ~/.claude/projects/.../memory/feedback_dosbox_automation.md
    for the complete automation ruleset.

.PARAMETER App
    DOS executable to run (e.g. "CLAUDE.EXE", "CATHODE.EXE about:npr").
    Arguments ARE supported when embedded in the app string.

.PARAMETER Duration
    Recording duration in seconds (default: 15).

.PARAMETER FPS
    Frames per second for capture (default: 4).

.PARAMETER OutFile
    Output GIF path. Defaults to website img/<app>.gif.

.PARAMETER AutoType
    DOSBox-X AUTOTYPE keystroke string (e.g. "-w 2000 -p 300 down enter").
    Prefer CLI args over AUTOTYPE for app state — AUTOTYPE is fragile
    for complex navigation.

.PARAMETER GifWidth
    Output GIF width in pixels (default: 640).

.PARAMETER StartDelay
    Seconds to wait after launching DOSBox-X before starting capture
    (lets the app render its initial state). Default: 3.

.PARAMETER KeepFrames
    If set, don't delete the frame directory after GIF creation.
#>

param(
    [Parameter(Mandatory)][string]$App,
    [int]$Duration = 15,
    [int]$FPS = 4,
    [string]$OutFile = "",
    [string]$AutoType = "",
    [int]$GifWidth = 640,
    [int]$StartDelay = 3,
    [switch]$KeepFrames
)

$ErrorActionPreference = "Stop"

# F14: Reject newlines in $App early. An embedded CR/LF would break
# out of the autoexec line in the generated config and let callers
# inject arbitrary DOSBox-X commands.
if ($App -match "[\r\n]") {
    Write-Host "ERROR: -App parameter cannot contain newlines."
    exit 1
}

# F6: Declare this process DPI-aware BEFORE loading WinCapture. On
# high-DPI displays (>100%), GetWindowRect and PrintWindow return
# physical pixels only when the process is DPI-aware; otherwise the
# OS scales coordinates to virtualized pixels and the hard-coded
# crop values (15/58/20/0) point at the wrong region of the frame.
# SetProcessDPIAware must be called once, before any window enumeration.
Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
public static class DPIHelper {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
[DPIHelper]::SetProcessDPIAware() | Out-Null

# ── Tool discovery ─────────────────────────────────────────────
# F-01: Hardcoded WinGet paths break on package upgrades (the version
# token in the path changes). Discover dynamically with env override,
# PATH, WinGet glob, then standard install locations.
function Find-DosBoxX {
    if ($env:DOSBOX_X -and (Test-Path $env:DOSBOX_X)) { return $env:DOSBOX_X }
    $cmd = Get-Command dosbox-x -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Path }
    $patterns = @(
        "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\joncampbell123.DOSBox-X_*\bin\x64\Release SDL2\dosbox-x.exe",
        "$env:ProgramFiles\DOSBox-X\dosbox-x.exe",
        "${env:ProgramFiles(x86)}\DOSBox-X\dosbox-x.exe"
    )
    foreach ($p in $patterns) {
        $found = Get-ChildItem -Path $p -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    Write-Host "ERROR: DOSBox-X not found. Install via WinGet (joncampbell123.DOSBox-X) or set `$env:DOSBOX_X"
    exit 1
}
function Find-FFmpeg {
    if ($env:FFMPEG -and (Test-Path $env:FFMPEG)) { return $env:FFMPEG }
    $cmd = Get-Command ffmpeg -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Path }
    $patterns = @(
        "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Gyan.FFmpeg_*\*\bin\ffmpeg.exe",
        "$env:ProgramFiles\ffmpeg\bin\ffmpeg.exe"
    )
    foreach ($p in $patterns) {
        $found = Get-ChildItem -Path $p -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    Write-Host "ERROR: ffmpeg not found. Install via WinGet (Gyan.FFmpeg) or set `$env:FFMPEG"
    exit 1
}

# ── Paths ──────────────────────────────────────────────────────
$dosboxExe = Find-DosBoxX
$ffmpegExe = Find-FFmpeg
$projectDir = "C:\Development\NetISA"
$websiteImg = "C:\Development\tonyuatkins-afk.github.io\img"
$tempDir    = "$projectDir\dos\tools\frames_temp"

if ($OutFile -eq "") {
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($App).ToLower()
    $OutFile = "$websiteImg\$baseName.gif"
}

# F22: Verify the output directory exists up-front. The default
# $websiteImg path is hardcoded to a sibling repo that may not be
# checked out on every machine; failing here is clearer than having
# ffmpeg silently fail to write the GIF 15 seconds into the capture.
$outDir = Split-Path -Parent $OutFile
if (-not (Test-Path $outDir)) {
    Write-Host "ERROR: Output directory does not exist: $outDir"
    Write-Host "Use -OutFile to specify a path in an existing directory."
    exit 1
}

# ── WinAPI ─────────────────────────────────────────────────────
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public class WinCapture {
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint nFlags);
    [DllImport("user32.dll")] public static extern IntPtr GetDC(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }

    public static Bitmap CaptureWindow(IntPtr hWnd) {
        RECT r;
        GetWindowRect(hWnd, out r);
        int w = r.Right - r.Left;
        int h = r.Bottom - r.Top;
        if (w < 100 || h < 100) return null;
        Bitmap bmp = new Bitmap(w, h);
        Graphics g = Graphics.FromImage(bmp);
        IntPtr hdc = g.GetHdc();
        // F-18: check PrintWindow's return; on failure the bitmap is
        // blank/garbage and we shouldn't silently emit a black frame.
        bool ok = PrintWindow(hWnd, hdc, 0x2); // PW_RENDERFULLCONTENT
        g.ReleaseHdc(hdc);
        g.Dispose();
        if (!ok) { bmp.Dispose(); return null; }
        return bmp;
    }
}
'@

function Capture-Window($proc, $outPath) {
    $bmp = [WinCapture]::CaptureWindow($proc.MainWindowHandle)
    if ($bmp -eq $null) { return $false }

    $w = $bmp.Width
    $h = $bmp.Height

    $cropL = 15; $cropT = 58; $cropR = 20; $cropB = 0
    $cw = $w - $cropL - $cropR
    $ch = $h - $cropT - $cropB
    if ($cw -lt 50 -or $ch -lt 50) { $bmp.Dispose(); return $false }

    $rect = New-Object System.Drawing.Rectangle($cropL, $cropT, $cw, $ch)
    $cropped = $bmp.Clone($rect, $bmp.PixelFormat)
    $bmp.Dispose()

    $cropped.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $cropped.Dispose()
    return $true
}

# ── Main ───────────────────────────────────────────────────────
Write-Host "NetISA DOSBox-X Capture Tool"
Write-Host "App: $App | Duration: $Duration`s | FPS: $FPS"
Write-Host "Output: $OutFile"
if ($AutoType) { Write-Host "AutoType: $AutoType" }
Write-Host ""

# Create temp directory
# F11: handle the edge case where $tempDir exists but is a FILE (not a
# directory) -- the old Test-Path branch would try to glob "$file\*"
# and fail. Test for PathType Leaf explicitly, remove, then ensure a
# directory exists. If it's already a directory, just clear its contents.
if (Test-Path $tempDir -PathType Leaf) {
    Remove-Item $tempDir -Force
}
if (-not (Test-Path $tempDir)) {
    New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
} else {
    Remove-Item "$tempDir\*" -Force -Recurse -ErrorAction SilentlyContinue
}

# Build DOSBox-X config
# Put app launch IN the autoexec rather than using -c flag, so args with
# special chars (like "about:npr" with a colon) pass through correctly.
$confPath = "$tempDir\capture.conf"
$autoLines = "MOUNT C `"$projectDir`"`nC:`nCD DOS"
if ($AutoType) {
    $autoLines += "`nAUTOTYPE $AutoType"
}
$autoLines += "`n$App"

$confContent = "[sdl]`noutput = surface`nwindowresolution = 720x400`n`n[render]`naspect = true`n`n[autoexec]`n$autoLines`n"
$confContent | Set-Content $confPath -Encoding ASCII

# Launch DOSBox-X
Write-Host "[1/3] Launching DOSBox-X..."
$proc = Start-Process -FilePath $dosboxExe -ArgumentList "-conf",$confPath,"-nopromptfolder" -PassThru

# F5: wrap the entire capture-and-encode pipeline in try/finally so
# we never leak a zombie DOSBox-X process on early `exit`. The old
# code returned via `exit 1` on too-few-frames without killing the
# process, leaving the DOSBox-X window open and any user script
# unable to re-run the capture (lock contention, GPU contention).
try {
    # Wait for window
    $attempts = 0
    do {
        Start-Sleep -Milliseconds 500
        $attempts++
        $r = New-Object WinCapture+RECT
        $hasWindow = [WinCapture]::GetWindowRect($proc.MainWindowHandle, [ref]$r)
    } while (-not $hasWindow -and $attempts -lt 10 -and -not $proc.HasExited)

    if ($proc.HasExited) {
        Write-Host "ERROR: DOSBox-X exited before capture."
        exit 1
    }

    [WinCapture]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
    # Wait for app to start (skip DOSBox-X boot screen)
    Write-Host "  Waiting $StartDelay`s for app to start..."
    Start-Sleep -Seconds $StartDelay

    # Capture frames
    $totalFrames = $Duration * $FPS
    $intervalMs = [math]::Floor(1000 / $FPS)
    $captured = 0

    Write-Host "[2/3] Capturing $totalFrames frames..."
    for ($i = 0; $i -lt $totalFrames; $i++) {
        if ($proc.HasExited) {
            Write-Host "  DOSBox-X exited after $captured frames."
            break
        }
        $framePath = "$tempDir\frame_{0:D5}.png" -f $i
        if (Capture-Window $proc $framePath) {
            $captured++
        }
        Start-Sleep -Milliseconds $intervalMs
    }
    Write-Host "  Captured $captured frames."

    if ($captured -lt 2) {
        Write-Host "ERROR: Not enough frames."
        exit 1
    }

    # Kill DOSBox-X if still running (before ffmpeg to free the GPU)
    if (-not $proc.HasExited) {
        $proc.Kill()
        $proc.WaitForExit(3000)
    }

    # Convert to GIF via ffmpeg
    Write-Host "[3/3] Building GIF via ffmpeg..."

    $vf = 'scale=' + $GifWidth + ':-1:flags=lanczos,split[s0][s1];[s0]palettegen=max_colors=64:stats_mode=diff[p];[s1][p]paletteuse=dither=bayer:bayer_scale=3'

    $inputPattern = "$tempDir\frame_%05d.png"
    # F13: capture ffmpeg stderr to a log file instead of discarding it
    # via `2>NUL`. If ffmpeg fails (bad filter graph, missing frames,
    # permission issues on the output path) the user needs the error
    # to diagnose; silent failure was the hardest class of bug here.
    $ffmpegLog = "$tempDir\ffmpeg.log"
    $ffCmd = "`"$ffmpegExe`" -y -framerate $FPS -i `"$inputPattern`" -vf `"$vf`" -loop 0 `"$OutFile`""
    cmd.exe /c "$ffCmd 2>`"$ffmpegLog`""

    if (Test-Path $OutFile) {
        $sizeKB = [math]::Round((Get-Item $OutFile).Length / 1024)
        Write-Host ""
        Write-Host "SUCCESS: $OutFile ($sizeKB KB, $captured frames)"
    } else {
        Write-Host "ERROR: GIF was not created."
        if (Test-Path $ffmpegLog) {
            Write-Host "ffmpeg output:"
            Get-Content $ffmpegLog | Select-Object -Last 20 | ForEach-Object { Write-Host "  $_" }
        }
        exit 1
    }

    Write-Host "Done."
} finally {
    # F5: always reap DOSBox-X. Covers `exit 1` branches above, Ctrl+C,
    # and any thrown exception during the capture loop.
    if ($proc -and -not $proc.HasExited) {
        $proc.Kill() | Out-Null
        $proc.WaitForExit(3000)
    }
    # F-02: Always wipe tempDir so ffmpeg failures (or any early exit)
    # don't leave thousands of PNGs behind. -KeepFrames preserves them
    # for debugging.
    if (-not $KeepFrames -and (Test-Path $tempDir)) {
        Remove-Item $tempDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}
