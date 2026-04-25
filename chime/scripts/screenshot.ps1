# scripts/screenshot.ps1 - Capture CHIME [Y/n] prompt as PNG.
#
# Pipeline:
#   1. Verify CHIME.EXE exists in the chime/ directory.
#   2. Launch DOSBox-X with screenshot.conf (autoexec runs CHIME /STUBNET).
#   3. Wait for the report + prompt to render.
#   4. Drive the existing capture.ps1 toolkit to grab the client area.
#   5. Force-close DOSBox-X. CHIME blocks on getchar() at the [Y/n] prompt,
#      so the clock never gets written.
#
# Usage:
#   ./screenshot.ps1
#   ./screenshot.ps1 -BootDelay 8

param(
    [string]$Conf       = "$PSScriptRoot\screenshot.conf",
    [string]$OutFile    = "C:\Development\Screenshots\chime_prompt.png",
    [int]   $BootDelay  = 5,
    [string]$Tag        = "",
    [string]$DosboxExe  = "C:\Users\tonyu\AppData\Local\Microsoft\WinGet\Packages\joncampbell123.DOSBox-X_Microsoft.Winget.Source_8wekyb3d8bbwe\bin\x64\Release SDL2\dosbox-x.exe",
    [string]$Capture    = "C:\Development\Screenshots\capture.ps1"
)

$ErrorActionPreference = 'Stop'

$chimeDir = Resolve-Path "$PSScriptRoot\.."
$chimeExe = Join-Path $chimeDir "CHIME.EXE"
if (-not (Test-Path $chimeExe)) {
    Write-Error "CHIME.EXE not found at $chimeExe. Run wmake in $chimeDir first."
    exit 1
}
if (-not (Test-Path $Conf))      { Write-Error "Config not found: $Conf"; exit 1 }
if (-not (Test-Path $DosboxExe)) { Write-Error "DOSBox-X not found at $DosboxExe"; exit 1 }
if (-not (Test-Path $Capture))   { Write-Error "capture.ps1 not found at $Capture"; exit 1 }

if ($Tag) {
    $dir  = Split-Path $OutFile
    $name = [System.IO.Path]::GetFileNameWithoutExtension($OutFile)
    $ext  = [System.IO.Path]::GetExtension($OutFile)
    $OutFile = Join-Path $dir "$name`_$Tag$ext"
}
$outDir = Split-Path $OutFile
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }

Write-Host "Launching DOSBox-X with $Conf"
$proc = Start-Process -PassThru $DosboxExe -ArgumentList @("-conf", $Conf, "-nopromptfolder", "-fastlaunch")

try {
    Write-Host "Waiting $BootDelay s for prompt to render"
    Start-Sleep -Seconds $BootDelay

    Write-Host "Capturing to $OutFile"
    & $Capture -OutFile $OutFile -Delay 0

    if (Test-Path $OutFile) {
        $size = (Get-Item $OutFile).Length
        Write-Host "Saved $OutFile ($size bytes)"
    } else {
        Write-Warning "Capture produced no output file."
    }
} finally {
    Write-Host "Closing DOSBox-X (PID $($proc.Id))"
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
}
