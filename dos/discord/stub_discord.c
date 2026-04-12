/*
 * stub_discord.c - Fake Discord data for DOSBox-X testing
 *
 * Per-channel message pools are far-heap allocated to avoid blowing
 * the 64KB DGROUP limit in small model.
 */

#include "discord.h"
#include <string.h>
#include <malloc.h>
#include <dos.h>

/* Author color palette */
static const unsigned char author_colors[DC_NUM_COLORS] = {
    SCR_ATTR(SCR_LIGHTCYAN, SCR_BLACK),
    SCR_ATTR(SCR_LIGHTMAGENTA, SCR_BLACK),
    SCR_ATTR(SCR_YELLOW, SCR_BLACK),
    SCR_ATTR(SCR_LIGHTBLUE, SCR_BLACK),
    SCR_ATTR(SCR_LIGHTRED, SCR_BLACK),
    SCR_ATTR(SCR_LIGHTGREEN, SCR_BLACK)
};

/* Per-channel message storage on far heap.
 * Each channel gets its own far-allocated array of DC_MAX_MESSAGES. */
static dc_message_t far *chan_msgs[DC_MAX_CHANNELS];
static int chan_msg_count[DC_MAX_CHANNELS];

static void add_msg(int ch_idx, const char *author, const char *text,
                    const char *ts, unsigned char color, int is_self)
{
    dc_message_t far *m;
    int idx;
    if (ch_idx < 0 || ch_idx >= DC_MAX_CHANNELS) return;
    if (!chan_msgs[ch_idx]) return;
    idx = chan_msg_count[ch_idx];
    if (idx >= DC_MAX_MESSAGES) return;

    m = &chan_msgs[ch_idx][idx];
    _fstrncpy((char far *)m->author, (const char far *)author, DC_MAX_AUTHOR_LEN - 1);
    m->author[DC_MAX_AUTHOR_LEN - 1] = '\0';
    _fstrncpy((char far *)m->text, (const char far *)text, DC_MAX_MSG_LEN - 1);
    m->text[DC_MAX_MSG_LEN - 1] = '\0';
    _fstrncpy((char far *)m->timestamp, (const char far *)ts, 5);
    m->timestamp[5] = '\0';
    m->author_color = color;
    m->is_self = is_self;
    chan_msg_count[ch_idx]++;
}

void stub_load_channel_msgs(dc_state_t *s, int ch_idx)
{
    int count, i;
    dc_message_t far *src;

    if (ch_idx < 0 || ch_idx >= DC_MAX_CHANNELS) return;
    if (!chan_msgs[ch_idx]) { s->msg_count = 0; return; }

    count = chan_msg_count[ch_idx];
    if (count > DC_MAX_MESSAGES) count = DC_MAX_MESSAGES;

    for (i = 0; i < count; i++) {
        src = &chan_msgs[ch_idx][i];
        _fmemcpy((void far *)&s->messages[i], (void far *)src,
                 sizeof(dc_message_t));
    }
    s->msg_count = count;
}

void stub_save_channel_msgs(dc_state_t *s)
{
    int ch = s->selected_channel;
    int count = s->msg_count;
    int i;
    dc_message_t far *dst;

    if (!chan_msgs[ch]) return;
    if (count > DC_MAX_MESSAGES) count = DC_MAX_MESSAGES;

    for (i = 0; i < count; i++) {
        dst = &chan_msgs[ch][i];
        _fmemcpy((void far *)dst, (void far *)&s->messages[i],
                 sizeof(dc_message_t));
    }
    chan_msg_count[ch] = count;
}

void stub_load_server(dc_state_t *s)
{
    int i;
    unsigned long alloc_size;

    strncpy(s->server_name, "Retro Computing Hub", DC_MAX_SERVER_NAME - 1);
    s->channel_count = 6;

    /* Allocate per-channel far message pools.
     * Guard: alloc_size must fit in 16-bit size_t for _fmalloc. */
    alloc_size = (unsigned long)DC_MAX_MESSAGES * sizeof(dc_message_t);
    if (alloc_size > 65535UL) return; /* Overflow: struct too large for _fmalloc */
    for (i = 0; i < DC_MAX_CHANNELS; i++) {
        chan_msg_count[i] = 0;
        chan_msgs[i] = (dc_message_t far *)_fmalloc((unsigned)alloc_size);
        if (chan_msgs[i])
            _fmemset((void far *)chan_msgs[i], 0, (unsigned)alloc_size);
    }

    strcpy(s->channels[0].name, "general");
    strcpy(s->channels[1].name, "hardware");
    strcpy(s->channels[2].name, "netisa");
    strcpy(s->channels[3].name, "software");
    strcpy(s->channels[4].name, "marketplace");
    strcpy(s->channels[5].name, "off-topic");

    s->channels[0].unread = 1;
    s->channels[2].unread = 1;

    /* #general */
    add_msg(0, "VintageNerd", "Hey everyone! XT-IDE rev4 soldered up. First try boot from CF card!", "14:02", author_colors[0], 0);
    add_msg(0, "RetroGamer", "Nice! Which CF adapter?", "14:03", author_colors[1], 0);
    add_msg(0, "VintageNerd", "Generic 40-pin IDE to CF. DOS 6.22, 32MB partition", "14:04", author_colors[0], 0);
    add_msg(0, "ChipCollector", "32MB? Living large. My 8088 only has 20MB", "14:05", author_colors[3], 0);
    add_msg(0, "BarelyBooting", "Anyone seen the NetISA project? ISA card with TLS 1.3 offload", "14:07", author_colors[5], 0);
    add_msg(0, "DOSenthusiast", "Yeah, an 8087 but for crypto. Love it", "14:08", author_colors[4], 0);
    add_msg(0, "RetroGamer", "Real HTTPS from a 286? That works?", "14:09", author_colors[1], 0);
    add_msg(0, "BarelyBooting", "ESP32-S3 handles crypto. DOS sends cleartext via INT 63h. Even 8088 can do it", "14:10", author_colors[5], 0);
    add_msg(0, "VintageNerd", "Browse github from an XT? Wild", "14:11", author_colors[0], 0);
    add_msg(0, "BarelyBooting", "Text mode. Working on a browser called Cathode", "14:12", author_colors[5], 0);
    add_msg(0, "ChipCollector", "How much conventional memory?", "14:13", author_colors[3], 0);
    add_msg(0, "BarelyBooting", "Under 2KB. TSR is INT vector + port I/O. Library per app", "14:14", author_colors[5], 0);
    add_msg(0, "DOSenthusiast", "mTCP model. Smart choice", "14:15", author_colors[4], 0);
    add_msg(0, "RetroGamer", "Spare CT1350 anyone? Mine died", "14:17", author_colors[1], 0);
    add_msg(0, "ChipCollector", "Check #marketplace", "14:18", author_colors[3], 0);

    /* #hardware */
    add_msg(1, "ChipCollector", "486DX2-66 with VLB slots for $25", "13:40", author_colors[3], 0);
    add_msg(1, "VintageNerd", "VLB in 2026. Respect.", "13:42", author_colors[0], 0);
    add_msg(1, "RetroGamer", "VLB ET4000 still best DOS gaming card", "13:45", author_colors[1], 0);

    /* #netisa */
    add_msg(2, "DOSenthusiast", "When is NetISA available?", "12:30", author_colors[4], 0);
    add_msg(2, "BarelyBooting", "Phase 0 still. Hardware design done, Verilog compiles clean", "12:32", author_colors[5], 0);
    add_msg(2, "VintageNerd", "What CPLD? ATF1508 discontinued?", "12:35", author_colors[0], 0);
    add_msg(2, "BarelyBooting", "ATF1508AS TQFP-100. Still made, PLCC-84 gone. ~$4 Mouser", "12:36", author_colors[5], 0);
    add_msg(2, "RetroGamer", "Multiplayer DOS games online?", "12:40", author_colors[1], 0);
    add_msg(2, "BarelyBooting", "Not v1 (needs UDP, v2). Serial-over-TLS bridge is easy though", "12:42", author_colors[5], 0);
    add_msg(2, "ChipCollector", "TLS handshake latency on 8088?", "12:45", author_colors[3], 0);
    add_msg(2, "BarelyBooting", "Handshake on ESP32-S3 at 240MHz. ~500ms. 8088 just waits", "12:47", author_colors[5], 0);
    add_msg(2, "DOSenthusiast", "500ms is nothing. 486 takes longer to load DOOM", "12:48", author_colors[4], 0);
    add_msg(2, "VintageNerd", "This keeps the retro scene alive", "12:50", author_colors[0], 0);

    /* #software */
    add_msg(3, "DOSenthusiast", "FreeDOS 1.4? Installer is way better", "11:20", author_colors[4], 0);
    add_msg(3, "RetroGamer", "Sticking with 6.22. Nostalgia", "11:25", author_colors[1], 0);
    add_msg(3, "VintageNerd", "FreeDOS + JEMMEX = 64MB EMS no board", "11:30", author_colors[0], 0);

    /* #marketplace */
    add_msg(4, "ChipCollector", "WTS: SB Pro CT1600, tested. $40 shipped", "10:00", author_colors[3], 0);
    add_msg(4, "RetroGamer", "WTB: 3Com 3C509B for packet driver", "10:30", author_colors[1], 0);
    add_msg(4, "VintageNerd", "WTS: 4x 30-pin 1MB SIMMs. $15/set", "11:00", author_colors[0], 0);

    /* #off-topic */
    add_msg(5, "RetroGamer", "8-Bit Guy Commander X16 video?", "09:00", author_colors[1], 0);
    add_msg(5, "DOSenthusiast", "VERA chip impressive for modern retro", "09:05", author_colors[4], 0);
    add_msg(5, "ChipCollector", "Backed kickstarter. Should ship eventually", "09:10", author_colors[3], 0);
}

/* Timed fake messages */
static const char *timed_authors[] = {
    "VintageNerd", "RetroGamer", "ChipCollector", "DOSenthusiast", "BarelyBooting"
};
static const char *timed_msgs[] = {
    "Anyone running OS/2 Warp on real HW?",
    "Found NIB 3Com 3C509 at Goodwill. $2",
    "DOSBox-X nightly fixes SB16 hang notes",
    "486 survived CHKDSK /F on 500MB. Sweating",
    "lol who needs more than 640K anyway",
    "Retrobright result: Model M looks new",
    "Where to get keyboard springs?",
    "Hot take: Turbo Pascal was peak DOS dev",
    "pcjs.org updates are amazing for testing",
    "Ordered EPM7128 CPLDs. Verilog time"
};
#define NUM_TIMED 10
static int ti_msg = 0, ti_auth = 0;

/* NOTE: BIOS tick counter wraps at midnight. The unsigned subtraction
 * (now - last_poll_tick) will produce a large value on wrap, causing one
 * spurious immediate injection. Acceptable for stub testing. */
void stub_inject_timed_message(dc_state_t *s)
{
    unsigned long now;
    char ts[6];

    _asm {
        xor ax, ax
        int 1Ah
        mov word ptr now, dx
        mov word ptr now+2, cx
    }

    if (s->last_poll_tick == 0) { s->last_poll_tick = now; return; }
    if ((now - s->last_poll_tick) < 91) return;
    s->last_poll_tick = now;

    {
        unsigned long secs = now / 18;
        unsigned char hr = (unsigned char)((secs / 3600) % 24);
        unsigned char mn = (unsigned char)((secs / 60) % 60);
        ts[0] = (char)('0' + hr / 10);
        ts[1] = (char)('0' + hr % 10);
        ts[2] = ':';
        ts[3] = (char)('0' + mn / 10);
        ts[4] = (char)('0' + mn % 10);
        ts[5] = '\0';
    }

    /* Add to #general internal storage, dropping oldest if full */
    if (chan_msg_count[0] >= DC_MAX_MESSAGES) {
        int j;
        for (j = 0; j < DC_MAX_MESSAGES - 1; j++)
            _fmemcpy((void far *)&chan_msgs[0][j],
                     (void far *)&chan_msgs[0][j + 1],
                     sizeof(dc_message_t));
        chan_msg_count[0] = DC_MAX_MESSAGES - 1;
    }
    add_msg(0, timed_authors[ti_auth], timed_msgs[ti_msg], ts,
            author_colors[ti_auth], 0);

    /* If viewing #general, also update the active buffer */
    if (s->selected_channel == 0) {
        if (s->msg_count >= DC_MAX_MESSAGES) {
            memmove(&s->messages[0], &s->messages[1],
                    (DC_MAX_MESSAGES - 1) * sizeof(dc_message_t));
            s->msg_count = DC_MAX_MESSAGES - 1;
            /* Keep scroll position stable after shift */
            if (s->msg_scroll > 0) s->msg_scroll--;
        }
        {
            dc_message_t far *src = &chan_msgs[0][chan_msg_count[0] - 1];
            _fmemcpy((void far *)&s->messages[s->msg_count],
                     (void far *)src, sizeof(dc_message_t));
            s->msg_count++;
        }
        s->dirty = 1;
    } else {
        s->channels[0].unread = 1;
        s->dirty = 1;
    }

    ti_msg = (ti_msg + 1) % NUM_TIMED;
    ti_auth = (ti_auth + 1) % 5;
}
