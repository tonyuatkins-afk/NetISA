/*
 * status.c - Card status UI panel
 */

#include "screen.h"
#include "netisa.h"
#include <dos.h>
#include <i86.h>
#include <string.h>

/* Detect CPU type using flags register tests */
static const char *detect_cpu(void)
{
    unsigned short flags_out;

    /* Test: can we clear bits 12-15 of FLAGS? 8088/8086 can't. */
    _asm {
        pushf
        pop ax
        and ax, 0x0FFF
        push ax
        popf
        pushf
        pop ax
        and ax, 0xF000
        mov flags_out, ax
    }
    if (flags_out == 0xF000)
        return "8088/8086";

    /* Test: can we set bit 14 (NT flag)? 286 can't. */
    _asm {
        pushf
        pop ax
        or ax, 0x7000
        push ax
        popf
        pushf
        pop ax
        and ax, 0x7000
        mov flags_out, ax
    }
    if (flags_out == 0)
        return "80286";

    /* Test: can we flip bit 18 (AC flag)? 386 can't. */
    /* We can't directly test AC from 16-bit mode, so 386+ is our limit */
    return "80386+";
}

/* Detect FPU using Intel's standard method (AP-485):
 * FNINIT resets FPU status word to 0. If no FPU is present, the bus
 * cycle is ignored and the test word remains at the sentinel value.
 * FNSTSW stores the status word; if it reads back as 0, an FPU responded. */
static int detect_fpu(void)
{
    unsigned short status;

    /* Check BIOS equipment list first - bit 1 = FPU present */
    {
        union REGS r;
        int86(0x11, &r, &r);
        if (!(r.x.ax & 0x02)) return 0;  /* No FPU per BIOS */
    }

    _asm {
        fninit
        mov word ptr status, 0x5A5A
        fnstsw status
    }
    return (status == 0) ? 1 : 0;
}

/* Get DOS version */
static void get_dos_version(uint8_t *major, uint8_t *minor)
{
    union REGS r;
    r.h.ah = 0x30;
    int86(0x21, &r, &r);
    *major = r.h.al;
    *minor = r.h.ah;
}

/* Get free conventional memory (in KB) */
static uint16_t get_free_mem_kb(void)
{
    union REGS r;
    r.h.ah = 0x48;
    r.w.bx = 0xFFFF;  /* Request impossibly large block */
    int86(0x21, &r, &r);
    /* BX = largest available block in paragraphs (16 bytes each) */
    return (uint16_t)(r.w.bx / 64);  /* Convert paragraphs to KB */
}

static void int_to_str(char *buf, unsigned long val)
{
    char tmp[12];
    int i = 0;
    int j;

    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    while (val > 0) {
        tmp[i++] = (char)('0' + (val % 10));
        val /= 10;
    }
    for (j = 0; j < i; j++)
        buf[j] = tmp[i - 1 - j];
    buf[i] = '\0';
}

static void ip_to_str(char *buf, uint8_t *ip)
{
    char tmp[4];
    int i;
    buf[0] = '\0';
    for (i = 0; i < 4; i++) {
        int_to_str(tmp, ip[i]);
        strcat(buf, tmp);
        if (i < 3) strcat(buf, ".");
    }
}

void panel_status(void)
{
    ni_card_status_t cs;
    ni_wifi_status_t ws;
    ni_version_t fv;
    uint32_t uptime;
    uint8_t dos_maj, dos_min;
    uint16_t free_kb;
    const char *cpu;
    int has_fpu;
    int y;
    char buf[80];

    /* Gather all status info */
    ni_card_status(&cs);
    ni_wifi_status(&ws);
    ni_fw_version(&fv);
    ni_diag_uptime(&uptime);
    get_dos_version(&dos_maj, &dos_min);
    free_kb = get_free_mem_kb();
    cpu = detect_cpu();
    has_fpu = detect_fpu();

    /* Draw panel */
    scr_fill(1, 3, 78, 20, ' ', ATTR_NORMAL);
    scr_putc(4, 3, (char)BOX_BULLET, ATTR_BORDER);
    scr_puts(6, 3, "Card Status", ATTR_HEADER);

    y = 5;

    /* Network section */
    scr_puts(4, y, "Network", ATTR_HEADER);
    scr_hline(12, y, 20, (char)0xC4, ATTR_DIM);
    y++;

    scr_puts(6, y, "WiFi SSID:", ATTR_DIM);
    if (ws.connected)
        scr_puts(20, y, ws.ssid, ATTR_HIGHLIGHT);
    else
        scr_puts(20, y, "(not connected)", ATTR_DIM);
    y++;

    scr_puts(6, y, "IP Address:", ATTR_DIM);
    if (ws.connected) {
        ip_to_str(buf, ws.ip);
        scr_puts(20, y, buf, ATTR_HIGHLIGHT);
    } else {
        scr_puts(20, y, "0.0.0.0", ATTR_DIM);
    }
    y++;

    /* Signal percentage comes from the card's pre-computed value
     * (cs.signal_pct via NETSTATUS).  wifi.c uses rssi_to_pct() on
     * raw RSSI instead.  Both sources are valid but may produce
     * slightly different values for the same connection. */
    scr_puts(6, y, "Signal:", ATTR_DIM);
    if (ws.connected) {
        int pct = cs.signal_pct;
        scr_signal_bars(20, y, pct);
        int_to_str(buf, pct);
        strcat(buf, "%");
        scr_puts(25, y, buf, ATTR_NORMAL);
    } else {
        scr_puts(20, y, "N/A", ATTR_DIM);
    }
    y += 2;

    /* Card section */
    scr_puts(4, y, "Card", ATTR_HEADER);
    scr_hline(9, y, 23, (char)0xC4, ATTR_DIM);
    y++;

    scr_puts(6, y, "Firmware:", ATTR_DIM);
    buf[0] = 'v';
    int_to_str(buf + 1, fv.major);
    strcat(buf, ".");
    int_to_str(buf + strlen(buf), fv.minor);
    strcat(buf, ".");
    int_to_str(buf + strlen(buf), fv.patch);
    scr_puts(20, y, buf, ATTR_HIGHLIGHT);
    y++;

    scr_puts(6, y, "Sessions:", ATTR_DIM);
    int_to_str(buf, cs.active_sessions);
    strcat(buf, "/");
    int_to_str(buf + strlen(buf), cs.max_sessions);
    scr_puts(20, y, buf, ATTR_NORMAL);
    y++;

    scr_puts(6, y, "Uptime:", ATTR_DIM);
    {
        uint32_t hrs = uptime / 3600;
        uint32_t mins = (uptime % 3600) / 60;
        uint32_t secs = uptime % 60;
        int_to_str(buf, hrs);
        strcat(buf, "h ");
        int_to_str(buf + strlen(buf), mins);
        strcat(buf, "m ");
        int_to_str(buf + strlen(buf), secs);
        strcat(buf, "s");
    }
    scr_puts(20, y, buf, ATTR_NORMAL);
    y++;

    scr_puts(6, y, "Base I/O:", ATTR_DIM);
    scr_puts(20, y, "0x280 (default)", ATTR_NORMAL);
    y += 2;

    /* System section */
    scr_puts(4, y, "System", ATTR_HEADER);
    scr_hline(11, y, 21, (char)0xC4, ATTR_DIM);
    y++;

    scr_puts(6, y, "CPU:", ATTR_DIM);
    scr_puts(20, y, cpu, ATTR_NORMAL);
    y++;

    scr_puts(6, y, "FPU:", ATTR_DIM);
    scr_puts(20, y, has_fpu ? "Detected" : "Not found", ATTR_NORMAL);
    y++;

    scr_puts(6, y, "DOS:", ATTR_DIM);
    int_to_str(buf, dos_maj);
    strcat(buf, ".");
    int_to_str(buf + strlen(buf), dos_min);
    scr_puts(20, y, buf, ATTR_NORMAL);
    y++;

    scr_puts(6, y, "Free RAM:", ATTR_DIM);
    int_to_str(buf, free_kb);
    strcat(buf, " KB");
    scr_puts(20, y, buf, ATTR_NORMAL);

    /* Footer */
    scr_fill(1, 23, 78, 1, ' ', ATTR_STATUS);
    scr_puts(2, 23, " Esc Back ", ATTR_STATUS);

    for (;;) {
        int key = scr_getkey();
        if ((key & 0xFF) == 0x1B)
            return;
    }
}
