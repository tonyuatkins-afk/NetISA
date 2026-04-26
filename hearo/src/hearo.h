/*
 * hearo.h - HEARO global types, constants, and master data structures
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_H
#define HEARO_H

#define HEARO_VER_MAJOR   1
#define HEARO_VER_MINOR   0
#define HEARO_VER_PATCH   0
#define HEARO_VER_STRING  "1.0.0"
#define HEARO_COPYRIGHT   "(c) 2026 Tony Atkins, MIT License"

/* Portable types (C89) */
typedef unsigned char  u8;   typedef signed char  s8;
typedef unsigned short u16;  typedef signed short s16;
typedef unsigned long  u32;  typedef signed long  s32;
typedef int hbool;
#define HTRUE 1
#define HFALSE 0

/* CPU */
typedef enum {
    CPU_8088=0, CPU_8086, CPU_80186, CPU_80286,
    CPU_80386SX, CPU_80386DX, CPU_80486SX, CPU_80486DX,
    CPU_PENTIUM, CPU_UNKNOWN
} cpu_class_t;

/* FPU */
typedef enum {
    FPU_NONE=0, FPU_8087, FPU_80287, FPU_80287XL,
    FPU_80387, FPU_80387SX, FPU_80487,
    FPU_INTEGRATED, FPU_PRESENT_UNKNOWN
} fpu_type_t;

typedef enum {
    FPUB_NONE=0, FPUB_INTEL, FPUB_IIT, FPUB_CYRIX,
    FPUB_ULSI, FPUB_AMD, FPUB_UNKNOWN
} fpu_brand_t;

/* Video */
typedef enum {
    VID_MDA=0, VID_HERCULES, VID_CGA, VID_EGA, VID_VGA, VID_SVGA
} video_class_t;

typedef enum {
    SVGA_NONE=0,
    SVGA_TRIDENT_9000, SVGA_TRIDENT_9400,
    SVGA_CIRRUS_5422, SVGA_CIRRUS_5428, SVGA_CIRRUS_5434,
    SVGA_S3_805, SVGA_S3_928, SVGA_S3_TRIO64,
    SVGA_TSENG_ET3000, SVGA_TSENG_ET4000, SVGA_TSENG_ET4000W32,
    SVGA_ATI_WONDER, SVGA_ATI_MACH8, SVGA_ATI_MACH32,
    SVGA_PARADISE_PVGA1A, SVGA_OAK_067, SVGA_OAK_087,
    SVGA_CT_65530, SVGA_CT_65545,
    SVGA_GENERIC_VESA, SVGA_UNKNOWN
} svga_chipset_t;

typedef enum {
    VTIER_MDA=0, VTIER_EGA, VTIER_VGA_CLASSIC,
    VTIER_SVGA_640, VTIER_SVGA_800, VTIER_SVGA_1024
} video_tier_t;

typedef struct {
    u8 ver_major, ver_minor; u16 vram_kb;
    hbool has_lfb; u16 max_w, max_h;
} vesa_info_t;

/* Audio devices (bitfield) */
#define AUD_PCSPEAKER   0x00000001UL
#define AUD_TANDY       0x00000002UL
#define AUD_COVOX       0x00000004UL
#define AUD_DISNEY      0x00000008UL
#define AUD_ADLIB       0x00000010UL
#define AUD_ADLIB_GOLD  0x00000020UL
#define AUD_SB_1X       0x00000040UL
#define AUD_SB_20       0x00000080UL
#define AUD_SB_PRO      0x00000100UL
#define AUD_SB_PRO2     0x00000200UL
#define AUD_SB_16       0x00000400UL
#define AUD_SB_16ASP    0x00000800UL
#define AUD_SB_AWE32    0x00001000UL
#define AUD_SB_AWE64    0x00002000UL
#define AUD_GUS         0x00004000UL
#define AUD_GUS_MAX     0x00008000UL
#define AUD_GUS_ACE     0x00010000UL
#define AUD_GUS_PNP     0x00020000UL
#define AUD_MPU401      0x00040000UL
#define AUD_ENSONIQ     0x00080000UL
#define AUD_PAS16       0x00100000UL
#define AUD_ESS         0x00200000UL
#define AUD_TBEACH      0x00400000UL
#define AUD_OPTI_MAD16  0x00800000UL    /* OPTi 82C928 / 82C929 MAD16 family */

typedef enum { OPL_NONE=0, OPL_OPL2, OPL_OPL3, OPL_CQM } opl_type_t;

typedef enum {
    MIDI_NONE=0, MIDI_GM, MIDI_MT32, MIDI_CM32L,
    MIDI_SC55, MIDI_SC55MK2, MIDI_SC88, MIDI_SC88PRO,
    MIDI_DB50XG, MIDI_SCB55, MIDI_UNKNOWN
} midi_synth_t;

typedef struct { u16 base; u8 irq, dma_lo, dma_hi; hbool has_asp; u8 dsp_major, dsp_minor; } sb_config_t;
typedef struct { u16 base; u8 irq, dma; u32 ram_kb; hbool has_db, has_codec; } gus_config_t;
typedef enum { NISA_NOT_FOUND=0, NISA_NO_LINK, NISA_LINK_UP } netisa_status_t;

/* Master Hardware Profile */
typedef struct {
    cpu_class_t cpu_class; u16 cpu_mhz, cpu_nominal_mhz; hbool cpu_overclock;
    fpu_type_t fpu_type; fpu_brand_t fpu_brand; char fpu_name[32];
    u16 mem_conv_kb; u32 mem_xms_kb, mem_ems_kb;
    video_class_t vid_class; svga_chipset_t svga_chip; char vid_name[48];
    vesa_info_t vesa; video_tier_t vid_tier;
    u32 aud_devices; opl_type_t opl; sb_config_t sb; gus_config_t gus;
    u16 mpu_base; midi_synth_t midi_synth; char midi_name[32];
    char aud_cards[8][48]; u8 aud_card_count;  /* human-readable card names */
    netisa_status_t nisa_status; u16 nisa_base; char nisa_fw[16];
    hbool has_mouse, has_joystick;
    u32 fingerprint; char detect_date[11];
} hw_profile_t;

/* Unlock IDs */
typedef enum {
    /* FPU */
    UL_FFT_256=0, UL_SINC_RESAMPLE, UL_16CH_MIX,
    UL_PLASMA, UL_TUNNEL, UL_PARTICLE, UL_FIRE, UL_WIREFRAME,
    UL_KARAOKE, UL_PARAM_EQ, UL_CONV_REVERB, UL_STEREO_WIDE,
    UL_GAMMA_DITHER, UL_EXACT_MIX, UL_ADAPTIVE_CORDIC, UL_LOG_EFFECTS,
    /* Video */
    UL_SVGA_1024, UL_SVGA_800, UL_SVGA_640,
    UL_ART_256, UL_ART_HIRES, UL_CHIPSET_ACCEL,
    /* Audio (expanded for all 24 devices) */
    UL_REALSOUND, UL_TANDY_PSG, UL_COVOX, UL_DISNEY,
    UL_OPL2_MIDI, UL_OPL3_MIDI, UL_CQM_FM,
    UL_SB_PCM, UL_SB_AUTOINIT, UL_SB_STEREO,
    UL_SB16_PCM, UL_SB16_ASP, UL_QSOUND, UL_FULL_DUPLEX,
    UL_GUS_HW_MIX, UL_GUS_32VOICE, UL_GUS_DRAM,
    UL_GUS_MAX_LINEIN, UL_GUS_MAX_CODEC,
    UL_ENSONIQ_WT, UL_ENSONIQ_DAC, UL_ENSONIQ_FILTER,
    UL_AWE_SFONT, UL_AWE_DSP, UL_AWE_32VOICE, UL_AWE_RAM,
    UL_MT32_MODE, UL_SC55_MODE, UL_XG_MODE,
    UL_AGOLD_DAC, UL_AGOLD_SURROUND,
    UL_MPU401, UL_DAUGHTER,
    UL_PAS16, UL_ESS_NATIVE, UL_TBEACH,
    /* Memory */
    UL_FULL_LIBRARY, UL_VIS_PRELOAD,
    /* Network */
    UL_STREAMING, UL_SCROBBLE, UL_CLOUD_LIB, UL_BT_OUT, UL_AIRPLAY,
    /* FPU-less exclusives */
    UL_STOCHASTIC, UL_BIPARTITE_FFT,
    UL_COUNT
} unlock_id_t;

typedef struct {
    unlock_id_t id; const char *name, *desc, *requirement;
    hbool unlocked, ever_used; char first_used[11];
} unlock_entry_t;

/* UI color attributes */
#define ATTR_NORMAL     0x07  /* light gray on black */
#define ATTR_BRIGHT     0x0F  /* bright white on black */
#define ATTR_DIM        0x08  /* dark gray on black */
#define ATTR_CYAN       0x0B  /* bright cyan on black */
#define ATTR_GREEN      0x0A  /* bright green on black */
#define ATTR_YELLOW     0x0E  /* yellow on black */
#define ATTR_RED        0x0C  /* bright red on black */
#define ATTR_TITLE_BAR  0x1F  /* bright white on blue */
#define ATTR_STATUS_BAR 0x30  /* black on cyan */
#define ATTR_MENU_ITEM  0x70  /* black on light gray */
#define ATTR_MENU_HOT   0x74  /* red on light gray (hotkey) */
#define ATTR_SELECTED   0x1E  /* yellow on blue */
#define ATTR_ENABLED    0x0A  /* bright green */
#define ATTR_DISABLED   0x08  /* dark gray */
#define ATTR_LOCKED     0x08  /* dark gray for locked features */
#define ATTR_MONO       0x07  /* standard mono attribute */
#define ATTR_MONO_BRIGHT 0x0F /* bright mono */

/* Key codes */
#define KEY_ESC    0x011B
#define KEY_ENTER  0x1C0D
#define KEY_TAB    0x0F09
#define KEY_SPACE  0x3920
#define KEY_UP     0x4800
#define KEY_DOWN   0x5000
#define KEY_LEFT   0x4B00
#define KEY_RIGHT  0x4D00
#define KEY_PGUP   0x4900
#define KEY_PGDN   0x5100
#define KEY_HOME   0x4700
#define KEY_END    0x4F00
#define KEY_F1     0x3B00
#define KEY_F2     0x3C00
#define KEY_F3     0x3D00
#define KEY_F4     0x3E00
#define KEY_F5     0x3F00
#define KEY_F10    0x4400
#define KEY_F12    0x8600
#define KEY_ALT_F  0x2100
#define KEY_ALT_P  0x1900
#define KEY_ALT_V  0x2F00
#define KEY_ALT_S  0x1F00
#define KEY_ALT_H  0x2300
#define KEY_ALT_X  0x2D00

#endif /* HEARO_H */
