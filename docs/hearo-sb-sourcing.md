# HEARO Sound Card Sourcing Guide

**Purpose:** identify the cheapest known-good test platform for HEARO so
that "is HEARO correct" stops being entangled with "is this Yamaha
cooperating." The Toshiba 320CDT's YMF715 OPL3-SAx detects as SB Pro
but its PCM engine never runs without a vendor init utility, which
contaminates every real-iron data point we get from it.

**Goal:** a working SB Pro 2 or SB16 in a 486 / Pentium 1 ISA box that
is reachable over the existing 486 dev env LAN (10.69.69.160 with
FTPSRV), so iteration loops are USB-class round-trip times.

## What we actually need

HEARO drives the SB family via its DSP, which has been stable since
SB 1.5 (1991). Any card in the SB lineage works. What matters for a
useful test:

1. **Real Creative silicon, not a clone.** The clones (Yamaha YMF7xx,
   ESS Audiodrive 18xx/19xx, OPTi 82C9xx, CMI/AC97 codecs) all have
   chip-specific init quirks that the SB driver should not have to
   know about. Test on the reference, then deal with clones as a
   compatibility layer.

2. **OPL3 onboard, not OPL2.** SB Pro 2 (CT1600) and SB16 both have
   OPL3. The original SB and SB Pro 1 had OPL2 only. We want OPL3 so
   midifm.c can be exercised end-to-end, including the four-operator
   voices.

3. **Jumpered, not PnP.** Plug-and-play SB16 cards (CT2940 Vibra16,
   AWE64) need CTCM.EXE / SBCONFIG.EXE to set their resources every
   boot. Skip them unless you also want to debug PnP. Jumpered cards
   set IRQ / DMA / base address with switches and "just work."

4. **ISA bus.** This is non-negotiable for the period-appropriate
   target. PCI SB compatibles (ENS1370, CT5880) emulate SB through a
   driver that doesn't load in pure DOS.

## Reference cards, in priority order

### Sound Blaster Pro 2 (CT1600)

The Phase 2 reference. Discrete YM3812 + YMF262 (OPL3), CT1345 mixer,
DSP 3.x, stereo 8-bit PCM up to 44.1 kHz mono / 22.05 kHz stereo.
Trivial to identify: full-length 8-bit ISA card, big YAMAHA chip near
the joystick port. Jumpers for IRQ / DMA / base.

eBay search: `"CT1600"` or `"Sound Blaster Pro 2 CT1600"`. Prices
$30-60 for tested cards. Do not pay extra for the OEM bracket.

Avoid: CT1330 (SB Pro 1, OPL2 only), CT1690 (SB Pro 2 OEM with weird
connector). The CT1600 is the one to want.

### Sound Blaster 16 (CT2230 / CT2740)

The Phase 3 reference. Adds 16-bit PCM and the SB16 mixer. CT2230 is
the original 1992 card; CT2740 is the 1995 revision that drops the
ASP / CSP socket. Both have proper OPL3 (CT2230 has Yamaha YMF262;
CT2740 has Creative CQM, which is an OPL3 clone that's good but not
identical).

eBay search: `"Sound Blaster 16 CT2230"` or `"CT2740"`. Prices $40-80.

Avoid: CT2940 (Vibra16, PnP required, dropped MIDI port from some
revisions), CT4520 / CT4540 (Vibra16C/CT, IDE controller and PnP
nonsense), anything labeled "AWE64 Gold" (overpriced for our needs;
AWE64 baseline is fine but pay $30, not $200).

### AdLib (Y8950 / OPL2 only)

Worth having for OPL2-specific testing if cheap. Single FM chip, no
PCM. Won't exercise the SB DSP path, but lets us validate that
midifm.c and the AdLib driver (adlib.c) work without an SB layer
underneath them.

eBay search: `"AdLib MSC"` or `"AdLib Music"`. Original cards are
expensive ($60+) due to collector interest. AdLib clones (e.g. ATI
Sound, various unbranded Korean cards) at the $10-20 range work
identically for our purposes.

Skip AdLib Gold; rare, expensive, and our probe is opt-in anyway.

## Target host

The 486 dev box at `10.69.69.160` already exists with FTPSRV running
and a documented FTP workflow. Dropping any of the above cards into
it gives us:

  - Static IP + FTP for sub-minute file transfer round trips
  - DOS 6.22 / Windows 3.11 dual environment (verify per memory
    `project_netisa_486.md`)
  - Cassette-class storage already working

The 386 box at `10.69.69.161` is a viable secondary target if we want
to validate on slower iron. But the 486 is the right primary.

Bracket layout: most ISA cases have 3-5 ISA slots. Pulling whatever
NIC is in there isn't an option (we need the network for FTP). The
SB has to share the bus with the NIC, which is fine; SB IRQ 5 + NIC
IRQ 10 + COM IRQ 4 don't conflict.

## What to look for in a listing

Trustable signals:

  - "Tested, working" with a photo of the card in a working DOS box
  - Visible jumper block in the photos (not "PnP only")
  - Seller is a vintage hardware specialist (recurring listings, good
    feedback for similar items)

Trustable in-photo:

  - YMF262 chip visible (OPL3)
  - CT1345 (SB Pro 2 mixer) or CT1745 (SB16 mixer)
  - No corrosion, no bent pins, no missing chips

Skip:

  - "Untested, sold as-is, parts only"
  - Listings that say "for repair"
  - Yellowed PCB with green corrosion around the connector (battery
    leak is the one thing you cannot easily fix)
  - Anything in a "lot of 5 cards" auction unless the per-card price
    is $5

## Estimated total

  - CT1600 SB Pro 2:               $40
  - Spare CT2230 SB16 (optional):  $50
  - Shipping (US):                 $10 each
  - Total to get unstuck:          $50

For comparison, every hour of "is the YMF715 cooperating" debugging
costs >$50 of opportunity cost. A real card pays for itself on the
first test.

## Once it arrives

1. Set jumpers: base 220h, IRQ 5, DMA 1 (8-bit), DMA 5 (16-bit if
   SB16). Same as the canonical defaults the SB driver falls back to.
2. Drop in 486 box. Make sure other ISA cards (NIC) don't conflict.
3. AUTOEXEC.BAT: `SET BLASTER=A220 I5 D1 H5 P330 T6`
4. CONFIG.SYS: nothing special (no init utility needed for jumpered
   Creative cards).
5. Boot. `TESTDET.EXE` should report SB Pro 2 / SB16 cleanly.
6. `TESTPLAY.EXE /T3 data\TONE.WAV` should produce audible output.
7. If output is clean, we have a baseline. Every subsequent HEARO
   change can be validated on real iron in <5 min.

## What this unblocks

  - Phase 3 real-iron validation of XM, S3M, IT decoders
  - GUS hardware-mixed playback (if we also acquire a GUS Classic)
  - Confidence that SB driver bugs are bugs, not chip-specific quirks
  - A reference data point against which to compare YMF715 / ESS /
    other clone behavior, which will inform the eventual "clone
    compatibility layer" if we go that route.
