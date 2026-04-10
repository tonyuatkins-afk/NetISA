@echo off
REM Build NISATEST.COM from NASM source
REM Requires: NASM (https://nasm.us/) in PATH

nasm -f bin -o nisatest.com nisatest.asm
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)
echo Built: nisatest.com
dir nisatest.com
