# HEARO: Claude Code Scaffolding Prompt

## Context

You are scaffolding the foundation for HEARO, a music player for vintage IBM PC compatibles (286+, DOS 3.3+). HEARO is part of the NetISA suite, an open-source networking card project. The full design document is at `docs/hearo-design.md` and the novel arithmetic additions are at `docs/hearo-design-additions.md`. Read both before starting.

HEARO's north star is **recognition density**: every feature should make the user feel more connected to their specific machine, not less. The recognition engine (hardware detection, boot screen celebration, Hall of Recognition, unlock matrix) is the structural heart of the project. Build it first. Everything else plugs into it.

This is a new subdirectory within the existing NetISA repository. The existing project uses OpenWatcom for DOS libraries and NASM for assembly. Follow those conventions.

## AI Attribution Policy

All AI-assisted code must be committed with `Assisted-by: Claude Code:claude-opus-4-6` in the commit message. Human bears full responsibility for every line. "The AI wrote it" is never acceptable. AI-generated code passes the same quality gates as human code. This policy is adopted from the Linux kernel coding-assistants.rst.

## Toolchain

- **C compiler:** OpenWatcom C/C++ (wcc for 16-bit real mode, wcc386 for 32-bit protected mode)
- **Assembler:** NASM (already used in the NetISA project)
- **Linker:** OpenWatcom wlink
- **Build system:** wmake (OpenWatcom make)
- **Memory model:** Large model for real-mode build (far pointers for code and data)
- **Target CPU for real-mode build:** 286 minimum (`-2` flag to wcc)
- **Target CPU for protected-mode build:** 386 (`-3r` flag to wcc386, register calling convention)
- **Test environment:** DOSBox-X (configure for 286, 386, 486 testing via `machine=` and `cputype=` settings)
- **Source style:** C89/C90 (no C99 features; OpenWatcom's C90 compliance is solid, C99 is partial)

## What to Build

### Phase 1: Project Structure and Build System

Create the directory structure. This lives inside the existing NetISA repo as `hearo/`.

```
hearo/
├── README.md                    Project readme
├── Makefile                     wmake makefile, builds both HEARO.EXE and HEAROXM.EXE
├── src/
│   ├── hearo.c                  Main entry point, arg parsing, orchestration
│   ├── hearo.h                  Global types, constants, version info
│   ├── detect/                  Hardware detection subsystem
│   │   ├── detect.h             Public detection API
│   │   ├── detect.c             Detection orchestrator
│   │   ├── cpu.c                CPU class and clock detection
│   │   ├── cpu.h
│   │   ├── fpu.c               FPU presence, type, and brand detection
│   │   ├── fpu.h
│   │   ├── video.c             Video adapter and VESA detection
│   │   ├── video.h
│   │   ├── audio.c             Sound card detection (SB, GUS, AdLib, MPU, etc.)
│   │   ├── audio.h
│   │   ├── memory.c            Conventional, XMS, EMS detection
│   │   ├── memory.h
│   │   ├── netisa.c            NetISA card detection (stub for now)
│   │   ├── netisa.h
│   │   ├── input.c             Mouse, joystick detection
│   │   └── input.h
│   ├── unlock/                  Unlock system
│   │   ├── unlock.h             Public unlock API
│   │   ├── unlock.c             Unlock matrix evaluation
│   │   ├── hall.c               Hall of Recognition (read/write persistent data)
│   │   ├── hall.h
│   │   ├── whisper.c            Dormant-feature whisper system
│   │   └── whisper.h
│   ├── ui/                      User interface
│   │   ├── ui.h                 Public UI API (tier-independent)
│   │   ├── boot.c               Boot screen renderer (THE priority)
│   │   ├── boot.h
│   │   ├── mda.c               MDA/Hercules text-mode UI
│   │   ├── ega.c               EGA 640x350x16 UI
│   │   ├── vga.c               VGA Mode 13h 320x200x256 UI
│   │   ├── svga.c              VESA SVGA UI (640x480, 800x600, 1024x768)
│   │   └── settings.c          Settings panel with treasure-map locked features
│   ├── audio/                   Audio output drivers (stubs for Phase 1)
│   │   ├── audiodrv.h           Public audio driver API
│   │   ├── sb.c                 Sound Blaster family
│   │   ├── gus.c                Gravis Ultrasound family
│   │   ├── adlib.c              AdLib / OPL2 / OPL3
│   │   ├── mpu401.c             MPU-401 / MIDI output
│   │   ├── pcspeaker.c          PC Speaker RealSound PWM
│   │   └── nulldrv.c            Null driver (no audio output, for testing)
│   ├── config/                  Configuration system
│   │   ├── config.h             Public config API
│   │   ├── config.c             HEARO.CFG reader/writer (INI-style)
│   │   └── cmdline.c            Command-line flag parser
│   ├── math/                    Novel arithmetic library
│   │   ├── cordic.h             Software CORDIC with adaptive precision
│   │   ├── cordic.c
│   │   ├── bipartite.h          Bipartite tables for FPU-less transcendentals
│   │   ├── bipartite.c
│   │   ├── bipartite_tables.c   Pre-generated lookup tables (~4KB data)
│   │   ├── quire.h              Software quire (256-bit fixed-point accumulator)
│   │   └── quire.c
│   ├── stub/                    NetISA stub layer
│   │   ├── netisa_stub.h        Stub API matching real INT 63h interface
│   │   └── netisa_stub.c        Returns mock data for all network calls
│   └── platform/                Platform abstraction
│       ├── dos.h                DOS-specific helpers (interrupts, ports, DMA)
│       ├── dos.c
│       ├── timer.h              PIT timer utilities
│       └── timer.c
├── data/
│   ├── HEARO.VID                Video chipset database (text, hand-editable)
│   └── unlocks.dat              Unlock matrix definition file
└── test/
    ├── testdet.c                Standalone detection test (builds to TESTDET.EXE)
    ├── testboot.c               Standalone boot screen test (builds to TESTBOOT.EXE)
    ├── testcord.c               CORDIC accuracy and performance test
    ├── testquir.c               Quire accuracy test
    └── testbip.c                Bipartite table accuracy test
```

### Phase 2: Core Types and Constants (hearo.h)

Define the foundational types that everything else depends on.

```c
/* hearo.h - HEARO global types and constants */
#ifndef HEARO_H
#define HEARO_H

#define HEARO_VERSION_MAJOR  1
#define HEARO_VERSION_MINOR  0
#define HEARO_VERSION_PATCH  0
#define HEARO_VERSION_STRING "1.0.0"
#define HEARO_COPYRIGHT      "(c) 2026 Tony Atkins, MIT License"

/* Boolean for C89 */
typedef int bool_t;
#define TRUE  1
#define FALSE 0

/* Fixed-width integers (OpenWatcom provides these but be explicit) */
typedef unsigned char      uint8_t;
typedef signed char        int8_t;
typedef unsigned short     uint16_t;
typedef signed short       int16_t;
typedef unsigned long      uint32_t;
typedef signed long        int32_t;

/* ---------- Hardware profile ---------- */

/* CPU classes */
typedef enum {
    CPU_8088 = 0,
    CPU_8086,
    CPU_80186,
    CPU_80286,
    CPU_80386SX,
    CPU_80386DX,
    CPU_80486SX,
    CPU_80486DX,
    CPU_PENTIUM,
    CPU_UNKNOWN
} cpu_class_t;

/* FPU types */
typedef enum {
    FPU_NONE = 0,
    FPU_8087,
    FPU_80287,
    FPU_80287XL,
    FPU_80387,
    FPU_80387SX,
    FPU_80487,
    FPU_INTEGRATED,     /* 486DX, Pentium */
    FPU_UNKNOWN_PRESENT /* detected but unidentified */
} fpu_type_t;

/* FPU brand (where detectable) */
typedef enum {
    FPU_BRAND_NONE = 0,
    FPU_BRAND_INTEL,
    FPU_BRAND_IIT,       /* IIT 2C87, 3C87 */
    FPU_BRAND_CYRIX,     /* Cyrix FasMath 83D87, 83S87 */
    FPU_BRAND_ULSI,      /* ULSI Math-Co */
    FPU_BRAND_AMD,       /* AMD 80C287 */
    FPU_BRAND_UNKNOWN
} fpu_brand_t;

/* Video adapter classes */
typedef enum {
    VIDEO_MDA = 0,
    VIDEO_HERCULES,
    VIDEO_CGA,
    VIDEO_EGA,
    VIDEO_VGA,
    VIDEO_SVGA
} video_class_t;

/* SVGA chipset identification */
typedef enum {
    SVGA_NONE = 0,
    SVGA_TRIDENT_9000,
    SVGA_TRIDENT_9400,
    SVGA_CIRRUS_5422,
    SVGA_CIRRUS_5428,
    SVGA_CIRRUS_5434,
    SVGA_S3_805,
    SVGA_S3_928,
    SVGA_S3_TRIO64,
    SVGA_TSENG_ET3000,
    SVGA_TSENG_ET4000,
    SVGA_TSENG_ET4000W32,
    SVGA_ATI_VGAWONDER,
    SVGA_ATI_MACH8,
    SVGA_ATI_MACH32,
    SVGA_PARADISE_PVGA1A,
    SVGA_OAK_067,
    SVGA_OAK_087,
    SVGA_CT_65530,
    SVGA_CT_65545,
    SVGA_GENERIC_VESA,
    SVGA_UNKNOWN
} svga_chipset_t;

/* Video tier (resolution class) */
typedef enum {
    VTIER_MDA_TEXT = 0,    /* 720x350 text */
    VTIER_EGA,             /* 640x350x16 */
    VTIER_VGA_CLASSIC,     /* 320x200x256 Mode 13h */
    VTIER_SVGA_STANDARD,   /* 640x480x256 */
    VTIER_SVGA_ENHANCED,   /* 800x600x256 */
    VTIER_SVGA_WORKSTATION /* 1024x768x256 */
} video_tier_t;

/* Audio device flags (bitfield, multiple can coexist) */
#define AUDIO_NONE          0x0000
#define AUDIO_PCSPEAKER     0x0001
#define AUDIO_ADLIB         0x0002
#define AUDIO_ADLIB_GOLD    0x0004
#define AUDIO_SB_1X         0x0008
#define AUDIO_SB_PRO        0x0010
#define AUDIO_SB_16         0x0020
#define AUDIO_SB_AWE32      0x0040
#define AUDIO_SB_AWE64      0x0080
#define AUDIO_GUS_CLASSIC   0x0100
#define AUDIO_GUS_MAX       0x0200
#define AUDIO_GUS_ACE       0x0400
#define AUDIO_GUS_PNP       0x0800
#define AUDIO_MPU401        0x1000
#define AUDIO_TANDY         0x2000
#define AUDIO_DISNEY        0x4000
#define AUDIO_COVOX         0x8000

/* OPL chip type */
typedef enum {
    OPL_NONE = 0,
    OPL_OPL2,
    OPL_OPL3
} opl_type_t;

/* MIDI synth type (what's on the other end of MPU-401) */
typedef enum {
    MIDI_NONE = 0,
    MIDI_GENERIC_GM,
    MIDI_MT32,
    MIDI_CM32L,
    MIDI_SC55,
    MIDI_SC88,
    MIDI_SC88PRO,
    MIDI_DB50XG,
    MIDI_UNKNOWN
} midi_synth_t;

/* Sound Blaster config */
typedef struct {
    uint16_t  base;       /* I/O base (220h, 240h, etc.) */
    uint8_t   irq;
    uint8_t   dma_low;
    uint8_t   dma_high;   /* 16-bit DMA, SB16+ only */
    bool_t    has_asp;    /* ASP/CSP chip present */
} sb_config_t;

/* GUS config */
typedef struct {
    uint16_t  base;
    uint8_t   irq;
    uint8_t   dma;
    uint32_t  ram_kb;     /* On-board sample RAM in KB */
    bool_t    has_db;     /* Daughterboard present */
} gus_config_t;

/* NetISA status */
typedef enum {
    NETISA_NOT_FOUND = 0,
    NETISA_FOUND_NO_LINK,
    NETISA_FOUND_LINK_UP
} netisa_status_t;

/* VESA info */
typedef struct {
    uint8_t   version_major;
    uint8_t   version_minor;
    uint16_t  vram_kb;
    bool_t    has_lfb;         /* Linear framebuffer support */
    uint16_t  max_width;       /* Highest detected 256-color mode */
    uint16_t  max_height;
} vesa_info_t;

/* ---- The master hardware profile ---- */
/* This is the single structure that flows through the entire program. */
/* Detection fills it. The unlock matrix reads it. The boot screen renders it. */
/* The Hall of Recognition persists it. Settings uses it for lock/unlock display. */

typedef struct {
    /* CPU */
    cpu_class_t   cpu_class;
    uint16_t      cpu_mhz;           /* Measured clock in MHz */
    bool_t        cpu_overclocked;   /* Measured > nominal */
    uint16_t      cpu_nominal_mhz;   /* Nominal clock if known */

    /* FPU */
    fpu_type_t    fpu_type;
    fpu_brand_t   fpu_brand;
    char          fpu_brand_str[32]; /* Human-readable, e.g. "IIT 2C87-12" */

    /* Memory */
    uint16_t      mem_conv_kb;       /* Conventional (typically 640) */
    uint32_t      mem_xms_kb;        /* Extended (XMS) */
    uint32_t      mem_ems_kb;        /* Expanded (EMS) */

    /* Video */
    video_class_t video_class;
    svga_chipset_t svga_chipset;
    char          video_name[48];    /* Human-readable, e.g. "S3 Trio64, 2MB VRAM" */
    vesa_info_t   vesa;
    video_tier_t  video_tier;        /* Resolved tier after detection */

    /* Audio */
    uint32_t      audio_devices;     /* Bitfield of AUDIO_* flags */
    opl_type_t    opl_type;
    sb_config_t   sb;
    gus_config_t  gus;
    uint16_t      mpu_base;         /* MPU-401 I/O base, 0 if not found */
    midi_synth_t  midi_synth;

    /* NetISA */
    netisa_status_t netisa_status;
    uint16_t      netisa_base;
    char          netisa_fw_ver[16]; /* ESP32 firmware version string */

    /* Input */
    bool_t        has_mouse;
    bool_t        has_joystick;

    /* Meta */
    uint32_t      fingerprint;       /* Hash for change detection */
    char          detect_date[11];   /* YYYY-MM-DD of this detection */
} hw_profile_t;

/* ---- Unlock feature IDs ---- */
/* Each unlock is a unique ID. The unlock matrix maps hw_profile_t -> set of IDs. */

typedef enum {
    /* FPU unlocks */
    UNLOCK_FFT_256BIN = 0,
    UNLOCK_SINC_RESAMPLE,
    UNLOCK_16CH_MIXING,
    UNLOCK_PLASMA_VIS,
    UNLOCK_TUNNEL_VIS,
    UNLOCK_PARTICLE_VIS,
    UNLOCK_FIRE_VIS,
    UNLOCK_WIREFRAME_VIS,
    UNLOCK_KARAOKE,
    UNLOCK_PARAMETRIC_EQ,
    UNLOCK_CONVOLUTION_REVERB,
    UNLOCK_STEREO_WIDEN,
    UNLOCK_GAMMA_DITHER,
    UNLOCK_EXACT_MIXING,        /* Software quire */
    UNLOCK_ADAPTIVE_CORDIC,
    UNLOCK_LOG_EFFECTS,

    /* Video unlocks */
    UNLOCK_SVGA_1024,
    UNLOCK_SVGA_800,
    UNLOCK_SVGA_640,
    UNLOCK_ALBUM_ART_256,
    UNLOCK_ALBUM_ART_HIRES,
    UNLOCK_CHIPSET_ACCEL,

    /* Audio unlocks */
    UNLOCK_GUS_HW_MIX,
    UNLOCK_GUS_MAX_LINEIN,
    UNLOCK_GUS_DAUGHTERBOARD,
    UNLOCK_AWE_SOUNDFONT,
    UNLOCK_AWE_DSP_EFFECTS,
    UNLOCK_SB16_ASP,
    UNLOCK_MT32_MODE,
    UNLOCK_SC55_MODE,
    UNLOCK_ADLIB_GOLD_DAC,
    UNLOCK_ADLIB_GOLD_SURROUND,
    UNLOCK_PCSPEAKER_REALSOUND,

    /* Memory unlocks */
    UNLOCK_FULL_LIBRARY_INDEX,
    UNLOCK_VIS_PRELOAD,

    /* Network unlocks */
    UNLOCK_STREAMING,
    UNLOCK_SCROBBLING,
    UNLOCK_CLOUD_LIBRARY,
    UNLOCK_BT_OUTPUT,
    UNLOCK_AIRPLAY,

    /* FPU-less specific */
    UNLOCK_STOCHASTIC_PARTICLE,
    UNLOCK_BIPARTITE_FFT,

    /* Meta */
    UNLOCK_COUNT               /* Always last; gives array size */
} unlock_id_t;

/* Unlock state for a single feature */
typedef struct {
    unlock_id_t  id;
    const char  *name;          /* Short name, e.g. "256-bin FFT" */
    const char  *description;   /* One-liner for settings panel */
    const char  *requirement;   /* What hardware is needed, e.g. "requires FPU" */
    bool_t       unlocked;      /* TRUE if current hw_profile enables this */
    bool_t       ever_used;     /* Has the user ever activated this feature */
    char         first_used[11];/* YYYY-MM-DD, empty if never */
} unlock_entry_t;

#endif /* HEARO_H */
```

This header is the spine of the project. Every module includes it. Every module speaks in terms of `hw_profile_t` and `unlock_id_t`. Get this right and everything else composes cleanly.

### Phase 3: Detection Engine

Implement each detection module. Here are the specific techniques to use:

**CPU detection (cpu.c):**
- Try CPUID instruction first (486+ only). Wrap in exception handler for 286/386.
- For 286 vs 386: try toggling bit 14 (NT flag) in FLAGS register. 286 cannot.
- For 386SX vs 386DX: not reliably distinguishable from software. Report 386 generic unless CPUID says otherwise.
- For 486SX vs 486DX: CPUID if available, otherwise try FPU detection (486SX has no FPU).
- Clock measurement: use PIT channel 2 as reference. Count loop iterations over a known PIT interval. Convert to MHz. Compare against known nominal values to detect overclocking.
- Store everything in `hw_profile_t.cpu_*` fields.

**FPU detection (fpu.c):**
- FNINIT, then FNSTSW to AX. If status word is 0, FPU is present.
- Type detection: FSTENV, examine control word defaults. 287 has different defaults than 387+.
- Brand detection via instruction timing fingerprints: IIT 2C87 has faster FDIV than Intel 287 at same clock. Cyrix 83D87 has distinct FSCALE timing. This is approximate; use timing ratios, not absolute cycle counts, to remain clock-independent.
- Write human-readable brand string to `fpu_brand_str`.

**Video detection (video.c):**
- INT 10h AH=0Fh to get current mode.
- Probe for MDA: check 6845 CRTC at 3B4h.
- Probe for EGA: INT 10h AH=12h BL=10h.
- Probe for VGA: INT 10h AX=1A00h (VGA identify adapter call).
- Probe for VESA: INT 10h AX=4F00h. Parse VBE info block for version, VRAM, mode list.
- Walk VESA mode list: for each mode, call INT 10h AX=4F01h. Identify highest 256-color mode. Check for LFB support (VBE 2.0+).
- SVGA chipset fingerprinting: read signature registers at known I/O ports for S3, Cirrus, Trident, Tseng, etc. Build a lookup table. Each chipset gets its own `svga_chipset_t` value.
- Resolve `video_tier` from detected capabilities.

**Audio detection (audio.c):**
- Parse BLASTER environment variable for SB base/IRQ/DMA. Verify by probing the detected port (write to DSP reset port, read status).
- Detect SB version by reading DSP version command (E1h). Maps to SB 1.x / 2.x / Pro / 16 / AWE.
- ASP/CSP detection on SB16: probe at base+0Ch with specific handshake.
- Parse ULTRASND environment variable for GUS. Probe GUS RAM size.
- GUS daughterboard: probe via GUS command interface.
- AdLib: probe at 388h. Write timer control, read status. OPL2 vs OPL3: probe at 388h with OPL3 enable sequence.
- MPU-401: probe at 330h (default) and 300h. Send UART mode command (3Fh), check ACK (FEh).
- MIDI synth identification: if MPU-401 found, send SysEx identity request (F0 7E 7F 06 01 F7). Parse response.
- PC speaker: always present, set AUDIO_PCSPEAKER.
- Tandy: check for SN76496 at C0h.
- Disney/Covox: probe LPT1 with a known write pattern.

**Memory detection (memory.c):**
- Conventional: INT 12h.
- XMS: INT 2Fh AX=4300h. If present, call driver entry for XMS version and total free.
- EMS: try opening "EMMXXXX0" device. If present, INT 67h AH=42h for free page count.

**NetISA detection (netisa.c):**
- Probe at configured base (default 300h).
- For Phase 1, this is a STUB. Return `NETISA_NOT_FOUND` unless a `--stub-netisa` flag is passed, in which case return `NETISA_FOUND_LINK_UP` with mock firmware version.

**Input detection (input.c):**
- Mouse: INT 33h AX=0000h. If AX returns FFFFh, mouse is present.
- Joystick: INT 15h AH=84h DX=0000h. Check return.

### Phase 4: Unlock Matrix

Implement `unlock.c` to evaluate `hw_profile_t` against the unlock rules and populate a global array of `unlock_entry_t` structs.

The matrix is data-driven, not hardcoded if/else. Define unlock rules as a table:

```c
typedef struct {
    unlock_id_t  id;
    const char  *name;
    const char  *desc;
    const char  *requirement;
    /* Pointer to evaluation function */
    bool_t     (*check)(const hw_profile_t *hw);
} unlock_rule_t;

/* Example rules */
static bool_t check_fft_256(const hw_profile_t *hw) {
    return hw->fpu_type != FPU_NONE;
}

static bool_t check_stochastic(const hw_profile_t *hw) {
    return hw->fpu_type == FPU_NONE;
}

static bool_t check_gus_hw_mix(const hw_profile_t *hw) {
    return (hw->audio_devices & (AUDIO_GUS_CLASSIC | AUDIO_GUS_MAX)) != 0;
}
```

Each rule is a pure function of `hw_profile_t`. No side effects. Easily testable. Easily extensible.

Count total unlocks and expose as `unlock_get_count()` for the boot screen's "Your machine has unlocked N features" line.

### Phase 5: Boot Screen

This is the single most important visual artifact in the project. Get it right before anything else renders.

**`boot.c` implements:**

1. Clear screen (text mode, 80x25 or 80x43)
2. Render ASCII art HEARO logo (stored as const char array)
3. Render version and copyright line
4. Render hardware detection results line by line, with right-justified status words (detected / ENABLED / OK)
5. For each hardware category that enables unlocks, indent and list the unlock names
6. Render the unlock count line: "Your machine has unlocked N features. Press any key to begin."
7. Wait for keypress
8. Optional: play the fanfare chiptune (stub for now, just a brief PC speaker beep)

The boot screen must work identically on every video tier because it renders in text mode before the graphical UI initializes. It must look correct on MDA, CGA, EGA, VGA, and SVGA. It should use DOS CON output or direct video memory writes for speed, but test both paths.

The text layout should match the mockup in `hearo-design.md` Section 5.3.

### Phase 6: Hall of Recognition

**`hall.c` implements:**

Persistent storage in `HEARO.HAL` (plain text, one entry per line):

```
# HEARO Hall of Recognition
# Machine fingerprint: A7C3B291
2026-04-24 CPU Intel 80486DX2/66 detected
2026-04-24 FPU Integrated x87 detected ENABLED
2026-04-24 VIDEO S3 Trio64 2MB VRAM detected
2026-04-24 AUDIO Sound Blaster 16 220h IRQ 5 DMA 1 detected
2026-04-24 NETISA not found
```

On each boot:
1. Load `HEARO.HAL` if it exists.
2. Compare current `hw_profile_t.fingerprint` against stored fingerprint.
3. If fingerprint differs, diff the profiles and generate "what changed" entries.
4. Append new entries with current date.
5. Track first-boot date and boot count.
6. Compute lifetime stats (hours played, tracks played, etc.) from a separate `HEARO.STA` file.

The Hall viewer is a scrollable text display accessible from the main menu. Phase 1 implementation is the data layer; the viewer UI comes later.

### Phase 7: Configuration System

**`config.c` implements:**

INI-style reader/writer for `HEARO.CFG`. Sections: `[hardware]`, `[ui]`, `[playback]`, `[network]`, `[library]`, `[recognition]`, `[advanced]`.

**`cmdline.c` implements:**

Parse argc/argv for `/flags`. Priority: command-line flags override config file values.

Key flags for Phase 1:
```
/SAFE            Conservative mode
/REDETECT        Force re-detection
/BENCHMARK       Run benchmark and exit (stub for now)
/HALL            Show Hall of Recognition and exit
/UNLOCKS         Print unlock matrix and exit
/VIDEO=mode      Override video tier
/VERSION         Print version and exit
/DEBUG           Verbose logging to HEARO.LOG
```

### Phase 8: Novel Arithmetic Foundation

**`cordic.c` implements:**

Software CORDIC with configurable iteration count. Public API:

```c
/* Compute sine and cosine simultaneously */
/* iterations: 8-24, controls precision/speed tradeoff */
void cordic_sincos(int32_t angle_fixed, int iterations,
                   int32_t *sin_out, int32_t *cos_out);

/* FPU-accelerated version (uses FPU for shift accumulation) */
void cordic_sincos_fpu(int32_t angle_fixed, int iterations,
                       int32_t *sin_out, int32_t *cos_out);
```

Fixed-point format: 16.16 (16 integer bits, 16 fractional bits) for 286 compatibility. The CORDIC angle table (arctangent values for each iteration) is a const array, not computed at runtime.

**`bipartite.c` implements:**

Two-table lookup for sine, cosine, log2, exp2. Tables are pre-generated and stored in `bipartite_tables.c` as const arrays. Public API:

```c
int16_t bipartite_sin(uint16_t angle);   /* 8-10 bit precision */
int16_t bipartite_cos(uint16_t angle);
int16_t bipartite_log2(uint16_t x);
int16_t bipartite_exp2(int16_t x);
```

**`quire.c` implements:**

256-bit fixed-point accumulator. Four 64-bit words in XMS (or in conventional memory if XMS is unavailable, with reduced 128-bit quire). Public API:

```c
typedef struct {
    uint32_t words[8];    /* 256 bits as 8 x 32-bit words */
} quire_t;

void quire_clear(quire_t *q);
void quire_add_product(quire_t *q, int32_t a, int32_t b);
int32_t quire_round(const quire_t *q, int shift);
```

The `quire_add_product` function computes a*b as a 64-bit intermediate and adds it to the 256-bit accumulator with carry propagation. This is the inner loop of the tracker mixer. The FPU is used for the multiplication if present; otherwise pure integer math.

### Phase 9: Test Programs

Build standalone test executables that validate each subsystem independently:

- **TESTDET.EXE:** Run detection, print all `hw_profile_t` fields, exit. The first thing you run on any machine.
- **TESTBOOT.EXE:** Run detection, render boot screen, wait for keypress, exit. The first screenshot.
- **TESTCORD.EXE:** Run CORDIC at iterations 8, 12, 16, 20, 24. Print accuracy vs libc sin/cos. Print cycles per call at each iteration count.
- **TESTQUIR.EXE:** Accumulate 1000 random products in quire vs naive float sum. Print error comparison.
- **TESTBIP.EXE:** Evaluate bipartite sin/cos at 1000 points. Print max error vs libc.

### Phase 10: Makefile

Build both real-mode and protected-mode executables. Key targets:

```makefile
# HEARO Makefile for OpenWatcom wmake
# Usage: wmake [target]
#   wmake           - build HEARO.EXE (real-mode, 286+)
#   wmake xm        - build HEAROXM.EXE (protected-mode, 386+)
#   wmake tests     - build all test executables
#   wmake clean     - remove build artifacts
#   wmake all       - build everything

CC16 = wcc
CC32 = wcc386
ASM = nasm
LINK = wlink

# Real-mode: 286, large model, optimize for size
CFLAGS16 = -2 -ml -os -wx -zq

# Protected-mode: 386, flat model, register convention, optimize for speed
CFLAGS32 = -3r -mf -ox -wx -zq
```

## What NOT to Build Yet

Do not implement any of the following in this scaffolding phase:

- Audio playback (no mixer, no decoder, no DMA). Audio drivers are stubs with detection only.
- File format decoders (no MOD, no WAV, no MIDI). These come after the foundation.
- Visualizers beyond the basic ASCII spectrum concept. The math library enables them; the visualizers themselves come later.
- Graphical UI rendering (Mode 13h, SVGA). The boot screen is text mode. The graphical UI tiers come after the foundation validates.
- Any NetISA networking. The stub layer mocks it.
- Library scanning, playlist management, streaming, scrobbling.
- Skin/theme system.

The goal of this scaffold is: a codebase where `TESTDET.EXE` correctly identifies every piece of hardware in the machine, `TESTBOOT.EXE` renders a boot screen that makes the user feel seen, and `TESTCORD.EXE` / `TESTQUIR.EXE` / `TESTBIP.EXE` prove the novel arithmetic works. Everything else builds on this foundation.

## Testing Strategy

All code must compile and run in DOSBox-X. Configure DOSBox-X with these machine types to test each tier:

```ini
# 286 test
[cpu]
cputype=286
cycles=12000

# 386DX test
[cpu]
cputype=386
cycles=33000

# 486DX2 test
[cpu]
cputype=486_prefetch
cycles=66000
```

DOSBox-X emulates Sound Blaster, GUS, AdLib, MPU-401, and various video modes. Detection code should work in emulation, with a note in the output when it suspects emulation rather than real hardware (DOSBox has detectable fingerprints via specific port behaviors).

Test on real hardware when available, but do not block development on real hardware access.

## Definition of Done for This Scaffold

The scaffold is complete when:

1. `wmake` produces `HEARO.EXE` without errors or warnings
2. `wmake tests` produces all five test executables
3. `TESTDET.EXE` runs in DOSBox-X and correctly reports CPU, FPU, video, audio, and memory
4. `TESTBOOT.EXE` renders a boot screen matching the design doc mockup, with unlock counts
5. `TESTCORD.EXE` demonstrates adaptive-precision CORDIC with measurable accuracy at each iteration count
6. `TESTQUIR.EXE` demonstrates quire accumulation with measurably less error than naive summation
7. `TESTBIP.EXE` demonstrates bipartite table accuracy within 8-10 bit precision
8. All source files have the MIT license header
9. All commits include `Assisted-by:` tags where applicable
10. `HEARO.EXE` launches, runs detection, renders boot screen, and exits cleanly

This is the foundation. Everything else in the design document builds on it.
