# CHIME Design Document

**Version:** 1.0 (Implementation phase)
**Status:** Foundation in progress
**Target Platform:** IBM PC/AT and compatibles, 80286 minimum, DOS 3.3+
**Companion Project:** NetISA (network coprocessor card)

## 1. Executive Summary

CHIME is a network time synchronization tool for vintage IBM PC compatibles, part of the NetISA suite. It queries an authoritative time source over the network, converts the result to DOS calendar form, and sets the system clock through INT 21h services. Defaults are aggressive: a single command, no flags, picks a sensible server, syncs once, prints the delta, exits.

DOS clocks drift. The CMOS battery is thirty years old. The PIT is not a frequency standard. Every retro PC eventually shows a date in 1980 because DOS rolled over at midnight on a clock with no power. CHIME is the small, focused tool that fixes this in one keystroke when the network path is up.

## 2. Vision: What CHIME Is For

### 2.1 The North Star

CHIME exists to do exactly one thing well: ask the network what time it is, and tell DOS. Everything else is a nice-to-have.

### 2.2 Design Principles

**One job, done correctly.** CHIME does not also run a clock service, edit the timezone database, schedule jobs, or talk NTP-the-protocol. It asks the network for a UTC timestamp, applies the configured offset, and writes to the DOS clock. That is the entire program.

**Pick a default that works.** Out of the box, CHIME tries a small handful of well-known HTTPS time-bearing endpoints in order. If the first responds, the rest are skipped. Users who want a specific server pass `/SERVER=`.

**Honest about precision.** HTTP `Date:` headers carry a one-second resolution. CHIME reports the achieved precision in its output (`set DOS clock to 14:30:42 UTC, 1s precision`). When NetISA grows a UDP path in v1.1 and CHIME gains real SNTP, the precision claim improves to milliseconds and the output line says so.

**Small, no TSR (yet).** v1.0 ships as a one-shot syncer. Run it from `AUTOEXEC.BAT`, run it from a menu, run it once a week. v1.5 adds the optional CHIME.TSR resident that hooks INT 21h time-of-day calls and silently corrects drift. Neither version is mandatory; both work standalone.

**No surprises.** CHIME never adjusts the clock without explicit consent. The default mode prints the delta and asks for `Y/n` before writing. `/AUTO` skips the prompt for use in batch files. `/DRYRUN` always reports without writing.

## 3. Suite Context

CHIME is one of nine NetISA suite applications:

| App | Role |
|-----|------|
| CATHODE | Web browser |
| DISCORD | Chat client |
| CLAUDE | LLM client |
| COURIER | Email (Gmail/Outlook OAuth2) |
| CRATE | Cloud files as DOS drive |
| KIOSK | Package manager |
| CHIME | NTP/NTS time sync |
| HEARO | Music player |
| RADIO | Lightweight streaming client (XT-class) |

Suite conventions inherited unchanged: INI config (`CHIME.CFG`) under the suite section grammar; command-line flags `/SAFE`, `/STUBNET`, `/REDETECT`, `/VERSION`, `/HELP` mean the same thing across every app; boot-screen visual language (right-justified status words, bright/dim attributes, deadpan tone); MIT license.

## 4. Time Source Strategy

### 4.1 Why not SNTP?

The protocol most people associate with network time is NTP, with its small-form sibling SNTP. Both run over UDP. The NetISA TSR `INT 63h` API as of v1.0 exposes TLS sessions and plaintext TCP via the session group (group 0x03), but does not yet carry UDP datagrams. CHIME v1.0 cannot speak SNTP.

### 4.2 What CHIME does instead (v1.0)

Three time sources tried in order:

1. **HTTPS `HEAD` to a time-anchor URL.** `time.cloudflare.com`, `time.google.com`, `worldtimeapi.org` are common targets that return a current UTC timestamp in the `Date:` response header. Standard RFC 7231 format, parseable with a single regex-free walker. No body content read. ~600 byte handshake, ~200 byte response. Round-trip precision: 1 second.

2. **HTTPS `GET` to a JSON time API.** `worldtimeapi.org/api/timezone/Etc/UTC` returns a JSON document with `unixtime` and `utc_datetime` fields. Used as a second-line fallback because it is body-parsing rather than header-parsing.

3. **Plain HTTP `HEAD`.** Same as (1) but over `NI_SESS_OPEN_PLAIN` to port 80 against a known-good responder. Available for users whose NetISA TLS path is misbehaving and need a diagnostic-mode fallback. Off by default.

The user can override via `/SERVER=hostname` for any of the three. Source type is inferred from URL or explicit `/MODE=https-head|https-json|http-head`.

### 4.3 What CHIME does in v1.1

When the NetISA TSR exposes UDP datagrams (group 0x08, planned), CHIME adds:

4. **SNTP over UDP.** RFC 4330. 48-byte packet to a server's port 123. Sub-second precision via the offset/delay calculation against the server's transmit timestamp. Becomes the default when available.

5. **NTS-KE over TLS, NTS over UDP.** RFC 8915. Modern secure NTP with mutual authentication via TLS 1.3 key extraction. Useful in environments where the network path cannot be trusted. Off by default; enabled with `/NTS`.

The HTTP-Date path stays in v1.1 as the diagnostic fallback. Users who never enable UDP keep working without changes.

## 5. Time-Set Strategy

### 5.1 The DOS clock

DOS exposes the system clock through three layers:

- **CMOS RTC** (battery-backed real-time clock at I/O ports 0x70/0x71). Persists across reboots. Resolution: 1 second.
- **BIOS tick counter** (memory at 0040:006C). 18.2 Hz, derived from PIT channel 0. Resets on reboot but reflects RTC at startup.
- **DOS internal date/time** (INT 21h AH=2Ah/2Ch get, AH=2Bh/2Dh set). Sets BIOS tick, optionally writes back to RTC.

CHIME sets all three when it can:

1. Compute target UTC time + configured timezone offset.
2. Decompose into year/month/day/hour/minute/second.
3. INT 21h AH=2Bh sets DOS date.
4. INT 21h AH=2Dh sets DOS time. The DOS interrupt handler updates the BIOS tick and (on most systems) writes through to CMOS.
5. As a backstop, CHIME also writes the CMOS RTC directly via ports 0x70/0x71 for systems where DOS does not propagate.

### 5.2 Timezone

CHIME does timezone-as-fixed-offset only. `TZ=PST8PDT` style POSIX names with DST rules are explicitly out of scope. Users supply the offset they want via `/TZ=-08:00` or in CHIME.CFG. The default is UTC (offset zero).

DST is the user's problem. CHIME does not adjust. This is intentional; DST handling is one of the great rabbit holes of computing and CHIME is not the program that solves it.

### 5.3 Confirmation prompt

Default behavior:

```
CHIME 1.0
NetISA: ready (firmware 1.0.0, IP 192.168.1.42)
Server: time.cloudflare.com (HTTPS HEAD)
Time:   2026-04-25 14:30:42 UTC (-08:00 = 06:30:42 local)
Now:    2026-04-25 06:30:39 (DOS clock)
Delta:  +3 seconds
Write to DOS clock? [Y/n]
```

`/AUTO` writes without asking. `/DRYRUN` never writes regardless. `/QUIET` suppresses the visual report and prints only the result line.

## 6. The Loop: Why You Run CHIME

### First Run
You install NetISA. Your DOS clock says it is March 1980. You type `CHIME`, it asks the cloud, it asks you, you say yes, the clock catches up to the present.

### After the Reboot
Your CMOS battery is dead and the clock reset. You add `CHIME /AUTO` to AUTOEXEC.BAT. From then on, every boot ends with the clock correct and a one-line "DOS clock set to 14:30:42 UTC" in the boot output.

### Periodic Sync
A weekly batch job runs `CHIME /AUTO /QUIET` from a scheduled task in some other tool. Drift never exceeds a few seconds.

### TSR Mode (v1.5)
You install `CHIME.TSR` via `CHIME /TSR`. Every program that reads the DOS clock (which is most of them) gets a quietly-corrected value. The TSR resyncs every 12 hours when the network path is up. Resident size: under 4 KB.

## 7. System Requirements

| Tier | CPU | RAM | Network |
|------|-----|-----|---------|
| Minimum | 286/12 | 256K | NetISA card with TLS path |
| Standard | 386SX/16 | 512K | Same |
| Maximum | 486DX2/66+ | 4MB+ | Same plus DNS cache enabled |

CHIME is light. The constraint is not CPU but the NetISA TLS handshake, which takes 1-3 seconds at the bus level regardless of host CPU.

## 8. Phased Roadmap

- **v1.0 Foundation (current).** HTTPS HEAD + JSON GET + plain HTTP fallback. Confirmation-prompt UI. INT 21h + CMOS write-through. Foundation phase complete; compilation pending bench iron.
- **v1.1 SNTP.** Real SNTP/UDP when NetISA exposes group 0x08. Sub-second precision, default time source.
- **v1.2 NTS.** RFC 8915 secure NTP with TLS-extracted keys.
- **v1.3 TSR.** `CHIME.TSR` resident with periodic resync, INT 21h hooks for transparent correction.
- **v1.4 GPS.** Optional COM-port time source for users with serial GPS receivers (1PPS pulse + NMEA).
- **v1.5 Multi-server.** Median-of-N consensus across multiple time sources, drift trend reporting, NIST traceability claim when reachable.

## 9. Anti-features

Not a clock service. Not an NTP daemon. Not a stratum-2 server. Not a DST manager. Not a calendar app. Not a scheduled-task runner. Not a precision instrument; a 1-second clock-set is acceptable for DOS and CHIME does not pretend otherwise.

## Closing Note

The other NetISA suite apps are about doing modern things on old machines: streaming, browsing, chatting. CHIME is about doing the most basic thing on old machines: knowing what time it is. That basic thing has been broken on every retro PC for decades. CHIME is the patch.
