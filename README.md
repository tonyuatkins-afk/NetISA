# NetISA

**An ISA coprocessor that tries to give old PCs a way to talk to the modern internet.**

> **Status (2026-04-23):** Parts landed. Every line of the BOM is now on the bench: TexElec 8-bit ISA prototype card, ESP32-S3 modules, ATF1508AS CPLDs, passives, connectors, programmer, anti-static everything.  
> Firmware, DOS stack, and apps are working in DOSBox-X. CPLD logic passes its testbench (160/160).  
> Bench assembly starts next. Fully expecting at least one thing to go wrong immediately.

> **https://barelybooting.com/**: project site, screenshots, status  
> https://barelybooting.com/log.html: build log, RSS feed  
> https://www.youtube.com/@BarelyBooting: build videos

---

NetISA is an open-source 8/16-bit ISA expansion card for IBM PC/XT/AT and 386/486 systems.

The basic idea is:

Trying to do modern TLS on an 8088 (or even a 386) is just not going to work well. You either get something painfully slow or something that barely holds together.

So instead of forcing the host CPU to do that work, NetISA moves it somewhere else.

- A CPLD (ATF1508AS) handles ISA bus timing and exposes a register interface
- An ESP32-S3 handles WiFi, TCP/IP, and TLS using its built-in crypto hardware
- The DOS side talks to it through a small TSR like it’s just another device

The intent is to keep the host CPU out of the TLS path entirely: no serial bottleneck, no second machine acting as a proxy, no software TLS on the 4.77 MHz CPU.

That's the design. Whether real hardware behaves the same way is the open question, and that's the next phase.

---

## Why this exists

This didn’t start as some big planned thing.

A lot of it comes from seeing what people have already pulled off with ISA hardware, especially projects like XT-IDE and PicoMEM. And honestly just watching way too many hours of Adrian’s Digital Basement and Usagi Electric.

At some point it kind of clicked that people are pushing these machines way past what they were originally meant to do. Storage, memory, all sorts of clever expansions.

So the question became: what about networking?

More specifically, what would it take to let a really old PC talk to modern HTTPS services without needing another computer sitting next to it doing all the work.

NetISA is basically me trying to answer that, probably the hard way.

---

## Scope

NetISA is a register-mapped coprocessor for legacy PCs (not a traditional NIC). The hardware and firmware together offload the parts of networking that old CPUs handle badly: WiFi, TCP/IP, and TLS. The host CPU sees a small register interface and a TSR; it does not see packets.

The project does not turn a 486 into a modern PC. CPU-bound tasks like rendering or video decode are unaffected. And the hardware is not finished: parts are on the bench, assembly is the next phase.

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

## Why a Coprocessor?

Every retro networking project I looked at before starting NetISA hits the same wall: TLS.

The modern internet requires HTTPS, which means TLS 1.2 at minimum. Running TLS on a vintage CPU is possible. [WinGPT](https://www.dialup.net/wingpt/tls.html) got TLS 1.3 working on a 486 running Windows 3.1 by porting WolfSSL to 16-bit, and [Crypto Ancienne](https://github.com/classilla/cryanc) brings TLS 1.2/1.3 to classic Mac OS, AmigaOS, and a dozen other platforms. But both need at least a 486 or 68040, and both disable certificate verification and use fake entropy to make it fit. Below a 486, the asymmetric key exchange math simply doesn't work in any reasonable time.

Hardware WiFi cards like [PicoMEM](https://github.com/FreddyVRetro/ISA-PicoMEM) solve the radio problem but not the crypto problem. They emulate an NE2000 on the ISA bus and bridge to WiFi, but TLS still has to run on the host. Proxies like [FrogFind](https://github.com/ActionRetro/FrogFind) and [Browservice](https://github.com/ttalvitie/browservice) work universally but require a second modern machine.

NetISA takes a different approach: move TLS termination onto the card. The ESP32-S3 has hardware-accelerated AES/SHA/RSA/ECC, a real hardware RNG, and enough RAM for certificate chains. The host just sends and receives plaintext. An 8088 at 4.77 MHz can talk to any HTTPS API because it never touches the cryptography.

## Related Projects

**ISA / expansion bus hardware:**
[PicoMEM](https://github.com/FreddyVRetro/ISA-PicoMEM) (FreddyVRetro): RP2040 ISA card with memory, storage, sound, and NE2000 WiFi. [PicoGUS](https://github.com/polpo/picogus) (Ian Scott): RP2040 ISA sound card. [Graphics Gremlin](https://github.com/schlae/graphics-gremlin) (Eric Schlaepfer): FPGA ISA video card. [PicoPCMCIA](https://www.yyzkevin.com/picopcmcia/) (Kevin Moonlight): RP2350 PCMCIA multi-card. [SEthernet/30](https://github.com/rhalkyard/SEthernet) (Richard Halkyard): Mac SE/30 PDS Ethernet.

**Software TLS on vintage platforms:**
[Crypto Ancienne](https://github.com/classilla/cryanc) (Cameron Kaiser): TLS 1.2/1.3 for classic Mac OS, AmigaOS, IRIX, and more. [WinGPT / WolfSSL Win16](https://www.dialup.net/wingpt/tls.html) (dialup.net): TLS 1.3 on Windows 3.1. [AmiSSL](https://github.com/jens-maus/amissl) (Jens Maus): full OpenSSL 3.x / TLS 1.3 for AmigaOS. [Secure Oldies](https://retrocoding.net/secure-oldies-v-windows-3x-and-winsock-11) (Didiet Noor): mbed TLS on Windows NT 3.x.

**DOS networking:**
[mTCP](https://www.brutman.com/mTCP/) (Michael Brutman): TCP/IP for DOS, 8088+. [DOStodon](https://github.com/SuperIlu/DOStodon) (SuperIlu): DOS Mastodon client. [MicroWeb](https://github.com/jhhoward/MicroWeb) (James Howard): web browser for 8088-class PCs.

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

---

## Software Suite

The Barely Booting suite is a set of DOS applications that exercise the NetISA API. None of them ship as products; each is open source and currently runs against a stub backend in DOSBox-X. Hardware-backed runs come after bench bring-up.

| App | Description | Status |
|-----|-------------|--------|
| HEARO | Music player with hardware recognition | v1.0.0 tagged 2026-04-25, software only |
| CHIME | Time sync (HTTPS Date header v1.0; SNTP/NTS planned for v1.1) | v1.0.0 tagged 2026-04-25, software only |
| CATHODE | Text-mode web browser | v0.2, builds clean against stub and hardware backends |
| DISCORD | Chat client (v2 ground-up rebuild) | Builds clean, stub backend only so far |
| CLAUDE | Anthropic API client | v0.1, builds clean, stub backend only so far |
| COURIER | Email client | Planned |
| CRATE | Cloud filesystem | Planned |
| KIOSK | Package manager | Planned |
| RADIO | Lightweight streaming (XT-class) | Planned |

See [hearo/README.md](hearo/README.md) for the first suite app to leave the design phase.
