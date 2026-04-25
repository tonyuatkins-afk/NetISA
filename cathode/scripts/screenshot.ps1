# scripts/screenshot.ps1 - Capture CATHODE start page as PNG.
#
# CATHODE blocks on its own input loop, so DOSBox-X stays at the start
# page until killed. We capture during that wait window.

param(
    [string]$Conf       = "$PSScriptRoot\screenshot.conf",
    [string]$OutFile    = "C:\Development\Screenshots\cathode_start.png",
    [int]   $BootDelay  = 5,
    [string]$Tag        = "",
    [string]$DosboxExe  = "C:\Users\tonyu\AppData\Local\Microsoft\WinGet\Packages\joncampbell123.DOSBox-X_Microsoft.Winget.Source_8wekyb3d8bbwe\bin\x64\Release SDL2\dosbox-x.exe",
    [string]$Capture    = "C:\Development\Screenshots\capture.ps1"
)

$ErrorActionPreference = 'Stop'

$cathodeDir = Resolve-Path "$PSScriptRoot\.."
$cathodeExe = Join-Path $cathodeDir "CATHODE.EXE"
if (-not (Test-Path $cathodeExe)) {
    Write-Error "CATHODE.EXE not found at $cathodeExe. Run wmake in $cathodeDir first."
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
    Write-Host "Waiting $BootDelay s for start page to render"
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
