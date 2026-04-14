@echo off
REM Example: Compile a C file and run in DOSBox-X
REM
REM This compiles a .C file with OpenWatcom wcc, links it, and runs
REM the resulting executable in DOSBox-X.
REM
REM Usage: build_c_example.bat path\to\source.c PROGRAM.EXE

if "%1"=="" (
    echo Usage: %~nx0 source.c OUTPUT.EXE
    exit /b 1
)
if "%2"=="" (
    echo Usage: %~nx0 source.c OUTPUT.EXE
    exit /b 1
)

cd /d "%~dp0..\.."
python devenv\dosbuild.py --cc %1 --link %2 --link-system "dos" --run %2 --timeout 15
