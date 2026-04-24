# NetISA

**An ISA coprocessor that tries to give old PCs a way to talk to the modern internet.**

> **Status (2026-04-23):** Parts landed. Every line of the BOM is now on the bench: TexElec 8-bit ISA prototype card, ESP32-S3 modules, ATF1508AS CPLDs, passives, connectors, programmer, anti-static everything.  
> Firmware, DOS stack, and apps are working in DOSBox-X. CPLD logic passes its testbench (160/160).  
> Bench assembly starts next. Fully expecting at least one thing to go wrong immediately.

> **https://barelybooting.com/** — build log, screenshots  
> • https://www.youtube.com/@BarelyBooting — videos  
> • https://barelybooting.com/log.html — build log + RSS

---

NetISA is an open-source 8/16-bit ISA expansion card for IBM PC/XT/AT and 386/486 systems.

The basic idea is:

Trying to do modern TLS on an 8088 (or even a 386) is just not going to work well. You either get something painfully slow or something that barely holds together.

So instead of forcing the host CPU to do that work, NetISA moves it somewhere else.

- A CPLD (ATF1508AS) handles ISA bus timing and exposes a register interface
- An ESP32-S3 handles WiFi, TCP/IP, and TLS using its built-in crypto hardware
- The DOS side talks to it through a small TSR like it’s just another device

No serial bottlenecks, no second machine acting as a proxy, and no pretending a 4.77 MHz CPU wants to do RSA.

That’s the plan anyway. Hardware will probably have opinions.

---

## Why this exists

This didn’t start as some big planned thing.

A lot of it comes from seeing what people have already pulled off with ISA hardware, especially projects like XT-IDE and PicoMEM. And honestly just watching way too many hours of Adrian’s Digital Basement and Usagi Electric.

At some point it kind of clicked that people are pushing these machines way past what they were originally meant to do. Storage, memory, all sorts of clever expansions.

So the question became: what about networking?

More specifically, what would it take to let a really old PC talk to modern HTTPS services without needing another computer sitting next to it doing all the work.

NetISA is basically me trying to answer that, probably the hard way.

---

## What this project is (and isn’t)

**What it is:**
- A hardware + firmware approach to getting legacy PCs onto modern networks
- A register-mapped coprocessor (v1), not a traditional NIC
- A way to offload the parts of networking that old CPUs are really bad at

**What it is not:**
- A way to turn a 486 into a modern PC
- A fix for CPU-bound problems (rendering is still slow, always will be)
- Finished hardware (not even close yet)

---

## What this is supposed to unlock

If everything works the way I think it should, a NetISA system would be able to:

- Fetch content from modern HTTPS sites in text-mode browsers
- Talk to APIs that require TLS 1.2/1.3
- Download software from HTTPS-only sources
- Send and receive email over IMAPS/SMTPS
- Interact with things like Discord, Mastodon, Matrix, or IRC-over-TLS
- Sync files via WebDAV-over-HTTPS
- Push data to MQTT-over-TLS brokers

The goal is not “modern browsing on a 486.”

The goal is more like:  
a 486 can still exist on today’s internet without cheating.

---

## What actually works right now

Right now this is all running without real ISA hardware, using a stubbed backend:

- DOS TSR and API are working
- Applications (browser, Discord client, etc.) run against simulated responses
- ESP32 firmware (WiFi, HTTP, HTML parsing, config UI) is complete
- CPLD logic is written and passes the testbench (160/160)

So the software side is in decent shape.

None of that guarantees it works once real signals are involved.

---

## What does not work yet

- No physical card assembled (parts are on the bench; soldering iron is next)
- No validation on real ISA machines
- No real bus timing verification
- No actual TLS session from DOS over the card

That’s the next phase, and also where things are most likely to break.

---

## Things I expect to break

Some guesses before hardware even shows up:

- ISA timing edge cases, especially on slower systems
- Signal integrity issues during early wiring/prototyping
- Race conditions between ISR and firmware tasks under load
- Memory pressure on the ESP32 with multiple TLS sessions

None of this is based on testing yet, just experience with things like this going sideways.

---

## How this differs from existing solutions

There are already ways to get old machines online:

- Serial WiFi modems (WiFi232, zimodem, etc.)  
  These work, but you’re limited by serial speeds and usually one connection at a time.

- HTTPS proxies / stunnel setups  
  These work too, but require a second modern machine doing the heavy lifting.

- ISA NICs with software TLS  
  This pushes the problem back onto the host CPU, which doesn’t really scale below a fast 486.

NetISA is trying to take a different approach:

- Put networking and TLS on a coprocessor
- Expose it directly over ISA (no serial layer)
- Let the host talk at a higher level instead of dealing with raw packets

Whether that tradeoff is actually better in practice is still to be proven.

---

## High-level architecture

Very roughly:

- ISA bus -> CPLD -> register interface  
- CPLD -> ESP32-S3 (parallel interface)  
- ESP32 handles:
  - WiFi
  - TCP/IP stack
  - TLS sessions
  - HTTP and higher-level protocols

The DOS side uses a TSR and INT 63h API to talk to it.

---

## Hardware

- CPLD: Microchip ATF1508AS (5V, 128 macrocells)
- MCU: ESP32-S3 (WiFi + hardware crypto)
- Optional Ethernet: W5500
- Antenna: external (metal PC cases are not RF-friendly)

---

## Software

- DOS TSR (INT 63h handler)
- Launcher / config UI
- Cathode (text-mode browser)
- Discord client
- Claude client
- Shared screen rendering library
- Stub layer for testing without hardware
- ESP32 firmware (WiFi, HTTP, HTML parsing, config, etc.)

---

## Current phase

Parts gathered (2026-04-23). Hardware bring-up is finally unblocked:

1. ~~Finish gathering parts~~ (done)
2. Wire up prototype on the TexElec 8-bit ISA prototype card
3. Validate basic bus behavior (address decode, read/write cycles, IOCHRDY)
4. Connect DOS side to real hardware (swap the stub backend for the real register file)
5. See what breaks first

After that, iterate until it stops breaking (or at least breaks less).

---

## Building

### DOS side

```
make all
```

---

### Firmware

```
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

---

## Testing

Right now everything is tested through DOSBox-X and simulated responses.

That’s been enough to validate logic and flow, but it doesn’t prove anything about real hardware behavior yet.

---

## Final note

This is very much a “let’s see if this actually works” kind of project.

Some of the design decisions will probably turn out to be wrong.  
Some parts will almost definitely need to be reworked once real hardware is involved.

That’s fine. That’s kind of the point.
