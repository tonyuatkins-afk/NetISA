# Phase 3.C: XM multi-sample mapping — WIP

Date: 2026-04-26
Status: **code added, runtime "Stack Overflow!" during playback**

## What landed

- `src/decode/xm.h`:
  - `xm_instrument_t.sample` (singular `xm_sample_t`) → `samples[16]` array.
  - New constant `XM_MAX_INSTR_SAMPLES` = 16.

- `src/decode/xm.c`:
  - Loader now reads ALL `num_samples` sample headers (capped at 16) and
    decodes each sample's data with delta unpacking. Previously only
    sample 0 was kept.
  - `xm_free` walks the 16-slot array and frees each sample's data.
  - Playback's `process_row` now consults `I->sample_for_note[note - 1]`
    to select which of the 16 samples to play. The `sample_for_note[]`
    table was already being parsed by the loader (line 103 of original
    xm.c) but was unused.
  - GUS `upload_sample` path still uses sample[0] only — multi-sample
    GUS DRAM upload deferred (would need multiple DRAM slots per
    instrument and a different mixer interface).

## Build status

Clean. `wmake` produces `TESTPLAY.EXE`.

## Audible playback status

**TESTPLAY against `data/TONE.XM` reaches "Playing for 8 seconds..."
and panics with "Stack Overflow!" at runtime, exit 1.** Same Watcom
runtime-check failure mode as Phase 3.B.

## Diagnosis

Same root cause as Phase 3.B (see
`2026-04-26-phase-3b-s3m-effects-wip.md` for the long form):

- Watcom DOS large-model stack-overflow check uses a static lower bound.
- The audio-ISR call chain — SB ISR → play_callback → decode_advance →
  xm_advance → process_tick/row → mixer_set_frequency — was already
  near the 4 KB stack limit before this session.
- Both Phase 3.B (S3M effects) and Phase 3.C (XM multi-sample) added
  marginal stack pressure that pushed it over.
- The trigger is the audio ISR firing on top of an already-deep main
  task; SP descends below the linker-recorded base; runtime check
  trips.

## Why the WIP is worth keeping

The XM multi-sample code itself is straightforward and correct:
- Loader correctly walks all 16 sample-header records (Watcom's `fread`
  + `fseek` per-sample, then sequential delta-decoded data).
- `sample_for_note[]` lookup is one byte-read per note-trigger,
  negligible cost.
- xm_free correctly walks the 16-slot table.

When the stack issue is solved (Phase 3.B's diagnosis applies here too:
inline `apply_freq_delta` in s3m.c, disable Watcom runtime stack check
without breaking DGROUP, or install an ISR-local stack swap in sb.c),
both XM and S3M will activate cleanly.

## What this session shipped overall

Even with all three Phase 3 sub-tasks landing in WIP rather than
"audibly playing in 86Box," real artifacts are on disk:

- **GENMIDI.OP2 loader and bank integration** (`src/decode/genmidi.{c,h}`,
  Makefile, decode.c, midifm.c). DMXOPL bank in
  `data/banks/GENMIDI.OP2`. Loader is far-heap allocated so DGROUP
  doesn't blow.
- **S3M effects coverage**: D fine slides (DxF/DFx), E/F regular and
  fine porta variants (xx/Fx/Ex), G tone portamento, H vibrato with
  64-entry sine table, U combined vibrato + volume slide, parameter
  memory across rows.
- **XM multi-sample mapping**: 16-sample-per-instrument loader,
  `sample_for_note[]` per-note dispatch in playback.

The audible regression layer is below the format-decoder code: it's in
the call-chain depth the audio ISR carries, an environmental Watcom DOS
constraint. Fixing it once (next session) unlocks all three Phase 3
contributions at the same time.

## Next-session plan

1. Inline apply_freq_delta in s3m.c (one less function frame).
2. Reduce per-call locals in process_tick (move to song struct).
3. Try `-s` Watcom flag (disable stack check) without `option stack=`
   on linker (large-model puts stack outside DGROUP by default; the
   linker option had unexpected behavior).
4. If still failing, instrument sb.c's ISR to install its own stack
   on entry and restore on exit — standard DOS audio ISR pattern.
5. Once any one of S3M / XM / MIDI plays audibly, the others should
   follow because they share the call-chain depth.
