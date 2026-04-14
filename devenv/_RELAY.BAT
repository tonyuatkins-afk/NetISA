@ECHO OFF
REM ============================================================
REM _RELAY.BAT - DOS-side relay for the DOSBox-X development env
REM
REM Called by autoexec in dev.conf. Runs _DOSCMD.BAT, captures
REM stdout to _RESULT.TXT, writes exit code to _RETCODE.TXT,
REM and signals completion via _DONE.TXT.
REM
REM All temp files use absolute paths (C:\) so that CD commands
REM in _DOSCMD.BAT don't break the relay.
REM
REM NOTE: Return code capture uses an IF ERRORLEVEL cascade with
REM 30 thresholds (1-10 exact, then steps of 5/10/25 up to 255).
REM This is a known design limitation of DOS batch files without
REM %%ERRORLEVEL%% support -- values between thresholds are
REM rounded down to the nearest lower threshold.
REM ============================================================

REM Guard: if no command file, exit immediately
IF NOT EXIST C:\_DOSCMD.BAT GOTO NOWORK

REM Clear any stale sentinel
IF EXIST C:\_DONE.TXT DEL C:\_DONE.TXT
IF EXIST C:\_RESULT.TXT DEL C:\_RESULT.TXT
IF EXIST C:\_RETCODE.TXT DEL C:\_RETCODE.TXT

REM Execute the command batch (output redirect is in _DOSCMD.BAT itself)
REM F-10: Reset ERRORLEVEL to known baseline (0) before executing user
REM commands. Without this, a prior command's ERRORLEVEL could leak into
REM our capture if _DOSCMD.BAT doesn't produce its own ERRORLEVEL (e.g.,
REM internal COMMAND.COM builtins don't always set it). VERIFY > NUL
REM always succeeds and sets ERRORLEVEL=0.
VERIFY > NUL
CALL C:\_DOSCMD.BAT

REM Capture errorlevel to _RETCODE.TXT
REM Use IF ERRORLEVEL cascade (checks >= N, so order matters: highest first)
REM Thresholds: 1-10, steps of 5 to 50, steps of 10 to 100, steps of 25 to 255
REM
REM ============================================================
REM LOAD-BEARING WHITESPACE WARNING:
REM The single space between each number and `>` in the lines
REM below (e.g. `ECHO 255 > C:\_RETCODE.TXT`) is INTENTIONAL and
REM load-bearing. DO NOT "clean up" by removing it.
REM
REM Why it's safe: the host-side relay reads _RETCODE.TXT with
REM .strip(), so any trailing whitespace appended by ECHO is
REM discarded before parsing the integer.
REM
REM Why removing the space is dangerous: `ECHO 1>C:\_RETCODE.TXT`
REM parses as "redirect stdout (fd 1) to C:\_RETCODE.TXT with an
REM empty ECHO" -- the number is consumed as a file descriptor,
REM not echoed, and the resulting file is empty. This silently
REM breaks return code capture for every run.
REM ============================================================
IF ERRORLEVEL 255 GOTO RC255
IF ERRORLEVEL 225 GOTO RC225
IF ERRORLEVEL 200 GOTO RC200
IF ERRORLEVEL 175 GOTO RC175
IF ERRORLEVEL 150 GOTO RC150
IF ERRORLEVEL 125 GOTO RC125
IF ERRORLEVEL 100 GOTO RC100
IF ERRORLEVEL 90 GOTO RC90
IF ERRORLEVEL 80 GOTO RC80
IF ERRORLEVEL 70 GOTO RC70
IF ERRORLEVEL 60 GOTO RC60
IF ERRORLEVEL 50 GOTO RC50
IF ERRORLEVEL 45 GOTO RC45
IF ERRORLEVEL 40 GOTO RC40
IF ERRORLEVEL 35 GOTO RC35
IF ERRORLEVEL 30 GOTO RC30
IF ERRORLEVEL 25 GOTO RC25
IF ERRORLEVEL 20 GOTO RC20
IF ERRORLEVEL 15 GOTO RC15
IF ERRORLEVEL 10 GOTO RC10
IF ERRORLEVEL 9 GOTO RC9
IF ERRORLEVEL 8 GOTO RC8
IF ERRORLEVEL 7 GOTO RC7
IF ERRORLEVEL 6 GOTO RC6
IF ERRORLEVEL 5 GOTO RC5
IF ERRORLEVEL 4 GOTO RC4
IF ERRORLEVEL 3 GOTO RC3
IF ERRORLEVEL 2 GOTO RC2
IF ERRORLEVEL 1 GOTO RC1
ECHO 0 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC255
ECHO 255 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC225
ECHO 225 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC200
ECHO 200 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC175
ECHO 175 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC150
ECHO 150 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC125
ECHO 125 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC100
ECHO 100 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC90
ECHO 90 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC80
ECHO 80 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC70
ECHO 70 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC60
ECHO 60 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC50
ECHO 50 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC45
ECHO 45 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC40
ECHO 40 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC35
ECHO 35 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC30
ECHO 30 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC25
ECHO 25 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC20
ECHO 20 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC15
ECHO 15 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC10
ECHO 10 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC9
ECHO 9 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC8
ECHO 8 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC7
ECHO 7 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC6
ECHO 6 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC5
ECHO 5 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC4
ECHO 4 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC3
ECHO 3 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC2
ECHO 2 > C:\_RETCODE.TXT
GOTO SIGNAL
:RC1
ECHO 1 > C:\_RETCODE.TXT
GOTO SIGNAL

:SIGNAL
REM Write sentinel to signal host that we're done
ECHO DONE > C:\_DONE.TXT
GOTO END

:NOWORK
ECHO NO_CMD > C:\_DONE.TXT

:END
