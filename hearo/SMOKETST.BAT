@ECHO OFF
REM HEARO smoke test for real iron.  Run from the directory that contains
REM HEARO.EXE, the test EXEs, and the data\ subdirectory.
REM
REM Output goes to SMOKETST.LOG. Each test runs for a few seconds, plays
REM through the active driver, then exits.  Audible output is the first
REM thing to verify; the LOG records what HEARO claims it did.

ECHO HEARO smoke test > SMOKETST.LOG
ECHO ================ >> SMOKETST.LOG
ECHO. >> SMOKETST.LOG

ECHO --- TESTDET (hardware detection) --- >> SMOKETST.LOG
TESTDET.EXE >> SMOKETST.LOG
ECHO. >> SMOKETST.LOG

ECHO --- TONE.WAV (PCM playback) --- >> SMOKETST.LOG
TESTPLAY.EXE /T3 data\TONE.WAV >> SMOKETST.LOG
ECHO. >> SMOKETST.LOG

ECHO --- TONE.MOD (ProTracker module) --- >> SMOKETST.LOG
TESTPLAY.EXE /T4 data\TONE.MOD >> SMOKETST.LOG
ECHO. >> SMOKETST.LOG

ECHO --- TONE.S3M (ScreamTracker 3) --- >> SMOKETST.LOG
TESTPLAY.EXE /T3 data\TONE.S3M >> SMOKETST.LOG
ECHO. >> SMOKETST.LOG

ECHO --- TONE.XM (FastTracker II) --- >> SMOKETST.LOG
TESTPLAY.EXE /T3 data\TONE.XM >> SMOKETST.LOG
ECHO. >> SMOKETST.LOG

ECHO --- TONE.MID (single-track MIDI via OPL FM) --- >> SMOKETST.LOG
TESTPLAY.EXE /T4 data\TONE.MID >> SMOKETST.LOG
ECHO. >> SMOKETST.LOG

ECHO --- BAND.MID (multi-instrument MIDI) --- >> SMOKETST.LOG
TESTPLAY.EXE /T5 data\BAND.MID >> SMOKETST.LOG
ECHO. >> SMOKETST.LOG

ECHO --- TONE.VGM (chip music via OPL) --- >> SMOKETST.LOG
TESTPLAY.EXE /T3 data\TONE.VGM >> SMOKETST.LOG
ECHO. >> SMOKETST.LOG

ECHO Smoke test complete. >> SMOKETST.LOG
ECHO ====================
ECHO Done. Results in SMOKETST.LOG
TYPE SMOKETST.LOG
