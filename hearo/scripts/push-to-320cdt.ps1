# Push the real-iron bundle to a Toshiba 320CDT (or any DOS/Win9x box) via FTP.
#
# Usage:
#   ./scripts/push-to-320cdt.ps1                              # default: 10.69.69.102, anonymous
#   ./scripts/push-to-320cdt.ps1 -Host 10.69.69.102 -User tony -Pass netisa
#   ./scripts/push-to-320cdt.ps1 -Loose                       # transfer files individually instead of the zip
#
# The script writes a temporary ftp.exe script and invokes the Windows
# ftp.exe client. Default account matches the NetISA 486 workflow
# (tony / netisa); override if your 320CDT FTP server uses a different one.

param(
    [string]$HostName = '10.69.69.102',
    [string]$User     = 'tony',
    [string]$Pass     = 'netisa',
    [string]$RemoteDir = '/',
    [string]$Bundle   = 'C:\Development\HEARO-realiron.zip',
    [switch]$Loose
)

$ErrorActionPreference = 'Stop'
$root = Resolve-Path "$PSScriptRoot\.."

$tmp = New-TemporaryFile
$lines = @(
    "open $HostName",
    "user $User $Pass",
    "binary",
    "prompt off"
)
if ($RemoteDir -and $RemoteDir -ne '/') {
    $lines += "cd $RemoteDir"
}

if ($Loose) {
    Push-Location $root
    try {
        $files = @(
            'HEARO.exe', 'TESTDET.exe', 'TESTBOOT.exe', 'TESTPLAY.exe',
            'TESTUI.exe', 'TESTCORD.exe', 'TESTQUIR.exe', 'TESTBIP.exe',
            'SMOKETST.BAT', 'README-REALIRON.TXT'
        )
        foreach ($f in $files) {
            $lines += "put `"$f`""
        }
        $lines += "mkdir data"
        $lines += "cd data"
        $dataFiles = @('TONE.WAV', 'TONE.MOD', 'TONE.S3M', 'TONE.XM', 'TONE.MID', 'BAND.MID', 'TONE.VGM')
        foreach ($f in $dataFiles) {
            $lines += "put `"data\$f`" $f"
        }
    } finally {
        Pop-Location
    }
} else {
    if (-not (Test-Path $Bundle)) {
        Write-Error "Bundle not found at $Bundle. Run build-realiron-bundle.ps1 first."
        exit 1
    }
    $lines += "put `"$Bundle`" HEARO.ZIP"
}
$lines += "bye"

Set-Content -Path $tmp.FullName -Value $lines -Encoding ASCII

Write-Host "Pushing to $HostName as $User"
ftp -s:$tmp.FullName

Remove-Item $tmp.FullName -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "On the 320CDT, run:"
Write-Host "  C: && CD \ && PKUNZIP HEARO.ZIP -d   (or whatever unzip you have)"
Write-Host "  CD HEARO && SMOKETST"
