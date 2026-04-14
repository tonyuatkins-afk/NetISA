# NetISA

**An ISA coprocessor that aims to give old PCs a realistic path to modern networking.**

> **Status:** Hardware not assembled yet. I am waiting on hardware to deliver from DigiKey, Amazon, and TexElec (don't tell my wife...!!!!) But, Firmware, DOS stack, and apps are built and tested in DOSBox-X. I am terrible at Assembly and C, so this was fun to mess up along the way. Real hardware bring-up is next, and I am looking forward to building it and seeing if it starts on fire.

> **[barelybooting.com](https://barelybooting.com/)** — build log, screenshots  
> • [YouTube: @BarelyBooting](https://www.youtube.com/@BarelyBooting) — videos  
> • [Build log + RSS](https://barelybooting.com/log.html)

---

NetISA is an open-source 8/16-bit ISA expansion card for IBM PC/XT/AT and 386/486 systems.

The core idea is pretty simple:

Trying to do modern TLS on an 8088 or even a 386 is a losing battle. You either end up with unusable performance or a fragile stack that barely works.

So instead of forcing the host CPU to do the hard part, NetISA moves that work off the system entirely.

- A **CPLD (ATF1508AS)** handles ISA bus timing and presents a clean register interface
- An **ESP32-S3** handles WiFi, TCP/IP, and TLS using its built-in crypto hardware
- The DOS side talks to it through a very small TSR as if it were a coprocessor

No serial bottlenecks, no second machine acting as a proxy, and no pretending a 4.77 MHz CPU wants to do RSA.

That’s the design goal, anyway. The hardware hasn’t had its chance to disagree yet.

---

## What this project is (and isn’t)

**What it is:**
- A hardware + firmware approach to giving legacy PCs access to modern HTTPS services
- A register-mapped coprocessor (v1), not a traditional NIC
- A way to offload the parts of networking that old CPUs are objectively bad at

**What it is not:**
- A way to make a 486 behave like a modern PC
- A fix for CPU-bound workloads (rendering is still expensive)
- Finished hardware (yet)

---

## What this is designed to unlock

If the hardware behaves and the integration holds up, a NetISA-equipped system should be able to:

- Fetch content from modern HTTPS sites in text-mode browsers (Cathode, Lynx, Links, etc.)
- Talk to APIs and services that require TLS 1.2/1.3
- Pull software from HTTPS-only sources
- Send and receive email over IMAPS/SMTPS
- Interact with services like Discord, Mastodon, Matrix, or IRC-over-TLS
- Sync files via WebDAV-over-HTTPS
- Publish data to MQTT-over-TLS brokers

The goal is not “modern web browsing on a 486.”  
The goal is “a 486 can still participate in modern networks without cheating.”

---

## What works today

Right now everything is running **without real ISA hardware**, using a stubbed backend:

- DOS TSR, API, and applications are functional
- ESP32 firmware (WiFi, HTTP, HTML parsing, config UI) is complete
- CPLD logic is written and passes the full testbench (160/160)
- End-to-end flows are tested in DOSBox-X using simulated responses

In other words: the software stack is ready. The hardware has not yet had its turn.

---

## What does not work yet

- No physical ISA card has been assembled or tested
- No real bus timing validation on actual machines
- No live TLS session from a DOS system over ISA

That’s Phase 0 bring-up.

---

## What this will never unlock

Even if everything works perfectly:

- No modern graphical web browsing
- No video streaming
- No escaping CPU limits

TLS was never the bottleneck for those.
