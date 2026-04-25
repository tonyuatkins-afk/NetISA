/*
 * stub_discord.c - Fake Discord data provider (v2)
 *
 * Populates 8 channels with ~75 realistic messages for offline
 * demo/testing. All message text lives in far-heap arrays to avoid
 * DGROUP pressure. String literals are temporary (copied by add_msg).
 *
 * Target: 8088 real mode, OpenWatcom C, small memory model.
 */

#include "discord.h"
#include <dos.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>

/* ================================================================
 * Per-channel far-heap message storage
 * ================================================================ */

dc_message_t far *chan_msgs[DC_MAX_CHANNELS];
int chan_msg_count[DC_MAX_CHANNELS];

/* ================================================================
 * Fake users
 * ================================================================ */

static const char *stub_users[8] = {
    "VintageNerd", "RetroGamer", "ChipCollector", "DOSenthusiast",
    "BarelyBooting", "PCBWizard", "AdLibFan", "SocketSlinger"
};

/* Shorthand indices for readability */
#define U_VNERD     0
#define U_RGAMER    1
#define U_CHIP      2
#define U_DOSEN     3
#define U_BARELY    4
#define U_PCB       5
#define U_ADLIB     6
#define U_SOCKET    7

/* ================================================================
 * Helper: add one message to a channel's far-heap array
 * ================================================================ */

static void add_msg(int ch, const char *author, const char *text,
                    const char *timestamp, unsigned char reactions,
                    int thread_id)
{
    dc_message_t far *m;
    int idx;

    if (ch < 0 || ch >= DC_MAX_CHANNELS) return;
    if (!chan_msgs[ch]) return;
    idx = chan_msg_count[ch];
    if (idx >= DC_MAX_MESSAGES) return;

    m = &chan_msgs[ch][idx];

    /* Zero the whole slot first */
    _fmemset((void far *)m, 0, sizeof(dc_message_t));

    /* Copy strings to far heap */
    _fstrncpy((char far *)m->author, (const char far *)author,
              DC_MAX_AUTHOR_LEN - 1);
    m->author[DC_MAX_AUTHOR_LEN - 1] = '\0';

    _fstrncpy((char far *)m->text, (const char far *)text,
              DC_MAX_MSG_LEN - 1);
    m->text[DC_MAX_MSG_LEN - 1] = '\0';

    _fstrncpy((char far *)m->timestamp, (const char far *)timestamp, 5);
    m->timestamp[5] = '\0';

    m->author_color = dc_author_color(author);
    m->is_self = 0;

    /* Reactions: bits index into reaction_glyphs[] */
    if (reactions != 0) {
        /* Find first set bit for the reaction index */
        unsigned char r = reactions;
        unsigned char idx_r = 0;
        while (idx_r < 8 && !(r & (1 << idx_r))) idx_r++;
        m->reaction_idx = idx_r;
        m->reaction_count = 1;
        /* If multiple bits set, count them */
        while (r) { if (r & 1) m->reaction_count++; r >>= 1; }
        m->reaction_count--;  /* we double-counted */
    } else {
        m->reaction_idx = 0xFF;
        m->reaction_count = 0;
    }

    m->thread_count = thread_id ? thread_id : 0;

    chan_msg_count[ch]++;
}

/* ================================================================
 * Channel 0: #general  (10 messages, retro computing chat)
 * ================================================================ */

static void stub_populate_general(void)
{
    int ch = 0;

    add_msg(ch, stub_users[U_VNERD],
        "Morning all. Just got my XT-IDE rev4 working! Boots from CF card on the first try",
        "10:02", 0x01, 0);  /* bit 0 = heart reaction */

    add_msg(ch, stub_users[U_VNERD],
        "Generic 40-pin IDE to CF. Trick is to format with 32MB partition and use DOS 6.22 FDISK",
        "10:05", 0, 0);

    add_msg(ch, stub_users[U_CHIP],
        "32MB? Living large. My 8088 Turbo XT only has a 20MB MFM and it sounds like a jet engine",
        "10:07", 0, 0);

    add_msg(ch, stub_users[U_BARELY],
        "Hey everyone - NetISA update: got TLS 1.3 handshake completing on real hardware yesterday",
        "10:12", 0x80, 0);  /* bit 7 = music note reaction */

    add_msg(ch, stub_users[U_BARELY],
        "ESP32-S3 handles all the crypto at 240MHz. DOS just sends cleartext over the ISA bus via INT 63h",
        "10:14", 0, 0);

    add_msg(ch, stub_users[U_VNERD],
        "So the 8088 never touches the encryption? Smart. Like an 8087 but for TLS",
        "10:15", 0, 0);

    add_msg(ch, stub_users[U_BARELY],
        "Exactly. Under 2KB conventional RAM. TSR is just the INT vector and port I/O routines",
        "10:16", 0, 0);

    add_msg(ch, stub_users[U_PCB],
        "What's the PCB stack? 2-layer or 4-layer? ISA timing is brutal on 2-layer",
        "10:18", 0, 0);

    add_msg(ch, stub_users[U_SOCKET],
        "Digikey still has Sullins EBC08. Like $3 each. Stock is spotty though",
        "10:24", 0, 0);

    add_msg(ch, stub_users[U_VNERD],
        "Anyone going to VCF West this year? I'm thinking about hauling my Compaq Portable",
        "10:32", 0, 0);
}

/* ================================================================
 * Channel 1: #hardware  (10 messages, ISA/caps/CRTs/keyboards)
 * ================================================================ */

static void stub_populate_hardware(void)
{
    int ch = 1;

    add_msg(ch, stub_users[U_CHIP],
        "486DX2-66 with VLB slots showed up today. $25 at the swap meet. Board looks clean",
        "10:00", 0, 0);

    add_msg(ch, stub_users[U_PCB],
        "Check the caps near the VRM. OPTi boards from that era love to leak",
        "10:06", 0, 1);  /* thread_id=1: start cap discussion */

    add_msg(ch, stub_users[U_CHIP],
        "Good call. I see two bulging Nichicons near the CPU socket. Standard 1000uF 6.3v?",
        "10:08", 0, 1);  /* thread_id=1: cap discussion */

    add_msg(ch, stub_users[U_PCB],
        "Usually 1000uF 10v or 16v. Use Panasonic FR series for the replacement. Low ESR matters here",
        "10:10", 0, 1);  /* thread_id=1: cap discussion */

    add_msg(ch, stub_users[U_RGAMER],
        "VLB ET4000 is still the best DOS gaming card. Fight me",
        "10:14", 0, 0);

    add_msg(ch, stub_users[U_DOSEN],
        "My NEC MultiSync 3D just developed a horizontal linearity problem. CRT people, any ideas?",
        "10:20", 0, 0);

    add_msg(ch, stub_users[U_PCB],
        "Usually a dried-out cap in the horizontal deflection circuit. Check C412 first",
        "10:22", 0, 0);

    add_msg(ch, stub_users[U_VNERD],
        "Just recapped my Model M. Replaced the controller membrane too. Typing on it right now",
        "10:30", 0, 0);

    add_msg(ch, stub_users[U_SOCKET],
        "5-pin DIN. Clock on pin 1, data on pin 2, ground 4, VCC 5. Pin 3 is reset/NC",
        "10:46", 0, 0);

    add_msg(ch, stub_users[U_RGAMER],
        "Pro tip: use a Teensy 2.0 for XT-to-USB conversion. Soarer's converter firmware is perfect",
        "10:50", 0, 0);
}

/* ================================================================
 * Channel 2: #builds  (8 messages, project showcases)
 * ================================================================ */

static void stub_populate_builds(void)
{
    int ch = 2;

    add_msg(ch, stub_users[U_VNERD],
        "Finally finished my sleeper build: modern ATX case, AT board inside. Looks boring, sounds amazing",
        "09:00", 0, 0);

    add_msg(ch, stub_users[U_PCB],
        "Working on a new ISA backplane design. 8 slots, active termination, proper power distribution",
        "09:10", 0, 0);

    add_msg(ch, stub_users[U_PCB],
        "Testing ISA cards without needing a vintage motherboard. Plug in CPU card + whatever you want",
        "09:14", 0, 0);

    add_msg(ch, stub_users[U_DOSEN],
        "Built a portable 486 in a Pelican case. 10\" LCD, compact keyboard, runs on 12V battery",
        "09:22", 0, 0);

    add_msg(ch, stub_users[U_SOCKET],
        "My project: ESP32 to parallel port adapter. Lets old printers work over WiFi",
        "09:30", 0, 0);

    add_msg(ch, stub_users[U_SOCKET],
        "Yeah IPP on the ESP32 side, standard LPT on the DOS side. Transparent to the application",
        "09:34", 0, 0);

    add_msg(ch, stub_users[U_RGAMER],
        "Converted a Tandy 1000 to take a GoTek. No more hunting for 720K floppies",
        "09:50", 0, 0);

    add_msg(ch, stub_users[U_BARELY],
        "PSA: if anyone wants to test NetISA, I have 3 prototype boards ready. Need testers with real ISA hardware",
        "10:00", 0, 0);
}

/* ================================================================
 * Channel 3: #software  (8 messages, DOS games/FreeDOS/dev)
 * ================================================================ */

static void stub_populate_software(void)
{
    int ch = 3;

    add_msg(ch, stub_users[U_DOSEN],
        "FreeDOS 1.4 is solid. The new installer actually detects my hardware correctly now",
        "10:00", 0, 0);

    add_msg(ch, stub_users[U_VNERD],
        "FreeDOS + JEMMEX gives you 64MB of EMS without a hardware board. That alone is worth it",
        "10:05", 0, 0);

    add_msg(ch, stub_users[U_SOCKET],
        "OpenWatcom is better for real-mode targets though. DJGPP needs a 386 minimum",
        "10:12", 0, 0);

    add_msg(ch, stub_users[U_RGAMER],
        "DOSBox-X just fixed the SB16 hanging notes bug. Finally can play Descent without glitches",
        "10:22", 0, 0);

    add_msg(ch, stub_users[U_DOSEN],
        "Hot take: Borland Pascal 7.0 was the peak of DOS development environments",
        "10:34", 0, 0);

    add_msg(ch, stub_users[U_SOCKET],
        "Not a hot take. That IDE was 20 years ahead of its time. Context-sensitive help, integrated debugger",
        "10:36", 0, 0);

    add_msg(ch, stub_users[U_BARELY],
        "Wrote a tiny HTTP client in 800 lines of Watcom C. Fits in 8KB with the NetISA library",
        "10:50", 0, 0);

    add_msg(ch, stub_users[U_BARELY],
        "Zero. TLS is fully offloaded to the ESP32. DOS app sees a plain socket API",
        "10:54", 0, 0);
}

/* ================================================================
 * Channel 4: #marketplace  (7 messages, WTS/WTB/FS posts)
 * ================================================================ */

static void stub_populate_marketplace(void)
{
    int ch = 4;

    add_msg(ch, stub_users[U_CHIP],
        "WTS: Sound Blaster Pro CT1600, fully tested with DIAGNOSE.EXE. $40 shipped CONUS",
        "09:00", 0, 0);

    add_msg(ch, stub_users[U_RGAMER],
        "WTB: 3Com 3C509B ISA ethernet. Need one for packet driver testing. Will pay $20+ship",
        "09:15", 0, 0);

    add_msg(ch, stub_users[U_DOSEN],
        "FS: IBM Model M 1391401, April 1989. Bolt modded, cleaned. All keys work. $80",
        "09:45", 0, 0);

    add_msg(ch, stub_users[U_ADLIB],
        "WTB: AdLib Music Synthesizer Card. Original only, no clones. Budget: $100",
        "10:10", 0, 0);

    add_msg(ch, stub_users[U_CHIP],
        "WTS: Complete 386DX-40 system. 8MB RAM, 120MB HDD, SVGA, SB16. $60 local pickup only",
        "10:30", 0, 0);

    add_msg(ch, stub_users[U_ADLIB],
        "WTB: Roland MT-32 or LAPC-I. For serious Sierra gaming. Budget flexible for good condition",
        "11:20", 0, 0);

    add_msg(ch, stub_users[U_BARELY],
        "FS: NetISA prototype board rev0.2. Bare PCB only, no components. Free + shipping if you'll test it",
        "11:30", 0, 0);
}

/* ================================================================
 * Channel 5: #off-topic  (7 messages, non-DOS retro)
 * ================================================================ */

static void stub_populate_offtopic(void)
{
    int ch = 5;

    add_msg(ch, stub_users[U_RGAMER],
        "Watched the 8-Bit Guy's Commander X16 update. VERA chip is seriously impressive",
        "09:00", 0, 0);

    add_msg(ch, stub_users[U_ADLIB],
        "Found an Amiga 500 at Goodwill for $15. Missing power supply but board looks perfect",
        "09:14", 0, 0);

    add_msg(ch, stub_users[U_PCB],
        "I designed a replacement Amiga PSU board. Open source, takes a Meanwell 5V/12V combo",
        "09:18", 0, 0);

    add_msg(ch, stub_users[U_DOSEN],
        "Anyone else collect manuals? Just got a mint IBM PC Technical Reference 1st edition",
        "09:32", 0, 0);

    add_msg(ch, stub_users[U_ADLIB],
        "MIDI on vintage hardware is a rabbit hole. Just bought a Roland SC-55 mk2",
        "09:40", 0, 0);

    add_msg(ch, stub_users[U_BARELY],
        "Random find: box of NIB Intel 8087 co-processors at an estate sale. $5 each",
        "09:48", 0, 0);

    add_msg(ch, stub_users[U_SOCKET],
        "Just realized my Raspberry Pi 5 has more computing power than every DOS machine I own combined",
        "09:55", 0, 0);
}

/* ================================================================
 * Channel 6: #help  (7 messages, troubleshooting)
 * ================================================================ */

static void stub_populate_help(void)
{
    int ch = 6;

    add_msg(ch, stub_users[U_DOSEN],
        "Help! My 386 won't POST after adding a second ISA card. Just beeps once and stops",
        "10:00", 0, 0);

    add_msg(ch, stub_users[U_PCB],
        "One beep is usually RAM. Try reseating your SIMMs first. Then remove the new card and test",
        "10:02", 0, 0);

    add_msg(ch, stub_users[U_DOSEN],
        "Reseated RAM, same thing. Card out = boots fine. Card in any slot = dead",
        "10:05", 0, 0);

    add_msg(ch, stub_users[U_DOSEN],
        "NE2000 clone. Jumpered to IRQ 5, base 300h. Other card is a SB Pro on IRQ 7, 220h",
        "10:09", 0, 0);

    add_msg(ch, stub_users[U_PCB],
        "IRQ 5 is fine but check if the NE2000 uses memory at D000h. Could overlap with your BIOS shadow",
        "10:11", 0, 0);

    add_msg(ch, stub_users[U_DOSEN],
        "That was it! Disabled upper memory shadow in BIOS. Boots now. Thank you!",
        "10:15", 0, 0);

    add_msg(ch, stub_users[U_ADLIB],
        "Sound Blaster not detected in any game. SET BLASTER is correct, DIAGNOSE works fine. Ideas?",
        "10:30", 0, 0);
}

/* ================================================================
 * Channel 7: #showcase  (7 messages, finished builds)
 * ================================================================ */

static void stub_populate_showcase(void)
{
    int ch = 7;

    add_msg(ch, stub_users[U_VNERD],
        "486DX4-100 build complete: Tseng ET4000, GUS Classic, 16MB RAM, 540MB Quantum Fireball",
        "09:00", 0, 0);

    add_msg(ch, stub_users[U_CHIP],
        "Restored an IBM 5150 to stock. New PSU fan, cleaned the board, retrobright case. Looks factory fresh",
        "09:10", 0, 0);

    add_msg(ch, stub_users[U_PCB],
        "Custom ISA sound card project done. OPL3 + DAC + game port. All through-hole for easy assembly",
        "09:20", 0, 0);

    add_msg(ch, stub_users[U_PCB],
        "Gerbers and BOM on GitHub next week. The YMF262 is the hardest part to source",
        "09:24", 0, 0);

    add_msg(ch, stub_users[U_BARELY],
        "NetISA rev0.3 is assembled. First successful HTTPS GET from a 4.77MHz 8088 to github.com",
        "09:30", 0, 0);

    add_msg(ch, stub_users[U_SOCKET],
        "Portable LAN party kit: two 486 laptops + serial null modem + DOOM. Fits in a backpack",
        "09:40", 0, 0);

    add_msg(ch, stub_users[U_DOSEN],
        "Pentium 75 + Voodoo 1 retro gaming rig. GLQuake at 640x480. Smooth as butter",
        "09:56", 0, 0);
}

/* ================================================================
 * Timed injection: rotating pool of messages
 * ================================================================ */

static const char * const timed_authors[] = {
    "VintageNerd", "RetroGamer", "ChipCollector", "DOSenthusiast",
    "BarelyBooting", "PCBWizard", "AdLibFan", "SocketSlinger"
};

static const char * const timed_msgs[] = {
    "Anyone running OS/2 Warp on real hardware? Just got a set of install floppies",
    "Found NIB 3Com 3C509 at Goodwill for $2. Score of the century",
    "DOSBox-X nightly build fixes the SB16 hanging notes bug",
    "My 486 survived a full CHKDSK /F on a 500MB drive. Was sweating the whole time",
    "Who needs more than 640K anyway? (me. I need more.)",
    "Retrobright results: Model M case looks factory new after 8 hours in the sun",
    "Where does everyone source replacement keyboard springs?",
    "Hot take: Turbo Pascal was the peak of DOS development",
    "pcjs.org keeps getting better. Just ran VisiCalc in the browser",
    "Ordered EPM7128 CPLDs from Mouser. Time to learn Verilog",
    "Just flashed a 27C256 EPROM on my first try. UV eraser actually works",
    "The smell of old capacitors is either nostalgia or a health hazard",
    "Anyone have the jumper settings for a WD1003? Can't find the manual online",
    "Pro tip: nail polish on the BIOS chip label preserves it during cleaning"
};
#define NUM_TIMED_MSGS  14
#define NUM_TIMED_AUTH  8

static int ti_msg = 0;
static int ti_auth = 0;

/* ================================================================
 * stub_load_server - Initialize all server data
 * ================================================================ */

void stub_load_server(dc_state_t *s)
{
    int i;
    unsigned long alloc_size;

    strncpy(s->server_name, "Retro Computing Hub", DC_MAX_SERVER_NAME - 1);
    s->server_name[DC_MAX_SERVER_NAME - 1] = '\0';
    s->channel_count = 8;

    /* Allocate per-channel far-heap message pools */
    alloc_size = (unsigned long)DC_MAX_MESSAGES * sizeof(dc_message_t);
    if (alloc_size > 65535UL) return;  /* safety: won't fit in _fmalloc */

    for (i = 0; i < DC_MAX_CHANNELS; i++) {
        chan_msg_count[i] = 0;
        chan_msgs[i] = (dc_message_t far *)_fmalloc((unsigned)alloc_size);
        if (chan_msgs[i])
            _fmemset((void far *)chan_msgs[i], 0, (unsigned)alloc_size);
    }

    /* Channel names */
    strcpy(s->channels[0].name, "general");
    strcpy(s->channels[1].name, "hardware");
    strcpy(s->channels[2].name, "builds");
    strcpy(s->channels[3].name, "software");
    strcpy(s->channels[4].name, "marketplace");
    strcpy(s->channels[5].name, "off-topic");
    strcpy(s->channels[6].name, "help");
    strcpy(s->channels[7].name, "showcase");

    /* Populate each channel */
    stub_populate_general();
    stub_populate_hardware();
    stub_populate_builds();
    stub_populate_software();
    stub_populate_marketplace();
    stub_populate_offtopic();
    stub_populate_help();
    stub_populate_showcase();

    /* Set initial unread counts */
    s->channels[0].unread = 3;   /* general */
    s->channels[1].unread = 0;   /* hardware */
    s->channels[2].unread = 5;   /* builds */
    s->channels[3].unread = 0;   /* software */
    s->channels[4].unread = 2;   /* marketplace */
    s->channels[5].unread = 0;   /* off-topic */
    s->channels[6].unread = 1;   /* help */
    s->channels[7].unread = 0;   /* showcase */

    /* Sync msg_count to channel metadata (not stored in channel struct,
     * but ensure the active channel loads correctly) */
    for (i = 0; i < DC_MAX_CHANNELS; i++) {
        s->channels[i].mention = 0;
    }
}

/* ================================================================
 * stub_load_channel_msgs - Copy far-heap msgs to near active buffer
 * ================================================================ */

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

/* ================================================================
 * stub_save_channel_msgs - Save near active buffer to far-heap
 * ================================================================ */

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

/* ================================================================
 * stub_inject_timed_message - Add a fake message every ~5 seconds
 * ================================================================ */

void stub_inject_timed_message(dc_state_t *s)
{
    unsigned long now;
    char ts[6];

    /* Read BIOS tick counter (INT 1Ah, AH=0) */
    _asm {
        xor ax, ax
        int 1Ah
        mov word ptr now, dx
        mov word ptr now+2, cx
    }

    /* First call: seed the tick counter and return */
    if (s->last_poll_tick == 0) {
        s->last_poll_tick = now;
        return;
    }

    /* ~5 seconds = 91 ticks at 18.2 Hz */
    if ((now - s->last_poll_tick) < 91) return;
    s->last_poll_tick = now;

    /* Build a timestamp from BIOS ticks */
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

    /* Drop oldest message if #general is full */
    if (chan_msg_count[0] >= DC_MAX_MESSAGES) {
        int j;
        for (j = 0; j < DC_MAX_MESSAGES - 1; j++)
            _fmemcpy((void far *)&chan_msgs[0][j],
                     (void far *)&chan_msgs[0][j + 1],
                     sizeof(dc_message_t));
        chan_msg_count[0] = DC_MAX_MESSAGES - 1;
    }

    /* Add the timed message to #general's far-heap storage */
    add_msg(0, timed_authors[ti_auth], timed_msgs[ti_msg], ts, 0, 0);

    /* Update active view or unread counter */
    if (s->selected_channel == 0) {
        /* Viewing #general: also update the near-heap active buffer */
        if (s->msg_count >= DC_MAX_MESSAGES) {
            memmove(&s->messages[0], &s->messages[1],
                    (DC_MAX_MESSAGES - 1) * sizeof(dc_message_t));
            s->msg_count = DC_MAX_MESSAGES - 1;
            if (s->msg_scroll > 0) s->msg_scroll--;
        }
        {
            dc_message_t far *src = &chan_msgs[0][chan_msg_count[0] - 1];
            _fmemcpy((void far *)&s->messages[s->msg_count],
                     (void far *)src, sizeof(dc_message_t));
            s->msg_count++;
        }
        s->dirty = 1;
        s->flash_ticks = 3;

#if FEAT_AUDIO
        if (s->config.sound)
            dc_audio_blip();
#endif
    } else {
        /* Not viewing #general: increment unread, notify */
        s->channels[0].unread++;
        s->dirty = 1;

#if FEAT_AUDIO
        if (s->config.sound)
            dc_audio_chirp();
#endif
    }

    /* Rotate through the timed message pool */
    ti_msg = (ti_msg + 1) % NUM_TIMED_MSGS;
    ti_auth = (ti_auth + 1) % NUM_TIMED_AUTH;
}
