/*
 * unlock/unlock.c - Feature unlock matrix.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * The structural heart of HEARO. Every unlock is a row in the rule table with
 * a pure function check against the master hw_profile_t. Adding a new feature
 * is one line.
 *
 * unlock_evaluate runs every check and stamps the runtime entries[]; UI code
 * reads entries[], not the rules[].
 */
#include "unlock.h"
#include <string.h>

/* ---- Pure check functions ---- */

static hbool chk_always   (const hw_profile_t *h) { (void)h; return HTRUE; }
static hbool chk_has_fpu  (const hw_profile_t *h) { return (h->fpu_type != FPU_NONE) ? HTRUE : HFALSE; }
static hbool chk_no_fpu   (const hw_profile_t *h) { return (h->fpu_type == FPU_NONE) ? HTRUE : HFALSE; }
static hbool chk_486plus  (const hw_profile_t *h) { return (h->cpu_class >= CPU_80486SX) ? HTRUE : HFALSE; }
static hbool chk_pentium  (const hw_profile_t *h) { return (h->cpu_class >= CPU_PENTIUM) ? HTRUE : HFALSE; }
static hbool chk_fpu_xms  (const hw_profile_t *h) { return chk_has_fpu(h) && h->mem_xms_kb >= 256UL ? HTRUE : HFALSE; }
static hbool chk_xms_4mb  (const hw_profile_t *h) { return h->mem_xms_kb >= 4096UL ? HTRUE : HFALSE; }
static hbool chk_xms_8mb  (const hw_profile_t *h) { return h->mem_xms_kb >= 8192UL ? HTRUE : HFALSE; }

static hbool chk_fpu_486  (const hw_profile_t *h) { return chk_has_fpu(h) && chk_486plus(h); }

/* Audio */
static hbool chk_pc_speaker(const hw_profile_t *h) { return (h->aud_devices & AUD_PCSPEAKER) ? HTRUE : HFALSE; }
static hbool chk_tandy   (const hw_profile_t *h) { return (h->aud_devices & AUD_TANDY) ? HTRUE : HFALSE; }
static hbool chk_covox   (const hw_profile_t *h) { return (h->aud_devices & AUD_COVOX) ? HTRUE : HFALSE; }
static hbool chk_disney  (const hw_profile_t *h) { return (h->aud_devices & AUD_DISNEY) ? HTRUE : HFALSE; }
static hbool chk_opl2    (const hw_profile_t *h) { return (h->opl == OPL_OPL2 || h->opl == OPL_OPL3) ? HTRUE : HFALSE; }
static hbool chk_opl3    (const hw_profile_t *h) { return (h->opl == OPL_OPL3) ? HTRUE : HFALSE; }
static hbool chk_cqm     (const hw_profile_t *h) { return (h->opl == OPL_CQM) ? HTRUE : HFALSE; }
static hbool chk_sb_any  (const hw_profile_t *h) {
    return (h->aud_devices & (AUD_SB_1X|AUD_SB_20|AUD_SB_PRO|AUD_SB_PRO2|
                              AUD_SB_16|AUD_SB_16ASP|AUD_SB_AWE32|AUD_SB_AWE64)) ? HTRUE : HFALSE;
}
static hbool chk_sb_autoinit(const hw_profile_t *h) {
    return (h->aud_devices & (AUD_SB_20|AUD_SB_PRO|AUD_SB_PRO2|AUD_SB_16|AUD_SB_16ASP|AUD_SB_AWE32|AUD_SB_AWE64)) ? HTRUE : HFALSE;
}
static hbool chk_sb_stereo(const hw_profile_t *h) {
    return (h->aud_devices & (AUD_SB_PRO|AUD_SB_PRO2|AUD_SB_16|AUD_SB_16ASP|AUD_SB_AWE32|AUD_SB_AWE64)) ? HTRUE : HFALSE;
}
static hbool chk_sb16    (const hw_profile_t *h) {
    return (h->aud_devices & (AUD_SB_16|AUD_SB_16ASP|AUD_SB_AWE32|AUD_SB_AWE64)) ? HTRUE : HFALSE;
}
static hbool chk_asp     (const hw_profile_t *h) { return (h->aud_devices & AUD_SB_16ASP) ? HTRUE : HFALSE; }
static hbool chk_full_dup(const hw_profile_t *h) { return chk_sb16(h) && (h->sb.dsp_major >= 4 && h->sb.dsp_minor >= 13) ? HTRUE : HFALSE; }
static hbool chk_gus     (const hw_profile_t *h) { return (h->aud_devices & (AUD_GUS|AUD_GUS_MAX|AUD_GUS_ACE|AUD_GUS_PNP)) ? HTRUE : HFALSE; }
static hbool chk_gus_ram (const hw_profile_t *h) { return chk_gus(h) && h->gus.ram_kb >= 1024 ? HTRUE : HFALSE; }
static hbool chk_gus_max (const hw_profile_t *h) { return (h->aud_devices & AUD_GUS_MAX) ? HTRUE : HFALSE; }
static hbool chk_awe     (const hw_profile_t *h) { return (h->aud_devices & (AUD_SB_AWE32|AUD_SB_AWE64)) ? HTRUE : HFALSE; }
static hbool chk_awe_ram (const hw_profile_t *h) { return chk_awe(h); /* upgraded later by SIMM probe */ }
static hbool chk_mt32    (const hw_profile_t *h) { return (h->midi_synth == MIDI_MT32 || h->midi_synth == MIDI_CM32L) ? HTRUE : HFALSE; }
static hbool chk_sc55    (const hw_profile_t *h) {
    return (h->midi_synth == MIDI_SC55 || h->midi_synth == MIDI_SC55MK2 ||
            h->midi_synth == MIDI_SC88 || h->midi_synth == MIDI_SC88PRO ||
            h->midi_synth == MIDI_SCB55) ? HTRUE : HFALSE;
}
static hbool chk_xg      (const hw_profile_t *h) { return (h->midi_synth == MIDI_DB50XG) ? HTRUE : HFALSE; }
static hbool chk_agold   (const hw_profile_t *h) { return (h->aud_devices & AUD_ADLIB_GOLD) ? HTRUE : HFALSE; }
static hbool chk_mpu     (const hw_profile_t *h) { return (h->aud_devices & AUD_MPU401) ? HTRUE : HFALSE; }
static hbool chk_daughter(const hw_profile_t *h) { return chk_mpu(h) && h->midi_synth != MIDI_NONE ? HTRUE : HFALSE; }
static hbool chk_pas16   (const hw_profile_t *h) { return (h->aud_devices & AUD_PAS16) ? HTRUE : HFALSE; }
static hbool chk_ess     (const hw_profile_t *h) { return (h->aud_devices & AUD_ESS) ? HTRUE : HFALSE; }
static hbool chk_tbeach  (const hw_profile_t *h) { return (h->aud_devices & AUD_TBEACH) ? HTRUE : HFALSE; }
static hbool chk_ensoniq (const hw_profile_t *h) { return (h->aud_devices & AUD_ENSONIQ) ? HTRUE : HFALSE; }

/* Video */
static hbool chk_svga_640 (const hw_profile_t *h) { return h->vid_tier >= VTIER_SVGA_640 ? HTRUE : HFALSE; }
static hbool chk_svga_800 (const hw_profile_t *h) { return h->vid_tier >= VTIER_SVGA_800 ? HTRUE : HFALSE; }
static hbool chk_svga_1024(const hw_profile_t *h) { return h->vid_tier >= VTIER_SVGA_1024 ? HTRUE : HFALSE; }
static hbool chk_art_256  (const hw_profile_t *h) { return h->vid_tier >= VTIER_VGA_CLASSIC ? HTRUE : HFALSE; }
static hbool chk_art_hires(const hw_profile_t *h) { return h->vid_tier >= VTIER_SVGA_640 && h->vesa.vram_kb >= 1024 ? HTRUE : HFALSE; }
static hbool chk_chipset  (const hw_profile_t *h) { return (h->svga_chip != SVGA_NONE && h->svga_chip != SVGA_GENERIC_VESA && h->svga_chip != SVGA_UNKNOWN) ? HTRUE : HFALSE; }

/* Network (NetISA) */
static hbool chk_netisa   (const hw_profile_t *h) { return (h->nisa_status == NISA_LINK_UP) ? HTRUE : HFALSE; }

/* ---- Rule table ---- */
static const unlock_rule_t rules[] = {
    /* FPU enabled visualisers and DSP */
    { UL_FFT_256,         "256-bin FFT",          "Full spectrum analyzer",       "requires FPU",                 chk_has_fpu },
    { UL_SINC_RESAMPLE,   "Sinc resampling",      "High-quality resampling",      "requires FPU",                 chk_has_fpu },
    { UL_16CH_MIX,        "16+ channel mixing",   "Tracker mixing with ramping",  "requires FPU",                 chk_has_fpu },
    { UL_PLASMA,          "Plasma",               "Sine-field demoscene effect",  "requires FPU",                 chk_has_fpu },
    { UL_TUNNEL,          "Tunnel",               "Z-shaded tunnel effect",       "requires FPU + 386+",          chk_has_fpu },
    { UL_PARTICLE,        "Particle system",      "Audio-reactive particles",     "requires 486+ FPU",            chk_fpu_486 },
    { UL_FIRE,            "Fire",                 "Classic fire effect",          "requires FPU",                 chk_has_fpu },
    { UL_WIREFRAME,       "Wireframe 3D",         "Beat-driven 3D wireframe",     "requires 486+ FPU",            chk_fpu_486 },
    { UL_KARAOKE,         "Karaoke vocal removal","Mid-side cancellation",        "requires FPU",                 chk_has_fpu },
    { UL_PARAM_EQ,        "Parametric EQ",        "10-band parametric EQ",        "requires FPU",                 chk_has_fpu },
    { UL_CONV_REVERB,     "Convolution reverb",   "Impulse-response reverb",      "requires Pentium FPU",         chk_pentium },
    { UL_STEREO_WIDE,     "Stereo widening",      "Haas-effect widener",          "requires FPU",                 chk_has_fpu },
    { UL_GAMMA_DITHER,    "Gamma-correct dither", "sRGB-aware album art dither",  "requires FPU",                 chk_has_fpu },
    { UL_EXACT_MIX,       "Exact mixing",         "Software quire accumulator",   "requires FPU + 256K XMS",      chk_fpu_xms },
    { UL_ADAPTIVE_CORDIC, "Adaptive CORDIC",      "Dynamic-precision visualizers","requires FPU",                 chk_has_fpu },
    { UL_LOG_EFFECTS,     "Log-domain effects",   "Multiplication-light DSP",     "requires FPU",                 chk_has_fpu },

    /* Video */
    { UL_SVGA_1024,       "1024x768 SVGA",        "Full Workstation tier UI",     "requires SVGA 1024x768",       chk_svga_1024 },
    { UL_SVGA_800,        "800x600 SVGA",         "Enhanced tier UI",             "requires SVGA 800x600",        chk_svga_800 },
    { UL_SVGA_640,        "640x480 SVGA",         "Standard tier UI",             "requires SVGA 640x480",        chk_svga_640 },
    { UL_ART_256,         "256-color album art",  "Cover art panel",              "requires VGA",                 chk_art_256 },
    { UL_ART_HIRES,       "High-res album art",   "1024x1024 covers",             "requires SVGA + 1MB VRAM",     chk_art_hires },
    { UL_CHIPSET_ACCEL,   "Chipset blits",        "Chipset-specific accel",       "requires recognized SVGA",     chk_chipset },

    /* Audio */
    { UL_REALSOUND,       "RealSound PWM",        "PC Speaker PWM playback",      NULL,                           chk_pc_speaker },
    { UL_TANDY_PSG,       "Tandy / PCjr PSG",     "3-voice + noise mixer",        "requires Tandy or PCjr",       chk_tandy },
    { UL_COVOX,           "Covox",                "8-bit LPT DAC",                "requires Covox on LPT",        chk_covox },
    { UL_DISNEY,          "Disney Sound Source",  "7 kHz LPT DAC w/ FIFO",        "requires Disney on LPT",       chk_disney },
    { UL_OPL2_MIDI,       "OPL2 MIDI",            "9-channel FM MIDI",            "requires AdLib/SB",            chk_opl2 },
    { UL_OPL3_MIDI,       "OPL3 MIDI",            "18-channel FM MIDI",           "requires OPL3 device",         chk_opl3 },
    { UL_CQM_FM,          "CQM FM",               "AWE64 software OPL3",          "requires AWE64",               chk_cqm },
    { UL_SB_PCM,          "Sound Blaster PCM",    "8-bit PCM playback",           "requires Sound Blaster",       chk_sb_any },
    { UL_SB_AUTOINIT,     "SB auto-init DMA",     "Glitch-free DMA chain",        "requires SB 2.0+",             chk_sb_autoinit },
    { UL_SB_STEREO,       "SB stereo PCM",        "8-bit stereo playback",        "requires SB Pro+",             chk_sb_stereo },
    { UL_SB16_PCM,        "SB16 16-bit PCM",      "16-bit stereo playback",       "requires SB16",                chk_sb16 },
    { UL_SB16_ASP,        "SB16 ASP",             "Hardware DSP effects",         "requires SB16 ASP",            chk_asp },
    { UL_QSOUND,          "QSound 3D audio",      "Positional audio via ASP",     "requires SB16 ASP",            chk_asp },
    { UL_FULL_DUPLEX,     "Full duplex",          "Simultaneous play+record",     "requires SB16 DSP 4.13+",      chk_full_dup },
    { UL_GUS_HW_MIX,      "GUS hardware mixing",  "32-voice zero-CPU mixing",     "requires GUS",                 chk_gus },
    { UL_GUS_32VOICE,     "GUS 32 voices",        "Full GF1 voice complement",    "requires GUS",                 chk_gus },
    { UL_GUS_DRAM,        "GUS 1MB DRAM tier",    "Full sample loading",          "requires GUS w/ 1MB+ DRAM",    chk_gus_ram },
    { UL_GUS_MAX_LINEIN,  "GUS MAX line-in",      "16-bit recording/passthrough", "requires GUS MAX",             chk_gus_max },
    { UL_GUS_MAX_CODEC,   "GUS MAX codec",        "CS4231 16-bit playback",       "requires GUS MAX",             chk_gus_max },
    { UL_ENSONIQ_WT,      "Ensoniq wavetable",    "OTTO ES5506 sample synth",     "requires SoundScape",          chk_ensoniq },
    { UL_ENSONIQ_DAC,     "Ensoniq DAC",          "16-bit stereo PCM",            "requires SoundScape",          chk_ensoniq },
    { UL_ENSONIQ_FILTER,  "Ensoniq filters",      "Per-voice resonant filter",    "requires SoundScape",          chk_ensoniq },
    { UL_AWE_SFONT,       "SoundFont loading",    "Custom instrument banks",      "requires AWE32/64",            chk_awe },
    { UL_AWE_DSP,         "EMU8000 DSP",          "Reverb, chorus, EQ on chip",   "requires AWE32/64",            chk_awe },
    { UL_AWE_32VOICE,     "EMU8000 32 voices",    "Full wavetable poly",          "requires AWE32/64",            chk_awe },
    { UL_AWE_RAM,         "AWE SIMM RAM",         "Up to 28MB SoundFont RAM",     "requires AWE32 w/ SIMMs",      chk_awe_ram },
    { UL_MT32_MODE,       "MT-32 mode",           "LA synthesis timbres",         "requires MT-32/CM-32L",        chk_mt32 },
    { UL_SC55_MODE,       "SoundCanvas mode",     "GS / GM / drum maps",          "requires SC-55+",              chk_sc55 },
    { UL_XG_MODE,         "XG mode",              "Yamaha XG voices",             "requires DB50XG/MU",           chk_xg },
    { UL_AGOLD_DAC,       "AdLib Gold DAC",       "12-bit stereo playback",       "requires AdLib Gold",          chk_agold },
    { UL_AGOLD_SURROUND,  "AdLib Gold surround",  "Quadraphonic surround mode",   "requires AdLib Gold + module", chk_agold },
    { UL_MPU401,          "MPU-401 MIDI",         "External MIDI gateway",        "requires MPU-401",             chk_mpu },
    { UL_DAUGHTER,        "Daughterboard MIDI",   "WaveBlaster header synth",     "requires SB16 + daughtercard", chk_daughter },
    { UL_PAS16,           "Pro Audio Spectrum 16","Native 16-bit + OPL3 stereo",  "requires PAS-16",              chk_pas16 },
    { UL_ESS_NATIVE,      "ESS native mode",      "ESFM and 6-bit ADPCM",         "requires ESS AudioDrive",      chk_ess },
    { UL_TBEACH,          "Turtle Beach DSP",     "56001 DSP wavetable",          "requires TBeach MultiSound",   chk_tbeach },

    /* Memory */
    { UL_FULL_LIBRARY,    "Full library index",   "100K+ track library cache",    "requires 4MB+ XMS",            chk_xms_4mb },
    { UL_VIS_PRELOAD,     "Visualizer preload",   "All visualizers in RAM",       "requires 8MB+ XMS",            chk_xms_8mb },

    /* Network */
    { UL_STREAMING,       "Streaming via NetISA", "Internet radio, Bandcamp",     "requires NetISA card",         chk_netisa },
    { UL_SCROBBLE,        "Scrobbling",           "last.fm-style play tracking",  "requires NetISA card",         chk_netisa },
    { UL_CLOUD_LIB,       "Cloud library",        "Mirror local index to cloud",  "requires NetISA card",         chk_netisa },
    { UL_BT_OUT,          "Bluetooth output",     "BT speaker/headset target",    "requires NetISA card",         chk_netisa },
    { UL_AIRPLAY,         "AirPlay target",       "Receive from Apple devices",   "requires NetISA card",         chk_netisa },

    /* FPU-less exclusives */
    { UL_STOCHASTIC,      "Stochastic particles", "Probability-based visualizer", "FPU-less systems only",        chk_no_fpu },
    { UL_BIPARTITE_FFT,   "64-bin bipartite FFT", "Fast spectrum w/o multiplies", NULL,                           chk_always }
};

#define RULE_COUNT (sizeof(rules) / sizeof(rules[0]))

static unlock_entry_t entries[UL_COUNT];
static u16 enabled_count = 0;
static hbool initialised = HFALSE;

static void init_entries(void)
{
    u16 i;
    for (i = 0; i < UL_COUNT; i++) {
        entries[i].id = (unlock_id_t)i;
        entries[i].name = "(unspecified)";
        entries[i].desc = "";
        entries[i].requirement = NULL;
        entries[i].unlocked = HFALSE;
        entries[i].ever_used = HFALSE;
        entries[i].first_used[0] = '\0';
    }
    for (i = 0; i < RULE_COUNT; i++) {
        unlock_id_t id = rules[i].id;
        if (id < UL_COUNT) {
            entries[id].name = rules[i].name;
            entries[id].desc = rules[i].desc;
            entries[id].requirement = rules[i].req;
        }
    }
    initialised = HTRUE;
}

void unlock_evaluate(const hw_profile_t *hw)
{
    u16 i;
    if (!initialised) init_entries();
    enabled_count = 0;
    for (i = 0; i < RULE_COUNT; i++) {
        unlock_id_t id = rules[i].id;
        hbool on = (rules[i].check != 0) ? rules[i].check(hw) : HFALSE;
        if (id < UL_COUNT) {
            entries[id].unlocked = on;
            if (on) enabled_count++;
        }
    }
}

u16 unlock_count_enabled(void) { return enabled_count; }

const unlock_entry_t *unlock_get(unlock_id_t id)
{
    if (!initialised) init_entries();
    if ((unsigned)id >= UL_COUNT) return 0;
    return &entries[id];
}

const unlock_entry_t *unlock_get_all(void)
{
    if (!initialised) init_entries();
    return entries;
}

const unlock_rule_t *unlock_rules(void) { return rules; }
u16 unlock_rule_count(void) { return (u16)RULE_COUNT; }
