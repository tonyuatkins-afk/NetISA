# HEARO session applicable lessons — NetISA repo-wide

Drafted 2026-04-26 after the HEARO session produced sixteen commits of
audio-detection, exit-cleanup, and test-infrastructure work. This
document covers what applies to the NetISA repo as a whole (firmware,
shared lib, DOS sub-projects) versus what stays local to HEARO. Sister
documents:

- `docs/plans/2026-04-26-cathode-session-lessons.md` — Cathode specific
- `C:\Development\CERBERUS\docs\plans\2026-04-26-hearo-session-lessons.md` — CERBERUS

The HEARO commits referenced are on `origin/main`, range `156695c..HEAD`.
File paths are absolute under `C:\Development\NetISA\`.

## Repo-wide process discipline

These apply to every sub-project in NetISA: hearo, cathode, discord,
chime, firmware, dos/.

### PROC-1: Linux-kernel-style `Assisted-by` trailer on every AI-assisted commit

Already adopted across NetISA. Memory at
`feedback_netisa_ai_assist_trailer.md`. Continue. The HEARO session
had 15/16 commits compliant; the one outlier was a docs commit that
predated the policy. Going forward, every code commit gets the trailer
with the real model ID.

### PROC-2: GPL contamination rule (read for patterns, never copy code)

Already adopted for HEARO (Mpxplay + OpenCP). Memory at
`feedback_gpl_contamination_rule.md`. The same rule applies to:

- **Cathode** if it ever consults Lynx/links/dillo source for HTML
  parser patterns (all GPL/GPL-compatible)
- **Discord** if it consults any open-source IRC/chat client for
  protocol patterns
- **Firmware** if it consults Linux kernel ESP32 drivers or other
  GPL ESP-IDF examples

The deliverable is "the same pattern, written from scratch." Brief
prose citations are fair use; line-by-line ports are not. When
implementing, work from notes and the pattern in mind, not with the
GPL source open.

### PROC-3: Iterative code review with fresh-eyes rotation

The HEARO session ran three review rounds, each with a fresh
reviewer (no anchoring on prior findings). Each round caught at
least one fresh issue:

- Round 1: 4 Important — fixed in `6755251`
- Round 2: 2 Important — fixed in `dfefcfd`
- Round 3: 2 Important — one fresh (variant code overwrite, fixed
  in `4b3d2d9`), one project-accepted stance

Pattern: dispatch a code-reviewer subagent per milestone or per
significant feature, fix Critical + Important findings, re-dispatch
with a NEW reviewer. Stagnation signal (same Critical+Important
count two rounds in a row) escalates to user.

Manifest archived at
`hearo/docs/research/session-2026-04-26-review-manifest.jsonl`.

Apply per-milestone in any NetISA sub-project's quality-gate work.

### PROC-4: Read-before-write on chunk-A-equivalents

HEARO declared chunk-A (`hearo/src/audio/*`, 10 files, 40 fixes)
load-bearing after a six-round quality gate. Touching chunk-A files
post-gate requires surfacing as a question first.

Analogous load-bearing surfaces exist in other NetISA sub-projects:

- **Firmware**: `firmware/main/cmd_handler.c`, `wifi_mgr.c`,
  `nv_config.c` — gated by 8-pattern security hardening
  (`feedback_firmware_security.md`)
- **Cathode**: `cathode/src/htmltok.c` and `htmlout.c` — gated by 12/12
  fixture tests (`project_cathode.md`)
- **Discord**: `discord/src/*` — recently rebuilt as v2 with 10
  modules

Any change to those files should surface as a question rather than
land silently. The CLAUDE.md or per-project docs should call out the
gated surface explicitly.

## Repo-wide test infrastructure

### TEST-1: 86Box VM matrix as a NetISA-wide test asset

Three FreeDOS 1.4 VMs at `C:\Tools\86Box\vms\`:

- `hearo-sb16` — Sound Blaster 16 reference
- `hearo-ymf715` — Yamaha OPL3-SA3
- `hearo-ess` — ESS ES1688

All three are tf486 (Award BIOS, 486SX 16 MHz, 16 MB RAM). The card
only matters for audio tests, so the same VMs work for any DOS
sub-project that wants a non-Vibra-16S regression baseline.

Test loop is `_scripts\run-test.ps1 -Vm <vm> -Payload <dir>`: host
packs payload onto transfer.vhd, launches 86Box, guest's AUTOEXEC
runs `D:\AUTORUN.BAT`, AUTORUN ends with `FDAPM POWEROFF`, host reads
`D:\PROBE.LOG`.

**Sub-project applicability:**

- **HEARO**: audio tests (current; payload at
  `_assets\hearo-mod-probe\`)
- **CERBERUS**: detection tests (per CERBERUS plan, payload at
  `_assets\cerberus-detect\`)
- **Cathode**: HTML parser + renderer tests against synthetic stub
  pages (no NetISA card emulation, so live fetch is not testable
  there; but the parser fixtures are)
- **Discord**: same caveat as Cathode (no NetISA card)
- **dos/netisa.exe** etc. — testable for command-line behavior, not
  for actual NetISA card I/O

Reference: `~/.claude/projects/.../memory/reference_86box_vms.md`.

### TEST-2: Multi-platform smoketest matrix definition

Each sub-project should have an explicit smoketest matrix. HEARO's
session smoketest was MOD-only at 22050 Hz on DOSBox-X SB16. Round
3 review flagged this as a coverage gap.

**Recommended matrix** for any DOS sub-project at release-gate time:

- DOSBox-X 386 / 486 / Pentium cycle settings (catches CPU-class
  branches)
- 86Box `hearo-sb16` / `hearo-ymf715` / `hearo-ess` (catches audio
  family branches; meaningful only for audio-aware projects)
- Real iron 486 DX-2-66 + Vibra 16S
- Real iron 386 DX-40 + Aztech ISA

The first two are local and run between development passes. Real-iron
is per-release-gate.

For **firmware**, the matrix is different: ESP32 unit + WPA2 AP test
+ HTTP/WS server smoke. Firmware testing infrastructure is documented
separately.

## DOS hygiene patterns (apply to every DOS sub-project)

### DOS-1: atexit + SIGINT/SIGBREAK + `_harderr` cleanup handlers

HEARO `hearo/src/hearo.c:38-80`. Idempotent `run_shutdown` helper
registered via `atexit()`, called from `signal(SIGINT, ...)` /
`signal(SIGBREAK, ...)`, and `_harderr(handler)` returning
`_HARDERR_FAIL` to suppress DOS's "Abort, Retry, Fail" prompt over a
text-mode UI.

**Sub-projects that should adopt this pattern:**

- **Cathode** (currently has no signal handlers; Ctrl-C during browse
  exits with screen state intact and possibly leaks NetISA card
  state). See `2026-04-26-cathode-session-lessons.md` DOS-1.
- **Discord** (same concern; long-lived connection, Ctrl-C during
  active session)
- **dos/netisa.exe** (less critical but the `_harderr` part matters
  if the user runs it against a not-ready drive)

Watcom-specific: `_DOSFAR` macro is `#undef`-ed at end of `<dos.h>`;
use `__far` directly in handler parameter declarations. SIGBREAK is
Watcom-specific (separate from SIGINT for Ctrl-Break vs Ctrl-C).

### DOS-2: DOS MCB chain validation on exit

HEARO `hearo/src/platform/dos.c:dos_mcb_validate`. Walks MCB chain
via INT 21h AH=52h head pointer at ES:[BX-2]. Six independent
corruption checks, 0x4000 walk budget, head-segment range guard.

The function is generic DOS hygiene, not HEARO-specific. Drop into
the shared `lib/` directory and any sub-project can call it from its
exit path. Cheap insurance: ~50 lines, ~microseconds at runtime,
gives operators a clear signal when the app is the cause of MCB
corruption rather than DOS panicking inscrutably.

**Sub-projects that should adopt:**

- **Cathode** (allocates per-page buffers, frees on navigate; classic
  use-after-free or leak surface)
- **Discord** (allocates connection state, message buffers)
- **HEARO** (already has it)

Promote `dos_mcb_validate` from `hearo/src/platform/dos.c` to
`lib/dos.c` (shared). The migration is a one-time copy + delete,
then HEARO's call site changes from `#include "platform/dos.h"` to
`#include "../lib/dos.h"` (or wherever the shared header lives).

### DOS-3: INT 16h direct keyboard, never libc kbhit/getch

NetISA has this discipline already via `lib/screen.c:scr_getkey` /
`scr_kbhit` (INT 16h AH=00h / AH=01h direct, no libc INT 21h
indirection). All sub-projects that need keyboard input should use
the shared lib functions, not libc.

**Compliance check:**

- HEARO: uses INT 16h direct in `hearo/src/ui/screen.c:scr_getkey`
  (a HEARO-local copy; could consolidate to shared lib)
- Cathode: uses `scr_kbhit` / `scr_getkey` from shared lib ✓
- Discord: needs verification (probably also uses shared lib)
- dos/netisa.exe: command-line, no keyboard polling

The HEARO-local screen.c is a small divergence from the shared lib
pattern; not a bug, but worth consolidating during the next major
HEARO refactor pass.

### DOS-4: Bounded port-write timeouts

HEARO `hearo/src/audio/sb.c:dsp_write_to`. Spin on the busy bit for
at most N iterations, then return failure if it never cleared. Caller
treats failure as "chip not responsive" and moves on rather than
hanging forever.

**Sub-project applicability:**

- **HEARO**: already has it for SB DSP writes
- **Cathode**: the `fetch.c` polls during NetISA card I/O; should
  audit for unbounded busy-wait loops (likely already bounded since
  the fetch can be cancelled via Ctrl-C, but verify)
- **CERBERUS**: per its plan, audit any DSP probing loops
- **Firmware**: N/A (FreeRTOS task scheduling has its own timeout
  primitives)

The pattern is a 5-line idiom; spread it wherever a chip's busy bit
is being polled.

## Firmware-specific items (limited applicability)

ESP32 firmware is not DOS, so most patterns above don't apply. What
does:

### FW-1: Iterative code review per milestone

PROC-3 above. Firmware's existing 8-pattern security hardening
(`feedback_firmware_security.md`) was the analog of HEARO's
chunk-A gate; the iterative review pattern is the same shape and
applies to any future firmware milestone.

### FW-2: Read-before-write discipline on hardened modules

PROC-4 above. The 8 patterns FIXED in firmware (WPA2 AP, ct_strcmp,
per-request ISR bufs, OTA SHA256, session mutex, etc.) are
load-bearing. Touching any of those modules requires surfacing as a
question first.

What does NOT apply to firmware:

- DSP timeouts (no DSP)
- MCB validation (no DOS)
- atexit / signal handlers (FreeRTOS task model, not POSIX-ish exit)
- INT 16h keyboard (no keyboard)
- 86Box VM testing (architecture mismatch — ESP32 not x86)

## Sequencing recommendation

If NetISA's next pass is across multiple sub-projects:

**Quickest cross-project win:** promote `dos_mcb_validate` from HEARO
to shared `lib/dos.c`. One commit. Unblocks adoption in Cathode,
Discord, dos/netisa.exe.

**Second quickest:** adopt atexit + signal handlers in Cathode (see
its dedicated plan). One commit.

**Per-project deep work:** see CERBERUS plan and Cathode plan for the
sub-project-specific details.

**Process activation:** start using PROC-3 (iterative review)
formally at the next sub-project milestone. The infrastructure
already exists; it's a discipline question, not a tooling one.

## Things explicitly out of scope for this addendum

- Sub-project deliverables (HEARO Phase 5, CERBERUS 0.9.0, Cathode
  v0.3, Discord v3, etc.) — those have their own plans.
- The 0.8.x firmware security hardening work — already complete.
- Hardware design changes (KiCad, CPLD).
- The barelybooting.com server side.

This addendum is purely about applying repo-wide lessons from one
session of HEARO work to the rest of the NetISA umbrella.
