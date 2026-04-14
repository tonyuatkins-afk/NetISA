/*
 * wifi.c - WiFi scan/connect UI panel
 */

#include "screen.h"
#include "netisa.h"
#include <string.h>

#define MAX_NETWORKS    16
#define LIST_TOP        5
#define LIST_HEIGHT     14
#define PASS_MAX        63

static ni_wifi_network_t networks[MAX_NETWORKS];
static int net_count = 0;

static const char *security_str(uint8_t sec)
{
    switch (sec) {
    case NI_WIFI_WPA3: return "WPA3";
    case NI_WIFI_WPA2: return "WPA2";
    case NI_WIFI_WEP:  return "WEP ";
    case NI_WIFI_OPEN: return "Open";
    default:           return "????";
    }
}

/* Convert RSSI (dBm) to percentage 0-100 */
static int rssi_to_pct(int8_t rssi)
{
    int val = (int)rssi;
    if (val >= -30) return 100;
    if (val <= -90) return 0;
    return (val + 90) * 100 / 60;
}

static void draw_wifi_frame(void)
{
    scr_fill(1, 3, 78, 20, ' ', ATTR_NORMAL);
    scr_putc(4, 3, (char)BOX_BULLET, ATTR_BORDER);
    scr_puts(6, 3, "WiFi Setup", ATTR_HEADER);

    /* Column headers */
    scr_puts(6, 4, "SSID", ATTR_DIM);
    scr_puts(42, 4, "Signal", ATTR_DIM);
    scr_puts(52, 4, "Security", ATTR_DIM);
    scr_puts(62, 4, "Ch", ATTR_DIM);
    scr_hline(4, LIST_TOP + LIST_HEIGHT, 72, (char)0xC4, ATTR_DIM);

    /* Update status bar */
    scr_fill(1, 23, 78, 1, ' ', ATTR_STATUS);
    scr_puts(2, 23,
        " \x18\x19 Select  Enter Connect  R Rescan  D Disconnect  Esc Back ",
        ATTR_STATUS);
}

static void draw_network_list(int sel, int scroll)
{
    int vis;

    /* Clear list area */
    scr_fill(4, LIST_TOP, 74, LIST_HEIGHT, ' ', ATTR_NORMAL);

    if (net_count == 0) {
        scr_puts(4, LIST_TOP + 1, "No networks found. Press R to scan.",
                 ATTR_DIM);
        return;
    }

    for (vis = 0; vis < LIST_HEIGHT && (scroll + vis) < net_count; vis++) {
        int idx = scroll + vis;
        int y = LIST_TOP + vis;
        unsigned char attr;
        int pct;

        if (idx == sel) {
            attr = ATTR_SELECTED;
            scr_putc(4, y, (char)BOX_ARROW, ATTR_SELECTED);
        } else {
            attr = ATTR_NORMAL;
            scr_putc(4, y, ' ', ATTR_NORMAL);
        }

        /* SSID (truncated to 32 chars) */
        scr_putsn(6, y, networks[idx].ssid, 34, attr);

        /* Signal bars */
        pct = rssi_to_pct(networks[idx].rssi);
        scr_signal_bars(42, y, pct);

        /* Percentage */
        {
            char pctbuf[5];
            pctbuf[0] = (char)('0' + pct / 100);
            pctbuf[1] = (char)('0' + (pct / 10) % 10);
            pctbuf[2] = (char)('0' + pct % 10);
            pctbuf[3] = '%';
            pctbuf[4] = '\0';
            /* Skip leading zeros */
            if (pctbuf[0] == '0') {
                if (pctbuf[1] == '0')
                    scr_puts(47, y, pctbuf + 2, attr);
                else
                    scr_puts(47, y, pctbuf + 1, attr);
            } else {
                scr_puts(47, y, pctbuf, attr);
            }
        }

        /* Security */
        scr_puts(52, y, security_str(networks[idx].security), attr);

        /* Channel */
        {
            char chbuf[4];
            int ch = networks[idx].channel;
            if (ch >= 100) {
                chbuf[0] = (char)('0' + ch / 100);
                chbuf[1] = (char)('0' + (ch / 10) % 10);
                chbuf[2] = (char)('0' + ch % 10);
                chbuf[3] = '\0';
            } else if (ch >= 10) {
                chbuf[0] = (char)('0' + ch / 10);
                chbuf[1] = (char)('0' + ch % 10);
                chbuf[2] = '\0';
            } else {
                chbuf[0] = (char)('0' + ch);
                chbuf[1] = '\0';
            }
            scr_puts(62, y, chbuf, attr);
        }
    }
}

static void do_scan(void)
{
    scr_fill(4, LIST_TOP, 74, LIST_HEIGHT, ' ', ATTR_NORMAL);
    scr_puts(4, LIST_TOP + 1, "Scanning...", ATTR_INPUT);

    net_count = ni_wifi_scan(networks, MAX_NETWORKS);
    if (net_count < 0)
        net_count = 0;
}

/* Password input dialog. Returns 1 if user entered password, 0 if cancelled. */
static int password_dialog(const char *ssid, char *pass_out)
{
    int len = 0;
    int y = 10;

    /* Draw dialog box */
    scr_fill(10, 8, 60, 7, ' ', ATTR_NORMAL);
    scr_box(10, 8, 60, 7, ATTR_BORDER);
    scr_puts(12, 9, "Connect to: ", ATTR_HEADER);
    scr_putsn(24, 9, ssid, 42, ATTR_HIGHLIGHT);
    scr_puts(12, y, "Password: ", ATTR_NORMAL);

    /* Input field background */
    scr_fill(22, y, 44, 1, '_', ATTR_INPUT);

    scr_puts(12, 12, "Enter=Connect  Esc=Cancel", ATTR_DIM);

    scr_cursor_show();
    scr_cursor_pos(22, y);

    pass_out[0] = '\0';

    for (;;) {
        int key = scr_getkey();
        int ch = key & 0xFF;

        if (ch == 0x1B) {
            scr_cursor_hide();
            return 0;
        }
        if (ch == 0x0D) {
            pass_out[len] = '\0';
            scr_cursor_hide();
            return 1;
        }
        if (ch == 0x08) {
            /* Backspace */
            if (len > 0) {
                len--;
                if (len < 44) {
                    scr_putc(22 + len, y, '_', ATTR_INPUT);
                    scr_cursor_pos(22 + len, y);
                }
            }
        } else if (ch >= 0x20 && ch < 0x7F && len < PASS_MAX) {
            pass_out[len] = (char)ch;
            if (len < 44) {
                scr_putc(22 + len, y, '*', ATTR_INPUT);
                scr_cursor_pos(22 + len + 1, y);
            }
            len++;
        }
    }
}

static void do_connect(int idx)
{
    char password[PASS_MAX + 1];
    int err;

    if (networks[idx].security != NI_WIFI_OPEN) {
        if (!password_dialog(networks[idx].ssid, password))
            return;
    } else {
        password[0] = '\0';
    }

    /* Show connecting animation */
    scr_fill(4, LIST_TOP + LIST_HEIGHT + 1, 74, 1, ' ', ATTR_NORMAL);
    scr_puts(4, LIST_TOP + LIST_HEIGHT + 1, "Connecting...", ATTR_INPUT);

    err = ni_wifi_connect(networks[idx].ssid, password);

    /* Zero password from stack */
    memset(password, 0, sizeof(password));

    scr_fill(4, LIST_TOP + LIST_HEIGHT + 1, 74, 1, ' ', ATTR_NORMAL);
    if (err == NI_OK) {
        scr_puts(4, LIST_TOP + LIST_HEIGHT + 1,
                 "Connected!", ATTR_SELECTED);
    } else {
        scr_puts(4, LIST_TOP + LIST_HEIGHT + 1,
                 "Connection failed.", ATTR_ERROR);
    }
}

void panel_wifi(void)
{
    int sel = 0;
    int scroll = 0;

    draw_wifi_frame();
    do_scan();

    for (;;) {
        int key;

        draw_network_list(sel, scroll);
        key = scr_getkey();

        switch (key & 0xFF) {
        case 0x1B:
            return;
        case 0x0D:
            if (net_count > 0)
                do_connect(sel);
            break;
        case 'r':
        case 'R':
            sel = 0;
            scroll = 0;
            do_scan();
            break;
        case 'd':
        case 'D':
            ni_wifi_disconnect();
            scr_fill(4, LIST_TOP + LIST_HEIGHT + 1, 74, 1, ' ', ATTR_NORMAL);
            scr_puts(4, LIST_TOP + LIST_HEIGHT + 1,
                     "Disconnected.", ATTR_DIM);
            break;
        default:
            if ((key & 0xFF) == 0) {
                switch (key) {
                case KEY_UP:
                    if (sel > 0) {
                        sel--;
                        if (sel < scroll) scroll = sel;
                    }
                    break;
                case KEY_DOWN:
                    if (sel < net_count - 1) {
                        sel++;
                        if (sel >= scroll + LIST_HEIGHT)
                            scroll = sel - LIST_HEIGHT + 1;
                    }
                    break;
                }
            }
            break;
        }
    }
}
