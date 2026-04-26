# HEARO Playback Wiring Design

**Status:** design pass, no code yet. Awaiting approval before implementation.
**Companion documents:** `hearo-design.md`, `hearo-soundcard-reference.md`
**Audience:** future-self, code reviewers
**Scope:** wire HEARO.EXE's UI to the audio engine + decoders so KEY_ENTER on a music file actually plays it, and `nowplay` + `spectrum` reflect real playback state.

## Current state

The audio engine (Phase 2) and decoder subsystem (Phase 2/3) are
complete and working in isolation. `TESTPLAY.EXE` exercises them
end-to-end. `HEARO.EXE` is a UI shell: every pane draws something,
keys move focus, but `KEY_ENTER` on a `.MOD` file in the browser does
nothing, `nowplay` shows "Axel F by Harold Faltermeyer" hardcoded
placeholders, and `spectrum` runs a sine-sweep simulation because
nobody ever calls `spectrum_feed()`.

The wiring missing is the integration layer between three things that
already exist:

- `audio_driver_t` vtable (`src/audio/audiodrv.h`) and the SB / GUS /
  AdLib / etc. drivers that implement it
- `decode_handle_t` and the `decode_load / start / advance / free`
  API (`src/decode/decode.h`)
- the four-pane UI (`src/ui/ui.c`, `browser.c`, `nowplay.c`,
  `spectrum.c`)

`TESTPLAY.EXE` already shows how to wire these together correctly. The
design here is to extract the playback orchestration from `testplay.c`
into a reusable module that both `TESTPLAY` and `HEARO.EXE` can drive.

## Module: `src/playback/playback.c`

New module with one job: own the active playback session. State,
lifecycle, ISR coupling, and the bridge to the UI panes.

### State (file-scoped)

```c
static decode_handle_t       *song;             /* heap, large; one per session */
static const audio_driver_t  *drv;              /* cached pointer to active driver */
static u32                    mixer_rate;       /* the rate drv was opened at */
static u8                     mixer_format;     /* AFMT_* */
static char                   filename[80];     /* current file, for nowplay display */
static char                   title[40];        /* extracted from decoder metadata */

static volatile u32           frames_rendered;  /* incremented from ISR */
static volatile u8            paused;           /* main loop sets, ISR reads */
static u8                     master_vol;       /* 0..250, mirrors mixer state */
static u8                     state;            /* PB_STOPPED / PB_PLAYING / PB_PAUSED */

static volatile decode_handle_t *next_song;     /* track-change handoff (see below) */
```

Volatile is load-bearing on the variables shared with the ISR. Watcom
otherwise hoists reads out of loops.

### Public API

```c
hbool playback_init(const hw_profile_t *hw);
void  playback_shutdown(void);

hbool playback_load(const char *path);
void  playback_stop(void);
void  playback_pause(hbool on);

void  playback_set_volume(u8 v);     /* 0..250 */
u8    playback_get_volume(void);

u8    playback_state(void);          /* PB_STOPPED / PB_PLAYING / PB_PAUSED */
u32   playback_position_ms(void);    /* derived from frames_rendered / rate */
u32   playback_duration_ms(void);    /* from decoder metadata */
const char *playback_title(void);
const char *playback_filename(void);

/* Tick from the main loop. Pushes frames_rendered -> nowplay,
 * recent samples -> spectrum. Must be called at >=10 Hz to keep
 * the spectrum responsive but not so fast it eats CPU. */
void  playback_ui_tick(void);
```

### Lifecycle

`playback_init()` replaces the current `audiodrv_register_all() /
audiodrv_auto_select() / mixer_init()` block in `hearo.c:main()`.
Caches `drv` for later. Allocates the `song` handle (heap, ~6 KB
union of all decoder state). Initializes `state = PB_STOPPED`.

`playback_load(path)`:

  1. If `state != PB_STOPPED`, call `playback_stop()` first.
  2. `decode_load(path, song)`. On failure, return HFALSE.
  3. `decode_start(song, mixer_rate)`.
  4. `decode_advance(song, 1)` to prime first frame (otherwise the
     very first ISR sees an idle mixer).
  5. Copy `path` to `filename`, decoder title to `title`.
  6. Reset `frames_rendered = 0`, `paused = 0`.
  7. `drv->open(mixer_rate, mixer_format, playback_callback)`. On
     failure, decode_free + return HFALSE.
  8. `state = PB_PLAYING`.
  9. Push title / filename / duration to `nowplay_set_track()`.
  10. Return HTRUE.

`playback_stop()`:

  1. If `state == PB_STOPPED`, no-op.
  2. `drv->close()`. This stops the ISR before we touch shared state.
  3. `decode_free(song)`.
  4. `state = PB_STOPPED`.
  5. `nowplay_set_state(NP_STOPPED)`.

`playback_pause(on)`:

  - Set `paused = on`. Atomic on x86 (single byte write).
  - `nowplay_set_state(on ? NP_PAUSED : NP_PLAYING)`.

`playback_shutdown()`:

  - Calls `playback_stop()` if needed.
  - Then `drv->shutdown()` to release the SB ISR vector / DMA buffers.

### The audio callback

```c
static void playback_callback(void *buffer, u16 samples, u8 format)
{
    if (paused) {
        memset(buffer, format >= AFMT_S16_MONO ? 0 : 0x80,
               (u32)samples * AFMT_FRAME_BYTES(format));
        return;
    }
    decode_advance(song, samples);
    mixer_render(buffer, samples, format);
    frames_rendered += samples;
}
```

Identical in shape to `testplay.c`'s `play_callback`. Runs from the SB
ISR on each half-buffer flip (auto-init) or each block (single-cycle).
Reads `paused` and `song`; writes `buffer` and `frames_rendered`.

### Track-change while playing (deferred)

The ideal path is for `playback_load()` to swap songs without closing
the driver. Sketch: store the prepared new handle in `next_song`; the
ISR callback checks `next_song` at entry, if non-null swaps `song =
next_song; next_song = 0;` (single 16-bit pointer write, atomic on
x86 in DGROUP).

Defer this to a later commit. Initial implementation is the simpler
"close + open" sequence in `playback_load`. The brief audio gap
during track change is acceptable for v1.

## UI integration

### `browser.c`: handle KEY_ENTER on files

```c
case KEY_ENTER:
    if (selected < entry_count && entries[selected].is_dir) {
        if (change_directory(entries[selected].name) == 0) {
            load_directory();
        }
        return HTRUE;
    }
    if (selected < entry_count && is_music_file(entries[selected].name)) {
        char full_path[128];
        snprintf(full_path, sizeof(full_path), "%s\\%s",
                 current_dir, entries[selected].name);
        playback_load(full_path);
        return HTRUE;
    }
    return HFALSE;
```

`browser.c` includes `playback.h`. `playback_load()` failures are
silent here; the user sees no state change. Future: surface the error
via `whisper_*` (the existing toast mechanism).

### `ui.c`: SPACE pauses the engine, not just the display

Replace:

```c
if (key == KEY_SPACE) {
    nowplay_set_state(nowplay_state() == NP_PLAYING ? NP_PAUSED : NP_PLAYING);
    need_redraw = HTRUE; continue;
}
```

with:

```c
if (key == KEY_SPACE) {
    playback_pause(playback_state() == PB_PLAYING ? HTRUE : HFALSE);
    need_redraw = HTRUE; continue;
}
```

Same for the menu's `MA_PLAY_PAUSE` / `MA_STOP` actions.

### `ui.c`: tick the playback engine each main loop iteration

The existing main loop already has a 100 ms timer for spectrum
animation:

```c
if (now - last_spectrum_step_ms > 100UL) {
    spectrum_step();
    spectrum_render(...);
    last_spectrum_step_ms = now;
}
```

Add `playback_ui_tick()` to that branch. Cost: one mixer_get_samples
copy + one nowplay metadata write + one spectrum_feed call. All
main-loop, no ISR contention.

### `playback_ui_tick()` internals

```c
void playback_ui_tick(void)
{
    if (state == PB_STOPPED) return;
    /* Position: derive from frames_rendered. Volatile read; tearing
     * a u32 across 16-bit halves is OK here since we're only updating
     * a display value at 10 Hz, and the worst case is one frame of
     * displayed lag. */
    u32 pos_ms = (frames_rendered * 1000UL) / mixer_rate;
    nowplay_advance_to(pos_ms / 1000UL);

    /* Spectrum: snapshot the most recently rendered samples from the
     * mixer's ring buffer. Need to add a mixer_peek_samples() helper
     * (see "Mixer changes" below). */
    s16 snap[256];
    u16 n = mixer_peek_samples(snap, 256);
    if (n) spectrum_feed(snap, n);
}
```

The `nowplay_advance_to(seconds)` call needs adding (currently only
`nowplay_advance(delta)` exists). Trivial new function on
`nowplay.c`.

## Mixer changes

`mixer_render()` currently writes its output to the buffer the driver
provides and that's it. For `spectrum_feed()` to see real audio, the
mixer needs to also keep the most recent N samples in a ring buffer
that `playback_ui_tick()` can peek into.

Add to `mixer.c`:

```c
#define PEEK_RING 256
static s16 peek_ring[PEEK_RING];
static u16 peek_head;

void mixer_render(...) {
    /* existing code, then: */
    /* Capture last `samples` worth of mono mixdown into peek_ring */
    /* (one sample per frame, mono mixdown of left/right; cheap) */
}

u16 mixer_peek_samples(s16 *out, u16 max) {
    u16 n = max < PEEK_RING ? max : PEEK_RING;
    /* Copy from peek_head wrapping back, in chronological order. */
    /* ... */
    return n;
}
```

Cost: 256 * 2 bytes = 512 bytes DGROUP + 256 cycles per render of
mono mixdown. Acceptable on 286+.

Caveat: `mixer_render` runs from the ISR. `mixer_peek_samples` runs
from the main loop. Reads of `peek_head` from main loop need to be
atomic (it's u16, atomic on x86). Tearing of the ring contents is
acceptable because we're feeding a visualizer, not reproducing audio.

## Cleanup paths

`hearo.c:main()` end-of-`ui_run` block becomes:

```c
playback_shutdown();    /* replaces the current audiodrv shutdown call */
return 0;
```

The Ctrl-Break handler in `hearo.c` (already exists for HEARO; copy
the pattern from `testplay.c` if not) needs to call
`playback_shutdown()` so an abnormal exit always uninstalls the SB
ISR. Not optional: without this, Ctrl-C during playback wedges the
machine on the next IRQ.

## Watchdog

`testplay.c` has a 2-second no-progress watchdog; if `frames_rendered`
does not advance, exit cleanly with a diagnostic message. For
`HEARO.EXE`, the equivalent shouldn't exit but should:

  - Set `state = PB_STOPPED`
  - Call `drv->close()` to release the ISR
  - Push a whisper toast: "Audio ISR not firing, see DOSMODE.TXT"

This way the UI stays alive, the user can see what happened, and the
machine isn't wedged. Wire from `playback_ui_tick()`: track
`last_progress_ms`; if `now - last_progress_ms > 2000` and `state ==
PB_PLAYING`, fire the watchdog.

## Files touched

  New:
    src/playback/playback.h
    src/playback/playback.c

  Modified:
    src/audio/mixer.c           (add peek ring + mixer_peek_samples)
    src/audio/mixer.h           (declare mixer_peek_samples)
    src/ui/browser.c            (KEY_ENTER on music files calls
                                 playback_load)
    src/ui/nowplay.c            (add nowplay_advance_to)
    src/ui/nowplay.h            (declare nowplay_advance_to)
    src/ui/ui.c                 (SPACE -> playback_pause; main loop
                                 calls playback_ui_tick)
    src/hearo.c                 (init: playback_init replaces direct
                                 audiodrv calls; cleanup: playback_
                                 shutdown; ctrl-break handler)
    Makefile                    (add playback.obj)
    test/testplay.c             (refactor onto playback API; the
                                 standalone player becomes a thin
                                 wrapper around playback_load + main
                                 loop polling. Same behaviour, less
                                 duplication.)

  Untouched: every audio driver, every decoder, every other UI pane
  except those listed above.

## Risks

1. **ISR re-entrancy.** `decode_advance()` runs from the ISR.
   `playback_ui_tick()` runs from main and reads decoder metadata
   (title, duration). If any decoder writes to that metadata in
   `advance()`, we have a race. Audit before implementation: title /
   duration are decoder-init-time values, never updated mid-track.
   Confirm and pin the invariant in `decode.h`.

2. **`song` handle pointer write hazard.** `playback_load()` sets
   `song = ...` before `drv->open()`. If a stale ISR fires between
   those, it will see the new song handle but the mixer is still
   set up for the old one. Mitigation: order matters. drv->close()
   happens before any state mutation in `playback_stop()`, and
   drv->open() happens last in `playback_load()` after song / mixer
   are fully set up.

3. **DGROUP pressure.** Adding a 512-byte peek ring + the `song`
   pointer (4 bytes far) + filename / title strings (120 bytes)
   means ~640 bytes of new DGROUP. Watcom large model; should fit
   but worth checking. Memory entry `feedback_dos_conventions.md`
   item 1 covers the DGROUP budget.

4. **Spectrum visual quality.** The peek ring is post-mix, so the
   spectrum reads what's actually being sent to the SB. Good for
   accuracy. But it's mono mixdown, so multi-channel module spectra
   look less interesting than they would with per-channel meters.
   Acceptable for v1; per-channel meters can come later if asked.

5. **Browser path concatenation.** `snprintf("%s\\%s", current_dir,
   name)` will produce `C:\\\foo.mod` if current_dir is `C:\`.
   Browser code will need a "trailing slash check" (or pull existing
   logic from the directory walker which already handles this).

## Out of scope

  - Playlist functionality. The `Playlist` pane currently shows
    hardcoded placeholders; wiring it to a real playlist is a
    separate feature.
  - Track change without audio gap (the `next_song` handoff above).
  - Prev / Next track buttons. Need playlist first.
  - File metadata caching (decoder ID3 / module title scanning during
    browser load). Big enough for its own design pass.
  - Streaming, NetISA, MP3, etc. (Phase 4+).

## Open questions for the user

1. **Watchdog UI behavior.** Above I proposed: stop playback, show a
   whisper toast, leave UI running. Alternative: keep the playback
   engine in PB_PLAYING state but with a warning indicator so the
   user knows the audio is stuck and can decide whether to stop.
   Which?

2. **`testplay.c` refactor.** The clean approach is to refactor
   `testplay.c` to use the `playback_*` API (deduplicates code, makes
   the standalone player a reference implementation of the API).
   The careful approach is to leave `testplay.c` standalone for now
   so we don't disrupt the working real-iron diagnostic tool while
   the UI integration is in flux. Which?

3. **Peek ring sample type.** Mono s16 mixdown is what I sketched.
   The mixer internally has stereo / 16-bit / 8-bit variants. We
   could expose what the mixer renders raw and let `spectrum_feed`
   downsample. Adds complexity for marginal gain. Default to mono
   s16 unless you want otherwise.

4. **Phase split.** This is one logical change but ~8 files. Land
   as a single commit for atomicity, or split into "add playback
   module + tests" then "wire UI to it" then "refactor testplay"?
   Three-commit version is more bisect-friendly.

If these are decided, I'll implement.
