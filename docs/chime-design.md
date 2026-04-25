# CHIME Design Document

**Version:** 1.0 (Implementation phase)
**Status:** Foundation in progress
**Target Platform:** IBM PC/AT and compatibles, 80286 minimum, DOS 3.3+
**Companion Project:** NetISA (network coprocessor card)

## 1. Summary

CHIME is a network time-sync tool for vintage IBM PC compatibles, part of the NetISA suite. It asks the network for the current UTC time, applies the configured timezone offset, and sets the DOS clock through INT 21h. Default is one command, no flags: pick a sensible server, sync, print the delta, exit.

DOS clocks drift, the CMOS battery is thirty years old, and the PIT is not a frequency standard. CHIME fixes that in one keystroke when the network path is up.

## 2. Scope

CHIME does one thing: ask the network for the current time, set the DOS clock. It does not run a clock service, schedule jobs, edit timezone databases, or compete with NTP daemons.

Defaults:

- Tries a small list of HTTPS time-anchor URLs in order, stops on the first response.
- Reports the achieved precision in the output line. v1.0 is one-second (HTTP `Date:` header limit). v1.1+ is sub-second once NetISA exposes UDP and CHIME can speak SNTP.
- Asks for `[Y/n]` before writing the clock unless `/AUTO` is set. `/DRYRUN` reports without writing.

## 3. Suite context

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

Inherited conventions:

- INI config (`CHIME.CFG`) using the suite section grammar.
- Cmdline flags `/SAFE`, `/STUBNET`, `/REDETECT`, `/VERSION`, `/HELP` mean the same thing across every app.
- MIT license.

## 4. Time source

### 4.1 Why not SNTP

NTP/SNTP runs over UDP. The NetISA TSR `INT 63h` API as of v1.0 exposes TLS sessions and plaintext TCP via the session group (group 0x03). No UDP yet. So v1.0 cannot speak SNTP.

### 4.2 What v1.0 does instead

Three sources, tried in order:

1. **HTTPS `HEAD` to a time-anchor URL.** `time.cloudflare.com`, `time.google.com`, `worldtimeapi.org`. RFC 7231 `Date:` header in the response. ~600 byte handshake, ~200 byte response. One-second precision.
2. **HTTPS `GET` JSON.** `worldtimeapi.org/api/timezone/Etc/UTC` returns a JSON document with `unixtime` and `utc_datetime` fields. Body parser fallback.
3. **Plain HTTP `HEAD`.** Same as (1) but over `NI_SESS_OPEN_PLAIN` to port 80. Diagnostic-mode fallback for systems with TLS-path issues. Off by default.

Override with `/SERVER=hostname`. Source type via `/MODE=https-head|https-json|http-head`.

### 4.3 v1.1+

When NetISA exposes UDP (group 0x08, planned):

4. **SNTP over UDP.** RFC 4330. 48-byte packet to server port 123. Sub-second precision via offset/delay against the server's transmit timestamp. Becomes default.
5. **NTS-KE over TLS, NTS over UDP.** RFC 8915. Secure NTP with mutual auth via TLS 1.3 key extraction. Off by default; enabled with `/NTS`.

The HTTP-Date path stays as the diagnostic fallback in v1.1+.

## 5. Setting the clock

### 5.1 DOS clock layers

- **CMOS RTC.** Battery-backed, at I/O ports 0x70/0x71. Persists across reboots. One-second resolution.
- **BIOS tick counter.** Memory at 0040:006C. 18.2 Hz from PIT channel 0. Resets on reboot, reflects RTC at startup.
- **DOS internal date/time.** INT 21h AH=2Ah/2Ch get, AH=2Bh/2Dh set. Sets BIOS tick, sometimes writes back to CMOS.

### 5.2 What CHIME writes

1. Compute target UTC + configured timezone offset.
2. Decompose into year/month/day/hour/minute/second.
3. INT 21h AH=2Bh sets DOS date.
4. INT 21h AH=2Dh sets DOS time.
5. Direct CMOS RTC write via ports 0x70/0x71 as backstop. On modern DOS (MS-DOS 5+, FreeDOS) the INT 21h call propagates to CMOS. On older DOS (3.3, OEM 4.x) it does not, and the next reboot loses the change.

CMOS write sequence: stop RTC updates by setting bit 7 of register 0x0B; BCD-encode and write seconds (reg 0), minutes (2), hour (4), day (7), month (8), year-mod-100 (9), century (32, conventional); resume updates.

### 5.3 Timezone

Fixed-offset only. `/TZ=-08:00` or in `CHIME.CFG`. Default is UTC (offset zero). DST is not handled; users supply the offset they want.

### 5.4 Default output

```
CHIME 1.0
NetISA: ready (firmware 1.0.0, IP 192.168.1.42)
Server: time.cloudflare.com (HTTPS HEAD)
Time:   2026-04-25 14:30:42 UTC (-08:00 = 06:30:42 local)
Now:    2026-04-25 06:30:39 (DOS clock)
Delta:  +3 seconds
Write to DOS clock? [Y/n]
```

`/AUTO` writes without prompting. `/DRYRUN` never writes. `/QUIET` prints only the result line.

## 6. Use modes

- **One-shot.** Run `CHIME` from the prompt, AUTOEXEC.BAT, a menu. Default v1.0 mode.
- **Periodic.** Weekly batch job: `CHIME /AUTO /QUIET`.
- **TSR (v1.3).** `CHIME.TSR` resident hooks INT 21h time-of-day calls and resyncs every 12 hours when the network path is up. Under 4 KB resident size.

## 7. System requirements

| Tier | CPU | RAM | Network |
|------|-----|-----|---------|
| Minimum | 286/12 | 256 K | NetISA card with TLS path |
| Standard | 386SX/16 | 512 K | Same |
| Maximum | 486DX2/66+ | 4 MB+ | Same plus DNS cache enabled |

CHIME is light. The constraint is the NetISA TLS handshake, which takes 1-3 seconds at the bus level regardless of host CPU.

## 8. Roadmap

- **v1.0 (current).** HTTPS HEAD + JSON GET + plain HTTP fallback. Confirmation-prompt UI. INT 21h + CMOS write-through.
- **v1.1.** SNTP over UDP when NetISA exposes group 0x08. Sub-second precision, becomes default.
- **v1.2.** NTS over UDP per RFC 8915.
- **v1.3.** `CHIME.TSR` resident with periodic resync.
- **v1.4.** Optional COM-port time source for serial GPS receivers (1PPS + NMEA).
- **v1.5.** Multi-server median-of-N consensus, drift trend reporting, NIST traceability claim when reachable.

## 9. Out of scope

CHIME is not a clock service, NTP daemon, stratum-2 server, DST manager, calendar app, or scheduled-task runner. One-second precision in v1.0 is acceptable for DOS.
