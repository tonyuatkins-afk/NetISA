# HEARO session applicable lessons — Cathode

Drafted 2026-04-26 after the HEARO session. Cathode is a DOS web
browser (v0.2 "sort of", 12/12 fixture tests passing, 40 KB EXE) at
`cathode/`. This doc focuses on what HEARO's defensive-DOS work means
for Cathode specifically. Sister doc:
`2026-04-26-netisa-session-lessons.md` (repo-wide).

The HEARO commits referenced are on `origin/main`, range
`156695c..HEAD`. File paths are absolute under
`C:\Development\NetISA\`.

## What Cathode already does right

A quick code-walk before recommending changes:

- Uses `scr_kbhit` / `scr_getkey` from the shared NetISA `lib/screen.c`
  (INT 16h AH=01h / AH=00h direct via inline asm to capture ZF). No
  libc kbhit/getch path. ✓
- Yields to DOS via `int 28h` when idle (lets TSRs run during browser
  loop). ✓
- Has explicit `browser_shutdown` + `scr_shutdown` on normal exit
  (`cathode/src/main.c:78-80`). ✓
- 12/12 fixture tests passing (`runtests.exe`). ✓
- Uses INT 16h directly via shared lib, not in main per-app. ✓

## What Cathode is missing (HEARO Phase 4 patterns)

### DOS-1: atexit + SIGINT/SIGBREAK + `_harderr` cleanup handlers

HEARO `hearo/src/hearo.c:38-80`. Cathode currently has zero signal
handlers. Ctrl-C during browse:

- exits via libc default SIGINT handler (immediate exit, no cleanup)
- skips `browser_shutdown` (per-page state leaks)
- skips `scr_shutdown` (text mode left in non-default attr / cursor
  state)
- skips any NetISA card state restoration that browser_shutdown does

Ctrl-Break has the same shape via SIGBREAK. INT 24h critical errors
(disk not ready when reading bookmarks, etc.) draw "Abort, Retry,
Fail" over Cathode's text-mode UI; the user sees a corrupted screen
and types Fail blind.

**Fix:** port the HEARO pattern. Idempotent `cathode_shutdown` helper
registered via `atexit()`, called from `signal(SIGINT, ...)` /
`signal(SIGBREAK, ...)`, and `_harderr(handler)` returning
`_HARDERR_FAIL`.

The shutdown helper does:
1. `browser_shutdown(&b)` (idempotent — guard with a flag)
2. `scr_shutdown()`
3. `dos_mcb_validate()` (when DOS-2 lands; see below)

Watcom-specific: `_DOSFAR` macro is `#undef`-ed at end of `<dos.h>`;
use `__far` directly in handler parameter declarations.

Cost: ~60 lines in `cathode/src/main.c`. One commit. Smoketestable
in 86Box: AUTORUN.BAT runs CATHODE.EXE, then... well, you can't
synthesize Ctrl-C from outside, so the smoketest only confirms the
handlers install cleanly. Manual interactive verification confirms
the actual Ctrl-C path.

### DOS-2: DOS MCB chain validation on exit

HEARO `hearo/src/platform/dos.c:dos_mcb_validate`. Cathode allocates
per-page buffers and frees on navigate. If any path leaks (page
allocated but never freed, fetch interrupted mid-allocation, etc.),
the MCB chain may be corrupt at exit.

Without validation: COMMAND.COM panics with "Memory allocation
error" later, user blames DOS.

With validation: Cathode prints "WARNING: DOS MCB chain corrupt at
exit — please report this with the URL last visited" and exits
cleanly. The diagnostic gives the operator the right culprit.

**Recommended path:** promote `dos_mcb_validate` from HEARO to shared
`lib/dos.c` (per `2026-04-26-netisa-session-lessons.md` DOS-2), then
Cathode calls it from the new `cathode_shutdown` helper above.

### DOS-3: INT 16h discipline (verify shared lib usage, no local divergence)

Cathode already uses the shared `lib/screen.c:scr_kbhit` /
`scr_getkey`. ✓ No fix needed. Worth a periodic audit during code
review — any new file that adds `kbhit()` / `getch()` from libc is a
regression.

## What Cathode might gain from HEARO patterns (selective)

### REC-1: Fetch-cancel via INT 16h polling — already there, audit only

Cathode `cathode/src/fetch.c:187, 303, 339` polls `scr_kbhit` during
fetch operations to allow Ctrl-C cancel. This is the right pattern —
analogous to HEARO's audio ISR keeping a callback responsive while
the foreground UI runs. No fix; verify this doesn't regress in v0.3.

### REC-2: Stack overflow defense in HTML parsing

HEARO's chunk-A audio core compiles ISR-reachable code with
`#pragma off (check_stack)` because the runtime stack-overflow probe
calls into INT 21h (DOS not reentrant from ISR). Cathode is
foreground-only so this doesn't apply directly.

But: Cathode's HTML parser (`htmltok.c`) processes tag nesting. A
deeply-nested HTML input (1000+ levels of `<div><div><div>...`) could
blow Cathode's stack. The fix is not the same as HEARO's; it's a
recursion-depth guard in the parser itself.

**Audit only.** If `htmltok.c` is iterative (state machine), this is
a non-issue. If it recurses, add a depth counter and fail-fast at
e.g. 64 levels (deeper than any real-world HTML).

This is a Cathode-specific concern that HEARO surfaces only by
contrast — HEARO doesn't have nested input at all.

### REC-3: Eliminate the duplicate `scr_init()` call

`cathode/src/main.c:35` and `cathode/src/main.c:38` both call
`scr_init()`. Looks like an editing leftover. Doesn't cause harm
(scr_init is presumably idempotent or it'd have been caught by
the v0.2 quality gate) but is dead code. Trivial cleanup.

## Test infrastructure

### TEST-1: 86Box VM matrix usage for Cathode

The three FreeDOS 1.4 VMs at `C:\Tools\86Box\vms\` (hearo-sb16,
hearo-ymf715, hearo-ess) have no NetISA card emulation, so live fetch
tests are not runnable there. But:

- HTML parser fixture tests (the existing 12/12 in `runtests.exe`)
  ARE testable
- about: stub pages render-test ARE testable
- Browser navigation, search, bookmarks (in-memory state) ARE
  testable
- INT 16h interaction (key-by-key navigation through a synthetic
  page) IS testable

Create a probe payload at `C:\Tools\86Box\vms\_assets\cathode-test\`:

- `CATHODE.EXE` (current build)
- `RUNTESTS.EXE` (current build)
- Synthetic `about:home` content baked in
- `AUTORUN.BAT` invoking `RUNTESTS.EXE > D:\TEST.OUT`,
  cat to `D:\PROBE.LOG`, `STATUS:OK if RC=0`, `FDAPM POWEROFF`

Catches a class of fixture-pass-on-DOSBox-X-fails-on-FreeDOS-486
regression that the existing single-platform test misses.

### TEST-2: Multi-platform smoketest matrix

For Cathode v0.3 (or whenever the next milestone lands), define the
explicit smoketest matrix:

- DOSBox-X 386 / 486 cycle settings (catches CPU-class branches)
- 86Box hearo-sb16 / hearo-ymf715 / hearo-ess (catches anything
  that is sensitive to FreeDOS specifics; not card-specific for
  Cathode but uses FreeDOS instead of MS-DOS)
- Real iron 486 DX-2-66 + Vibra 16S + actual NetISA card (only path
  that exercises live fetch)

Local smoketest is the first two; per-release-gate is real iron.

## Process discipline

### PROC-1: Iterative code review at each Cathode milestone

Per `2026-04-26-netisa-session-lessons.md` PROC-3. Apply at each
Cathode release gate. v0.3 should run through three rounds of fresh
reviewer per the HEARO pattern.

Cathode already had a quality-gate pass for v0.2 (12/12 fixtures, 3
parser bugs fixed). The next gate should formalize the iterative
multi-round protocol.

### PROC-2: Read-before-write discipline on htmltok.c, htmlout.c

These are Cathode's chunk-A-equivalent: gated by 12 fixture tests,
3 parser bugs fixed against real-world content. Touching either
file in v0.3+ work should surface as a question first rather than
landing silently.

## Sequencing recommendation

If Cathode v0.3 is opened tomorrow, the leverage-vs-risk sort:

**Quickest wins (one commit each):**
- DOS-1 atexit + signal handlers + `_harderr` (~60 lines, drop-in
  from HEARO pattern, ~1 hour)
- DOS-2 MCB validate at exit (~50 lines, depends on shared `lib/dos.c`
  promotion landing first)
- REC-3 remove duplicate `scr_init()` (one line)

**Audit-only (no commits unless issue found):**
- DOS-3 verify no libc kbhit/getch in any new files
- REC-1 verify fetch.c polling still allows Ctrl-C cancel
- REC-2 verify htmltok.c is not recursive (or add depth guard if it
  is)

**Test infrastructure (one task, parallel to code work):**
- TEST-1 cathode-test probe payload for 86Box VMs

A focused single-session pass on DOS-1, DOS-2, REC-3, and TEST-1
yields three commits plus one infrastructure task, all
smoketestable, none touching htmltok.c / htmlout.c. Mirrors HEARO's
Phase 4 cleanup-hardening pass: harden the exit path before adding
new feature surface.

## What does not carry over from HEARO

- **Wake layer architecture.** HEARO's vendor pre-init pattern is
  audio-specific. Cathode talks to one device (NetISA card) which
  the shared lib already manages.
- **Frequency-scaled DMA buffer.** No audio in Cathode.
- **DSP write timeout (HEARO sb.c).** No DSP. The `fetch.c` polling
  is structurally similar but already correctly bounded via the
  Ctrl-C check.
- **OPL3-SAx variant detection.** No audio.
- **Mixer 0x81 DMA discovery.** No audio.
- **0xE3 copyright probe.** No audio.
- **Cyrix FPU env-gate.** Cathode does not probe FPU brand.
- **playback module / UI ENTER wiring.** That was a HEARO bug
  (browser ENTER never called drv->open). Cathode's main loop
  already wires KEY_ENTER through to navigation; verified in
  `cathode/src/main.c:56-60`.

## Things explicitly unchanged in this addendum

- Cathode v0.2 release scope and the 12/12 fixture suite.
- Existing `2026-04-13-cathode-v02-design.md` plan (this is sibling
  detail, not replacement).
- Render / parser / fetch contracts.
- The "(sort of)" qualifier in the project description — keep until
  Cathode v1.0 ships actual JavaScript-aware browsing or never (the
  honesty of the qualifier is a feature).

This addendum adds detail to v0.3+ planning without redirecting it.
