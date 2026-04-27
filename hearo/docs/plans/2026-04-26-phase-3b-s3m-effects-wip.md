# Phase 3.B: S3M effects (vibrato, fine slides, porta) — WIP

Date: 2026-04-26
Status: **code added, runtime "Stack Overflow!" during playback**

## What landed

- `src/decode/s3m.h`:
  - `s3m_song_t.chans` grew with `u32 freq, target_freq, base_freq` plus
    `u8 tone_speed` (~13 bytes per channel × 32 = ~400 bytes more).

- `src/decode/s3m.c`:
  - Added 64-entry quarter-period sine table (`sine_q[]`).
  - Added `apply_freq_delta()` helper — clamped 1/8192-units freq adjust.
  - `process_row` now sets `chans[ch].freq`, `base_freq`, `target_freq`,
    resets `vib_pos` on every fresh note trigger.
  - `process_row` now handles **D fine slides** (DxF / DFx on tick 0),
    **EFx/EEx fine porta down**, **FFx/FEx fine porta up**.
  - `process_tick` now implements:
      - Regular **D** volume slide (already existed; now skips fine variants
        which run on tick 0 instead).
      - Per-tick **E** porta down, **F** porta up.
      - **G** tone portamento — slides freq toward target_freq by tone_speed.
      - **H** vibrato — sine-modulated freq around base_freq.
      - **U** vibrato + volume slide — combined leg.
  - Effect parameter memory respected for E/F/G/H/U/D where the original
    S3M spec says "0 means continue last".

## Build status

Clean. `wmake` produces `TESTPLAY.EXE` (~119 KB).

Note: an earlier attempt added `option stack=16384` to the linker
invocation. That tripped the large-model DGROUP 64KB limit and was
reverted. Watcom DOS large-model `wlink` apparently places stack into
DGROUP when the option is set — surprising, but reproducible.

## Audible playback status

**TESTPLAY against `data/TONE.S3M` reaches "Playing for 8 seconds..."
and then panics with "Stack Overflow!" at runtime, exit code 1.** Same
result with or without the `option stack=16384` linker tweak (the linker
option turned out to be incompatible with large model anyway).

The added code per-channel adds modest stack (a few u32/s32 locals
per `case` plus one `apply_freq_delta` call), but the call chain from
the SB ISR (`sb.c`) -> `play_callback` -> `decode_advance` -> `s3m_advance`
-> `process_tick` -> `mixer_set_frequency` is already deep, and Watcom's
runtime stack-overflow check trips when SP goes below its
linker-recorded base. The default stack appears to be ~4 KB and we're
near or past it.

## Diagnostic next session

1. Inline `apply_freq_delta` into process_tick (remove one function frame).
2. Move the H/U `s32 sample`, `s32 mod`, `s32 next` locals to function
   scope or to the s3m_song_t struct so per-case pushes/pops don't accrue.
3. Check whether `mixer_set_frequency` itself does something stack-heavy
   (it's called many times per tick); inline or simplify.
4. Investigate Watcom's stack-overflow runtime-check disable path that
   doesn't blow DGROUP. The `-s` compiler flag (no stack checking) plus
   leaving `option stack=` off the linker may be the right combo if we
   trust we're not actually overflowing — testplay's pre-existing
   safety net (the per-2-second watchdog) catches real wedges.
5. Consider a per-thread / per-IRQ ISR-local stack swap (Watcom DOS
   pattern) — the SB ISR could install its own 4 KB stack on entry so
   the audio callback doesn't borrow the application stack.

## Why the WIP is worth keeping

The effect code itself looks right (vibrato sine table, fine-slide
fork, tone-porta target tracking, parameter memory). When the stack
problem is solved — likely by inlining one helper and disabling the
runtime check — these effects should activate without further edits.

The stack issue is also separate from the S3M effect work itself: it
arose because effect code added one more function frame to a call
chain that was already deep. Fixing it benefits the whole S3M player,
not just these new effects.
