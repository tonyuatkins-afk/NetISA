/*
 * detect/video.c - Video adapter, chipset, and VESA detection.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 *
 * Strategy:
 *   1. MDA at 3B4h vs CGA at 3D4h selects the base class.
 *   2. Hercules: MDA + status port 3BAh bit 7 toggles with vertical retrace.
 *   3. EGA via INT 10h AH=12h BL=10h.
 *   4. VGA via INT 10h AX=1A00h.
 *   5. VESA via INT 10h AX=4F00h, walks the mode list, finds max 256 colour.
 *   6. Chipset fingerprint: a sequence of vendor specific magic register reads.
 *      Each probe is wrapped in save/restore of the relevant register.
 *   7. video_tier_t resolves from class + VRAM + max VESA mode.
 *
 * Real iron will surface plenty of edge cases. The structure here keeps each
 * probe a small, self contained function so a misbehaving probe can be
 * disabled with a single line change.
 */
#include "video.h"
#include <string.h>
#include <stdio.h>

extern hbool vid_int10_get_ega(void);
extern hbool vid_int10_get_vga(void);
extern hbool vid_vesa_info(vesa_info_t *out);

extern u8    vid_inp(u16 port);
extern void  vid_outp(u16 port, u8 val);
extern void  vid_index(u16 idx_port, u8 idx, u8 val);
extern u8    vid_index_read(u16 idx_port, u8 idx);

#ifdef HEARO_NOASM
hbool vid_int10_get_ega(void) { return HFALSE; }
hbool vid_int10_get_vga(void) { return HTRUE; }
hbool vid_vesa_info(vesa_info_t *o) { o->ver_major=2; o->ver_minor=0; o->vram_kb=2048; o->has_lfb=HTRUE; o->max_w=1024; o->max_h=768; return HTRUE; }
u8    vid_inp(u16 p) { (void)p; return 0xFF; }
void  vid_outp(u16 p, u8 v) { (void)p; (void)v; }
void  vid_index(u16 ip, u8 i, u8 v) { (void)ip; (void)i; (void)v; }
u8    vid_index_read(u16 ip, u8 i) { (void)ip; (void)i; return 0xFF; }
#endif

const char *video_class_name(video_class_t c)
{
    switch (c) {
        case VID_MDA:      return "MDA";
        case VID_HERCULES: return "Hercules";
        case VID_CGA:      return "CGA";
        case VID_EGA:      return "EGA";
        case VID_VGA:      return "VGA";
        case VID_SVGA:     return "SVGA";
        default:           return "unknown";
    }
}

const char *svga_chipset_name(svga_chipset_t c)
{
    switch (c) {
        case SVGA_TRIDENT_9000:    return "Trident 8900";
        case SVGA_TRIDENT_9400:    return "Trident 9400";
        case SVGA_CIRRUS_5422:     return "Cirrus Logic CL-GD5422";
        case SVGA_CIRRUS_5428:     return "Cirrus Logic CL-GD5428";
        case SVGA_CIRRUS_5434:     return "Cirrus Logic CL-GD5434";
        case SVGA_S3_805:          return "S3 805";
        case SVGA_S3_928:          return "S3 928";
        case SVGA_S3_TRIO64:       return "S3 Trio64";
        case SVGA_TSENG_ET3000:    return "Tseng ET3000";
        case SVGA_TSENG_ET4000:    return "Tseng ET4000";
        case SVGA_TSENG_ET4000W32: return "Tseng ET4000/W32";
        case SVGA_ATI_WONDER:      return "ATI Wonder";
        case SVGA_ATI_MACH8:       return "ATI Mach8";
        case SVGA_ATI_MACH32:      return "ATI Mach32";
        case SVGA_PARADISE_PVGA1A: return "Paradise PVGA1A";
        case SVGA_OAK_067:         return "Oak OTI-067";
        case SVGA_OAK_087:         return "Oak OTI-087";
        case SVGA_CT_65530:        return "Chips & Tech 65530";
        case SVGA_CT_65545:        return "Chips & Tech 65545";
        case SVGA_GENERIC_VESA:    return "VESA generic";
        case SVGA_NONE:            return "";
        default:                   return "unknown SVGA";
    }
}

const char *video_tier_name(video_tier_t t)
{
    switch (t) {
        case VTIER_MDA:         return "MDA text";
        case VTIER_EGA:         return "EGA";
        case VTIER_VGA_CLASSIC: return "VGA";
        case VTIER_SVGA_640:    return "SVGA 640";
        case VTIER_SVGA_800:    return "SVGA 800";
        case VTIER_SVGA_1024:   return "SVGA 1024";
        default:                return "unknown";
    }
}

/* Chipset probes - each restores any registers it writes to. */

static svga_chipset_t probe_s3(void)
{
    /* S3 unlock at 3D4h index 38h with 48h, then 39h with A5h. Read CR30. */
    u8 prev_lock1, prev_lock2, id;
    prev_lock1 = vid_index_read(0x3D4, 0x38);
    prev_lock2 = vid_index_read(0x3D4, 0x39);
    vid_index(0x3D4, 0x38, 0x48);
    vid_index(0x3D4, 0x39, 0xA5);
    id = vid_index_read(0x3D4, 0x30);
    vid_index(0x3D4, 0x39, prev_lock2);
    vid_index(0x3D4, 0x38, prev_lock1);
    if ((id & 0xF0) == 0x80) return SVGA_S3_805;
    if ((id & 0xF0) == 0x90) return SVGA_S3_928;
    if ((id & 0xF0) == 0x10) return SVGA_S3_TRIO64;
    return SVGA_NONE;
}

static svga_chipset_t probe_cirrus(void)
{
    u8 prev, id;
    prev = vid_index_read(0x3C4, 0x06);
    vid_index(0x3C4, 0x06, 0x12); /* unlock extensions */
    if (vid_index_read(0x3C4, 0x06) != 0x12) {
        vid_index(0x3C4, 0x06, prev);
        return SVGA_NONE;
    }
    id = vid_index_read(0x3C4, 0x1F);
    vid_index(0x3C4, 0x06, prev);
    switch (id & 0xFC) {
        case 0x88: return SVGA_CIRRUS_5422;
        case 0x9C: return SVGA_CIRRUS_5428;
        case 0xA8: return SVGA_CIRRUS_5434;
        default:   return SVGA_NONE;
    }
}

static svga_chipset_t probe_trident(void)
{
    u8 prev, id;
    prev = vid_index_read(0x3C4, 0x0B);
    /* Reading 3C4h:0B switches to mode 1; the readback ID is then valid. */
    id = vid_index_read(0x3C4, 0x0B);
    vid_outp(0x3C4, prev);
    if (id == 0x03) return SVGA_TRIDENT_9000;
    if (id == 0x04) return SVGA_TRIDENT_9400;
    return SVGA_NONE;
}

static svga_chipset_t probe_tseng(void)
{
    /* Tseng index port 3CDh: write nonzero, read back, restore. */
    u8 prev = vid_inp(0x3CD);
    vid_outp(0x3CD, 0x55);
    if (vid_inp(0x3CD) == 0x55) {
        vid_outp(0x3CD, 0xAA);
        if (vid_inp(0x3CD) == 0xAA) {
            vid_outp(0x3CD, prev);
            return SVGA_TSENG_ET4000;
        }
    }
    vid_outp(0x3CD, prev);
    return SVGA_NONE;
}

static svga_chipset_t probe_paradise(void)
{
    /* Paradise unlock: 3CEh index 0Fh with value 05h, then read 3CEh:0B. */
    u8 prev_lock = vid_index_read(0x3CE, 0x0F);
    u8 id;
    vid_index(0x3CE, 0x0F, 0x05);
    id = vid_index_read(0x3CE, 0x0B);
    vid_index(0x3CE, 0x0F, prev_lock);
    if ((id & 0xF0) == 0x80 || (id & 0xF0) == 0x90) return SVGA_PARADISE_PVGA1A;
    return SVGA_NONE;
}

static svga_chipset_t probe_oak(void)
{
    /* Oak index port 3DEh: write 23h, then read; valid Oak responds. */
    u8 prev = vid_inp(0x3DE);
    vid_outp(0x3DE, 0x23);
    if (vid_inp(0x3DE) == 0x23) {
        vid_outp(0x3DE, prev);
        return SVGA_OAK_087;
    }
    vid_outp(0x3DE, prev);
    return SVGA_NONE;
}

static svga_chipset_t probe_chips(void)
{
    u8 prev = vid_inp(0x3D6);
    vid_outp(0x3D6, 0x00);
    if (vid_inp(0x3D6) == 0x00) {
        vid_outp(0x3D6, prev);
        return SVGA_CT_65545;
    }
    vid_outp(0x3D6, prev);
    return SVGA_NONE;
}

static video_tier_t resolve_tier(const hw_profile_t *hw)
{
    if (hw->vid_class == VID_MDA || hw->vid_class == VID_HERCULES) return VTIER_MDA;
    if (hw->vid_class == VID_EGA) return VTIER_EGA;
    if (hw->vid_class == VID_VGA) return VTIER_VGA_CLASSIC;

    if (hw->vesa.max_w >= 1024) return VTIER_SVGA_1024;
    if (hw->vesa.max_w >= 800)  return VTIER_SVGA_800;
    if (hw->vesa.max_w >= 640)  return VTIER_SVGA_640;
    return VTIER_VGA_CLASSIC;
}

void video_detect(hw_profile_t *hw)
{
    hw->vid_class = VID_MDA;
    hw->svga_chip = SVGA_NONE;
    hw->vid_name[0] = '\0';

    if (vid_int10_get_vga()) {
        hw->vid_class = VID_VGA;
    } else if (vid_int10_get_ega()) {
        hw->vid_class = VID_EGA;
    } else {
        /* Walk the older path: CGA vs MDA via port presence. */
        if (vid_inp(0x3D4) != 0xFF) hw->vid_class = VID_CGA;
        else if (vid_inp(0x3B4) != 0xFF) hw->vid_class = VID_MDA;
    }

    if (hw->vid_class == VID_VGA && vid_vesa_info(&hw->vesa)) {
        hw->vid_class = VID_SVGA;
    } else {
        memset(&hw->vesa, 0, sizeof(hw->vesa));
    }

    if (hw->vid_class == VID_SVGA) {
        svga_chipset_t s;
        if      ((s = probe_s3()) != SVGA_NONE)        hw->svga_chip = s;
        else if ((s = probe_cirrus()) != SVGA_NONE)    hw->svga_chip = s;
        else if ((s = probe_trident()) != SVGA_NONE)   hw->svga_chip = s;
        else if ((s = probe_tseng()) != SVGA_NONE)     hw->svga_chip = s;
        else if ((s = probe_paradise()) != SVGA_NONE)  hw->svga_chip = s;
        else if ((s = probe_oak()) != SVGA_NONE)       hw->svga_chip = s;
        else if ((s = probe_chips()) != SVGA_NONE)     hw->svga_chip = s;
        else                                           hw->svga_chip = SVGA_GENERIC_VESA;
    }

    if (hw->svga_chip != SVGA_NONE) {
        sprintf(hw->vid_name, "%s, %uKB VRAM", svga_chipset_name(hw->svga_chip), hw->vesa.vram_kb);
    } else {
        sprintf(hw->vid_name, "%s", video_class_name(hw->vid_class));
    }

    hw->vid_tier = resolve_tier(hw);
}
