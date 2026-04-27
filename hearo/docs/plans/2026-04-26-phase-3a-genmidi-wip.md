# Phase 3.A: GENMIDI.OP2 — work-in-progress checkpoint

Date: 2026-04-26
Status: **integration complete, audible regression unresolved**

## What landed

- New files:
  - `src/decode/genmidi.h`
  - `src/decode/genmidi.c` — DMX OPL2 bank parser. 175-instrument format
    (8-byte `#OPL_II#` magic + 175×36 instrument records + 175×32 names),
    per-instrument unpacked into a 11-byte `genmidi_patch_t` that is
    layout-compatible with midifm's existing `fm_patch_t`.
  - `data/banks/GENMIDI.OP2` — DMXOPL bank from
    https://github.com/sneakernets/DMXOPL (MIT-licensed, Sound-Canvas-tuned).

- Modified:
  - `Makefile` — adds `genmidi.c` to `SRCS` and `LIB_OBJS` plus the
    explicit build rule. 16-bit and 32-bit targets both pick it up.
  - `src/decode/decode.c` — DECODE_MIDI case calls `midifm_load_bank()`
    after `midifm_init()`. Search order: CWD, `banks\GENMIDI.OP2`,
    `C:\HEARO\GENMIDI.OP2`. First hit wins; falls through silently if
    none are present (16-patch fallback bank is used).
  - `src/decode/midifm.c` — `midifm_load_bank()` now calls
    `genmidi_load()`. `patch_for()` consults `genmidi_lookup()` first,
    falls back to the 16-patch built-in. `note_on` runs the percussion
    note through `genmidi_fixed_note()` so OP2 fixed-pitch entries play
    at their authored OPL pitch instead of the MIDI note number.
    Also adds:
      - `adlib_set_opl3(opl3_present)` call so 18-voice mode works when
        SB (not AdLib) is the active driver.
      - SB16 mixer unmute (regs 22h/26h/30h/31h/34h/35h/3Eh = FFh) so FM
        output is audible by default after midifm_init.
  - `src/audio/adlib.{c,h}` — exports `adlib_set_opl3(hbool)` so callers
    can force the OPL3 flag on without going through the AdLib driver's
    `a_init` path.

## Build status

`wmake` builds clean (16-bit). 32-bit `wmake xm` should also build
(uses the same `$(SRCS)` list).

## Audible playback status

**HEARO + MIDI is silent in 86Box hearo-sb16.** Verified with both
`rock_odyssey.mid` (40 KB, 17-track Format 1) and the synthetic
`data/TONE.MID` (65 bytes). testplay traces show all the right
checkpoints — file loaded, audiodrv_auto_select picks SB16, mixer_init,
decode_start, drv->open returned ok, "Playing for N seconds..." — but
no audio reaches the host.

This regression is **not specific to GENMIDI**: even with the bank
absent (genmidi_loaded() returns false, fallback path active), MIDI
remains silent. It may be a pre-existing untested code path:
`midifm_*` was last touched in `4b3d2d9` (OPL3-SAx wake-layer commit)
and the actual end-to-end audible playback through the SB driver was
never validated in 86Box prior to this session. The VGM pipeline plays
correctly via the same OPL ports, so the issue is somewhere in:

- `decode_advance` -> `midi_advance` -> `dispatch_event` -> `midifm_note_on`
- ... or in the OPL stereo / volume init that midifm does at startup

## Diagnostic to do next session

1. Add traced-printf instrumentation to `midifm_note_on` to confirm
   notes are actually being dispatched at all. If they are, the OPL
   register sequence is wrong; if not, the sequencer is stalling.
2. Verify the `samples_per_tick_q16` value at `midi_play_init` for
   rock_odyssey (PPQN=960, default tempo 120 BPM, mixer 22050) — should
   be ~752793 (samples_per_tick ~= 11.48). If it's 0 or huge, ticks
   never advance.
3. Compare the OPL register write sequence midifm produces for a single
   note vs. what our session's `oplplay.com` produced for the same note
   (we know `oplplay.com` is audible through 86Box).
4. Once midifm is confirmed audible, validate GENMIDI.OP2 actually
   improves the sound (shouldn't just sound the same as the 16-patch
   bank — DMXOPL is Sound Canvas tuned, distinct per-instrument).

## Why it's worth shipping the WIP

Even if MIDI playback regression is fixed by the next session's first
`adlib_write` trace, the bank-loading scaffold is real:
- 175-patch DMX OP2 parser
- License-clean DMXOPL bank in `data/banks/`
- Auto-search at decode time
- Compatible byte layout with the existing `fm_patch_t`

When MIDI playback comes back online, the GENMIDI bank is already in
the pipeline.
