# Build a ZIP for transfer to a real-iron test box (e.g., Toshiba 320CDT).
# Includes EXEs, test data, smoke test batch, and README. Runs wmake first
# to ensure a clean build.
#
# Usage:  ./scripts/build-realiron-bundle.ps1 [-OutFile path.zip]

param(
    [string]$OutFile = "C:\Development\HEARO-realiron.zip"
)

$ErrorActionPreference = 'Stop'
$root = Resolve-Path "$PSScriptRoot\.."

# Ensure DOS-bound text files use CRLF line endings. COMMAND.COM silently
# fails on LF-only .BAT files (silent "Bad command or file name" — the file
# parses to nothing and DOS thinks it doesn't exist). TYPE handles either,
# but CRLF is the DOS convention. This is feedback_dos_batch_relay.md item 1.
function Convert-ToCrlf([string]$path) {
    if (-not (Test-Path $path)) { return }
    # [System.IO.File]::ReadAllBytes works on both Windows PowerShell 5 and
    # PowerShell 7+. Get-Content -Encoding Byte was removed in PS7 in favour
    # of -AsByteStream; using the .NET API sidesteps the version split.
    $content = [System.IO.File]::ReadAllBytes($path)
    $text = [System.Text.Encoding]::ASCII.GetString($content)
    $text = $text -replace "`r`n", "`n"        # normalize first
    $text = $text -replace "`n", "`r`n"        # then add CR
    [System.IO.File]::WriteAllText($path, $text, [System.Text.Encoding]::ASCII)
}

Push-Location $root
try {
    Write-Host "Building (wmake clean && wmake)..."
    cmd /c "call C:\WATCOM\owsetenv.bat && wmake clean && wmake" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "wmake failed (exit $LASTEXITCODE)" }

    Write-Host "Normalizing .BAT and .TXT to CRLF..."
    foreach ($f in (Get-ChildItem -Path . -Filter '*.BAT')) { Convert-ToCrlf $f.FullName }
    foreach ($f in (Get-ChildItem -Path . -Filter '*.TXT')) { Convert-ToCrlf $f.FullName }

    $files = @(
        'HEARO.exe', 'TESTDET.exe', 'TESTBOOT.exe', 'TESTPLAY.exe',
        'TESTUI.exe', 'TESTCORD.exe', 'TESTQUIR.exe', 'TESTBIP.exe',
        'SMOKETST.BAT', 'README-REALIRON.TXT', 'USBDOS.TXT', 'RUN320.TXT',
        'data\TONE.WAV', 'data\TONE.MOD', 'data\TONE.S3M',
        'data\TONE.XM',  'data\TONE.MID', 'data\BAND.MID', 'data\TONE.VGM'
    )

    foreach ($f in $files) {
        if (-not (Test-Path $f)) { throw "Missing required file: $f" }
    }

    if (Test-Path $OutFile) { Remove-Item $OutFile }
    Compress-Archive -Path $files -DestinationPath $OutFile -CompressionLevel Optimal

    $bytes = (Get-Item $OutFile).Length
    Write-Host ("Bundled {0} files into {1} ({2:N0} bytes)" -f $files.Count, $OutFile, $bytes)
    Write-Host ""
    Write-Host "Transfer to the test box (PCMCIA flash / Ethernet PCMCIA + FTP /"
    Write-Host "CD-R / floppy + PKUNZIP), unzip, and run SMOKETST.BAT from MS-DOS"
    Write-Host "mode for accurate audio results."
} finally {
    Pop-Location
}
