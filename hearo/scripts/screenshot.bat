@echo off
rem screenshot.bat - Convenience wrapper for screenshot.ps1.
rem Captures the HEARO boot screen as a PNG via DOSBox-X.
rem
rem Usage:
rem   screenshot.bat                   Captures with the default config.
rem   screenshot.bat min               Tags output as "_min" (use a 286/MDA conf).
rem   screenshot.bat max workstation   Tags as "_workstation", waits 10 s.

setlocal
set TAG=%1
set DELAY=%2
if "%DELAY%"=="" set DELAY=6

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0screenshot.ps1" -Tag "%TAG%" -BootDelay %DELAY%

endlocal
