<#
.SYNOPSIS
    Capture animated GIFs of Cathode browser running in DOSBox-X.

.DESCRIPTION
    Launches DOSBox-X with a preset configuration, captures frames
    via PrintWindow, and assembles an animated GIF with ffmpeg.

    Presets:
      home      - Home page with CATHODE logo and links
      npr       - NPR news page with scrolling and link navigation
      test      - Feature test page showing HTML rendering
      browse    - Full browsing demo: home -> NPR -> scroll -> search

.PARAMETER Preset
    Named capture preset.

.PARAMETER Duration
    Recording duration in seconds (overrides preset).

.PARAMETER FPS
    Frames per second (default: 4).

.PARAMETER StartDelay
    Seconds to wait before recording.

.PARAMETER OutFile
    Output GIF path.

.PARAMETER GifWidth
    Output GIF width in pixels (default: 480).

.PARAMETER KeepFrames
    Keep frame directory after GIF creation.
#>

param(
    [Parameter(Mandatory)][string]$Preset,
    [int]$Duration = 0,
    [int]$FPS = 4,
    [int]$StartDelay = 0,
    [string]$OutFile = "",
    [int]$GifWidth = 480,
    [switch]$KeepFrames
)

$ErrorActionPreference = "Stop"

# ── Paths ──────────────────────────────────────────────────────
$dosboxExe  = "C:\Users\tonyu\AppData\Local\Microsoft\WinGet\Packages\joncampbell123.DOSBox-X_Microsoft.Winget.Source_8wekyb3d8bbwe\bin\x64\Release SDL2\dosbox-x.exe"
$ffmpegExe  = "C:\Users\tonyu\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe\ffmpeg-8.1-full_build\bin\ffmpeg.exe"
$projectDir = "C:\Development\NetISA\dos"
$websiteImg = "C:\Development\tonyuatkins-afk.github.io\img"
$tempDir    = "$projectDir\tools\frames_temp"

# ── Presets ────────────────────────────────────────────────────
$presets = @{
    "home" = @{
        Cmd   = "CATHODE.EXE"
        Auto  = ""
        Dur   = 8
        Delay = 5
    }
    "npr" = @{
        Cmd   = "CATHODE.EXE"
        Auto  = "-w 3000 -p 500 tab -p 500 tab -p 500 tab -p 500 tab -p 500 tab -p 500 enter -w 2000 -p 300 down -p 300 down -p 300 down -p 300 down -p 300 down -p 300 down -p 300 down -p 300 down -p 500 tab -p 500 tab -p 500 tab"
        Dur   = 18
        Delay = 4
    }
    "test" = @{
        Cmd   = "CATHODE.EXE"
        Auto  = "-w 3000 -p 500 tab -p 800 enter -w 2000 -p 400 down -p 400 down -p 400 down -p 400 down -p 400 down -p 400 down -p 400 down -p 400 down -p 400 down -p 400 down -p 400 down -p 400 down"
        Dur   = 16
        Delay = 4
    }
    "browse" = @{
        Cmd   = "CATHODE.EXE"
        Auto  = "-w 3000 -p 500 tab -p 500 tab -p 500 tab -p 500 tab -p 500 tab -p 800 enter -w 2000 -p 300 down -p 300 down -p 300 down -p 300 down -p 300 down -p 300 down -p 300 down -p 300 down -p 300 down -p 300 down -p 500 tab -p 500 tab -p 500 tab -p 500 tab"
        Dur   = 22
        Delay = 4
    }
}

# Apply preset defaults, then override with CLI args
if ($presets.ContainsKey($Preset)) {
    $p = $presets[$Preset]
    if ($Duration -eq 0)   { $Duration  = $p.Dur }
    if ($StartDelay -eq 0) { $StartDelay = $p.Delay }
    $DosCmd   = $p.Cmd
    $AutoType = $p.Auto
} else {
    Write-Host "ERROR: Unknown preset '$Preset'. Available: $($presets.Keys -join ', ')"
    exit 1
}

if ($Duration -eq 0)   { $Duration = 15 }
if ($StartDelay -eq 0) { $StartDelay = 3 }

if ($OutFile -eq "") {
    $OutFile = "$websiteImg\cathode-$Preset.gif"
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
        PrintWindow(hWnd, hdc, 0x2);
        g.ReleaseHdc(hdc);
        g.Dispose();
        return bmp;
    }
}
'@

function Capture-Window($proc, $outPath) {
    $bmp = [WinCapture]::CaptureWindow($proc.MainWindowHandle)
    if ($bmp -eq $null) { return $false }

    $w = $bmp.Width
    $h = $bmp.Height

    # Crop DOSBox-X window chrome (title bar, borders)
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

# ── Build DOSBox-X config ──────────────────────────────────────
if (Test-Path $tempDir) { Remove-Item "$tempDir\*" -Force }
else { New-Item -ItemType Directory -Path $tempDir -Force | Out-Null }

$confPath = "$tempDir\capture.conf"
$autoexec = "MOUNT C $projectDir`nC:"
if ($AutoType) {
    $autoexec += "`nAUTOTYPE $AutoType"
}
$autoexec += "`n$DosCmd"

$conf = @"
[sdl]
output = surface
windowresolution = 720x450

[render]
aspect = true

[cpu]
cycles = 15000

[dosbox]
machine = svga_s3

[autoexec]
$autoexec
"@
$conf | Set-Content $confPath -Encoding ASCII

# ── Launch and capture ─────────────────────────────────────────
Write-Host "Cathode Capture"
Write-Host "  Preset:     $Preset"
Write-Host "  Command:    $DosCmd"
Write-Host "  AutoType:   $AutoType"
Write-Host "  Duration:   $Duration`s @ $FPS fps"
Write-Host "  StartDelay: $StartDelay`s"
Write-Host "  Output:     $OutFile"
Write-Host ""

Write-Host "[1/3] Launching DOSBox-X..."
$proc = Start-Process -FilePath $dosboxExe -ArgumentList "-conf",$confPath -PassThru

# Wait for window to appear
$attempts = 0
do {
    Start-Sleep -Milliseconds 500
    $attempts++
    $r = New-Object WinCapture+RECT
    $hasWindow = [WinCapture]::GetWindowRect($proc.MainWindowHandle, [ref]$r)
} while (-not $hasWindow -and $attempts -lt 20 -and -not $proc.HasExited)

if ($proc.HasExited) {
    Write-Host "ERROR: DOSBox-X exited before capture could start."
    exit 1
}

[WinCapture]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
Write-Host "  Waiting $StartDelay`s for app to initialize..."
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
    Write-Host "ERROR: Not enough frames captured."
    if (-not $proc.HasExited) { $proc.Kill(); $proc.WaitForExit(3000) }
    exit 1
}

# Kill DOSBox-X
if (-not $proc.HasExited) {
    $proc.Kill()
    $proc.WaitForExit(3000)
}

# ── Build GIF ──────────────────────────────────────────────────
Write-Host "[3/3] Building GIF via ffmpeg..."

$vf = "scale=${GifWidth}:-1:flags=lanczos,split[s0][s1];[s0]palettegen=max_colors=128:stats_mode=diff[p];[s1][p]paletteuse=dither=bayer:bayer_scale=3"
$inputPattern = "$tempDir\frame_%05d.png"
$ffCmd = "`"$ffmpegExe`" -y -framerate $FPS -i `"$inputPattern`" -vf `"$vf`" -loop 0 `"$OutFile`""
cmd.exe /c "$ffCmd 2>NUL"

if (Test-Path $OutFile) {
    $sizeKB = [math]::Round((Get-Item $OutFile).Length / 1024)
    Write-Host ""
    Write-Host "SUCCESS: $OutFile ($sizeKB KB, $captured frames)"
} else {
    Write-Host "ERROR: GIF was not created."
    exit 1
}

# Cleanup
if (-not $KeepFrames) {
    Remove-Item $tempDir -Recurse -Force
}

Write-Host "Done."
