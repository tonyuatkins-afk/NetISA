@echo off
REM Example: Assemble and run NISATEST.COM via the development relay
REM
REM This assembles phase0\dos\nisatest.asm natively with NASM,
REM then runs the resulting .COM in DOSBox-X.

cd /d "%~dp0..\.."
python devenv\dosbuild.py --asm phase0\dos\nisatest.asm --run NISATEST.COM --cwd \phase0\dos --timeout 15
