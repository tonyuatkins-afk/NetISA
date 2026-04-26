HEARO real-iron validation kit
==============================

Bundle contents
---------------

  HEARO.EXE     main player + UI
  TESTDET.EXE   hardware detection report
  TESTBOOT.EXE  boot screen test
  TESTPLAY.EXE  command-line player (used by SMOKETST.BAT)
  TESTUI.EXE    UI exercise test
  TESTCORD.EXE  CORDIC math test
  TESTQUIR.EXE  quire accumulator test
  TESTBIP.EXE   bipartite tables test
  SMOKETST.BAT  one-button smoke test that exercises every audio path
  data\TONE.WAV 22050 Hz 16-bit mono sine, 2 seconds
  data\TONE.MOD 4-channel ProTracker, plays C-2 every 16 rows
  data\TONE.S3M ScreamTracker 3, single C-5 note via channel 0
  data\TONE.XM  FastTracker II, single C-5 note via channel 0
  data\TONE.MID 4-note arpeggio (C-E-G-C5), single channel
  data\BAND.MID multi-channel (piano + bass + brass + percussion)
  data\TONE.VGM minimal OPL2 chip-music, single A4 note

Running on Windows 98 SE
------------------------

For accurate audio results, boot to MS-DOS mode rather than running
inside a Windows DOS box:

  Start menu -> Shut Down -> Restart in MS-DOS mode

The Windows DOS box virtualises IRQs and the timer chip; HEARO's IRQ
handlers and PIT reprogramming will mostly work but timing will be off
and the SB ISR may be intercepted by Windows. MS-DOS mode is the
real thing.

If "Restart in MS-DOS mode" is missing, create a DOS boot floppy or set
BootGUI=0 in C:\MSDOS.SYS to get true real mode at boot.

Quick smoke test
----------------

  C:
  CD HEARO
  SMOKETST.BAT

This runs TESTDET then plays each format for 3-5 seconds. Listen for
audio on each. Results are appended to SMOKETST.LOG.

Expected on Toshiba 320CDT (Yamaha OPL3-SAx, YMF715):

  TESTDET should report:
    - PC Speaker
    - AdLib (OPL3)
    - Sound Blaster Pro 2 (DSP 3.x) at base 220 IRQ 5/7 DMA 1
    - Roland MPU-401 UART at 330h
    - General MIDI device

  TESTPLAY data\TONE.WAV: 440 Hz tone, 3 seconds, clean stop
  TESTPLAY data\TONE.MOD: low note repeating every ~250 ms
  TESTPLAY data\TONE.S3M: single C-5 tone for 2 seconds
  TESTPLAY data\TONE.XM: single C-5 tone for 2 seconds
  TESTPLAY data\TONE.MID: ascending arpeggio C-E-G-C5 via OPL3 FM
  TESTPLAY data\BAND.MID: piano + bass + brass + drums via OPL3 FM
  TESTPLAY data\TONE.VGM: A-4 note for 1 second via OPL2

Interactive use
---------------

  HEARO.EXE              full UI: boot screen, four-pane player, hall
  HEARO.EXE /UNLOCKS     dump unlock matrix (no UI)
  HEARO.EXE /HALL        dump hall of recognition log
  HEARO.EXE /VERSION     version and copyright
  TESTPLAY.EXE [/N] [/Tn] filename
                         /N skip detection (use SB16 defaults)
                         /Tn auto-exit after n seconds (headless)
                         ESC quit, SPACE pause, +/- volume

Reporting back
--------------

  SMOKETST.LOG           contents of every test in order
  HEARO.HAL              persistent hall log (delete to reset)
  HEARO.CFG              persistent config

If anything misbehaves, capture SMOKETST.LOG plus a description of
what was audible (or not) and we can correlate.

Known limitations as of this build
----------------------------------

  - GUS hardware-mix is wired but only triggers when a GUS is detected
    alongside SB. The SB ISR provides timing; GUS plays voices in HW.
    On the 320CDT (no GUS) the software mixer is used unconditionally.
  - IT format identifies but does not play
  - MIDI uses 16 hand-tuned FM patches, not GENMIDI.OP2
  - XM single-sample-per-instrument (multi-sample mapping deferred)
  - S3M effect set is basic (no vibrato / fine slides yet)
  - OPTi 82C929 has a detection probe but no native driver init; if your
    box has a 82C929 in dormant mode, run 929INIT first to bring it up
    in SB Pro compat mode
