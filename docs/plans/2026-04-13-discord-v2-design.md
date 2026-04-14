---
ticket: "discord-v2"
title: "Discord v2: Ground-Up Rebuild"
date: "2026-04-13"
source: "design"
---

# Discord v2: Ground-Up Rebuild

## Summary

Discord v1 is a 7-module proof of concept: state machine, stub data with far-heap message pools, VGA rendering, keyboard input. It works. It proves the architecture (state machine + immediate VGA + far-heap per-channel storage) is sound for 8088 through 486.

v2 is a ground-up rebuild that keeps the proven architecture and adds everything needed for a complete Discord text client: per-channel far-heap storage with 128 messages (up from 32), multi-line compose, find in messages, reactions, thread indicators, PC speaker notifications, user list overlay, help overlay, settings persistence, and richer stub data. No near-heap message copies during rendering -- render directly from far pointers via single-message scratch buffer.

**10 source modules, ~2,500 lines of new code. Under 40KB EXE. Builds clean at -w4.**

## Target Environment

| Property | Value |
|----------|-------|
| CPU range | 8088 real mode through 486 |
| Compiler | OpenWatcom 2.0, small model (`-ms`) |
| Memory model | Near code, near data, far heap for messages |
| EXE budget | < 40KB |
| Display | 80x25 text mode (VGA mode 3), EGA compatible |
| Shared libs | `screen.c`/`screen.h` (VGA rendering), `utf8.c`/`utf8.h` (entity/UTF-8 decoding) |

## Architecture Overview

```
main.c
  |
  v
dc_init() ---------> stub_discord.c (fake data, far-heap alloc)
  |                       |
  v                       v
main loop             chan_msgs[8] (far heap, 128 msgs each)
  |
  +--- scr_kbhit() ---> input_dc.c ---> dc_handle_key()
  |                                        |
  |                                        +--- dc_switch_channel()
  |                                        +--- dc_send_message()
  |                                        +--- search_dc functions
  |                                        +--- audio_dc functions
  |
  +--- dc_poll_messages() ---> stub_inject_timed_message()
  |
  +--- dc_render_all() ---> render_dc.c
  |                            |
  |                            +--- dc_render_titlebar()
  |                            +--- dc_render_channels()
  |                            +--- dc_render_messages()  <-- far ptr scratch render
  |                            +--- dc_render_compose()
  |                            +--- dc_render_statusbar()
  |                            +--- dc_render_scrollbar()
  |                            +--- dc_render_overlay_users()
  |                            +--- dc_render_overlay_help()
  |
  +--- config_dc.c (load/save DISCORD.CFG)
```

### Main Loop (main.c)

```c
int main(void)
{
    dc_state_t state;

    scr_init();
    scr_cursor_hide();
    dc_config_load(&state.config);
    dc_init(&state);
    scr_fade_in(8, 40);
    dc_render_all(&state);

    while (state.running) {
        if (scr_kbhit()) {
            int key = scr_getkey();
            dc_handle_key(&state, key);
        }
        dc_poll_messages(&state);
        if (state.dirty) {
            dc_render_all(&state);
            state.dirty = 0;
        }
    }

    dc_config_save(&state.config);
    scr_fade_out(8, 40);
    scr_shutdown();
    return 0;
}
```

---

## Data Structures

### dc_message_t (~240 bytes)

```c
#define DC_MAX_MSG_LEN      200
#define DC_MAX_AUTHOR_LEN   16

typedef struct {
    char author[DC_MAX_AUTHOR_LEN];   /* 16 bytes: display name             */
    char text[DC_MAX_MSG_LEN];        /* 200 bytes: message body             */
    char timestamp[6];                /* 6 bytes: "HH:MM\0"                 */
    unsigned char author_color;       /* 1 byte: VGA attribute for author    */
    unsigned char reaction_bits;      /* 1 byte: bitfield, 8 reaction types  */
    int thread_id;                    /* 2 bytes: 0=no thread, >0=thread ID  */
    int is_self;                      /* 2 bytes: 1 if sent by local user    */
    /* padding to 240: 12 bytes reserved for future fields */
    char _reserved[12];
} dc_message_t;  /* Total: 240 bytes */
```

**Reaction bits layout** (`reaction_bits`):

| Bit | CP437 | Glyph | Hex |
|-----|-------|-------|-----|
| 0   | 0x03  | Heart  | `\x03` |
| 1   | 0x01  | Smiley | `\x01` |
| 2   | 0x04  | Diamond | `\x04` |
| 3   | 0x05  | Club   | `\x05` |
| 4   | 0x06  | Spade  | `\x06` |
| 5   | 0x0F  | Sun    | `\x0F` |
| 6   | 0x10  | Arrow  | `\x10` |
| 7   | 0x0E  | Note   | `\x0E` |

### dc_channel_t

```c
#define DC_MAX_CHANNELS     8
#define DC_MAX_CHAN_NAME     20

typedef struct {
    char name[DC_MAX_CHAN_NAME];   /* 20 bytes: channel name (no #)     */
    int unread;                    /* 2 bytes: unread message count     */
    int msg_count;                 /* 2 bytes: messages in far storage  */
} dc_channel_t;  /* Total: 24 bytes */
```

### dc_compose_t

```c
#define DC_MAX_COMPOSE      237   /* 79 chars x 3 lines */
#define DC_COMPOSE_COLS     77    /* usable width per line */
#define DC_COMPOSE_MAX_LINES 3

typedef struct {
    char buf[DC_MAX_COMPOSE + 1];  /* 238 bytes: full compose buffer    */
    int len;                        /* 2 bytes: current text length      */
    int cursor;                     /* 2 bytes: cursor position in buf   */
    int lines;                      /* 2 bytes: current line count (1-3) */
} dc_compose_t;  /* Total: 244 bytes */
```

### dc_search_t

Adapted from Cathode's `search_state_t`:

```c
#define DC_SEARCH_MAX        40   /* max query length */
#define DC_SEARCH_MAX_MATCHES 32  /* max tracked match positions */

typedef struct {
    int msg_idx;    /* message index in channel */
    int char_pos;   /* character offset within message text */
} dc_search_pos_t;

typedef struct {
    char query[DC_SEARCH_MAX + 1];
    int query_len;
    int active;          /* 1 = search mode active */
    int editing;         /* 1 = typing in search bar */
    int cursor;          /* cursor position within query */
    int current_idx;     /* index into matches[] */
    int total_matches;
    dc_search_pos_t matches[DC_SEARCH_MAX_MATCHES];
} dc_search_t;  /* Total: ~220 bytes */
```

### dc_config_t

```c
typedef struct {
    int sound;           /* 0=off, 1=on (F9 toggle) */
    int notify;          /* 0=off, 1=on */
    int last_channel;    /* 0-7: last selected channel */
    int color_scheme;    /* 0=default (reserved for future themes) */
    char username[DC_MAX_AUTHOR_LEN];  /* display name */
} dc_config_t;  /* Total: 24 bytes */
```

### dc_state_t (master state, near heap)

```c
#define DC_MAX_SERVER_NAME  24

typedef struct {
    /* Server metadata */
    char server_name[DC_MAX_SERVER_NAME];

    /* Channel list */
    dc_channel_t channels[DC_MAX_CHANNELS];
    int channel_count;
    int selected_channel;

    /* Message rendering scratch buffer (ONE message at a time) */
    dc_message_t scratch;           /* 240 bytes: near copy for rendering */

    /* Scroll state */
    int msg_scroll;                 /* scroll offset from bottom (0=latest) */

    /* UI state */
    int focus;                      /* DC_FOCUS_CHANNELS/MESSAGES/COMPOSE */
    dc_compose_t compose;
    dc_search_t search;
    dc_config_t config;

    /* Overlay states */
    int show_users;                 /* 1 = user list overlay visible */
    int show_help;                  /* 1 = help overlay visible */

    /* Notification flash */
    int flash_ticks;                /* countdown: >0 means title bar flashing */

    /* Runtime control */
    int running;
    int dirty;                      /* screen needs redraw */
    unsigned long last_poll_tick;
} dc_state_t;
```

**Near heap budget for dc_state_t:**

| Field | Size (bytes) |
|-------|-------------|
| server_name | 24 |
| channels[8] | 192 |
| channel_count + selected_channel | 4 |
| scratch | 240 |
| msg_scroll | 2 |
| focus | 2 |
| compose | 244 |
| search | ~220 |
| config | 24 |
| show_users + show_help | 4 |
| flash_ticks | 2 |
| running + dirty | 4 |
| last_poll_tick | 4 |
| **Total** | **~966 bytes** |

This fits comfortably in DGROUP. The critical change from v1: **no near-heap message array**. The `messages[DC_MAX_MESSAGES]` array (v1: 32 x 232 = 7,424 bytes) is eliminated. Messages live exclusively on the far heap. The renderer copies one message at a time into `scratch` (240 bytes) for processing.

---

## Memory Layout

### Far Heap: Per-Channel Message Pools

```
Far Heap Map (total ~245,760 bytes = ~240KB)
============================================

chan_msgs[0] --> [dc_message_t x 128]  = 30,720 bytes  (#general)
chan_msgs[1] --> [dc_message_t x 128]  = 30,720 bytes  (#hardware)
chan_msgs[2] --> [dc_message_t x 128]  = 30,720 bytes  (#builds)
chan_msgs[3] --> [dc_message_t x 128]  = 30,720 bytes  (#software)
chan_msgs[4] --> [dc_message_t x 128]  = 30,720 bytes  (#marketplace)
chan_msgs[5] --> [dc_message_t x 128]  = 30,720 bytes  (#off-topic)
chan_msgs[6] --> [dc_message_t x 128]  = 30,720 bytes  (#help)
chan_msgs[7] --> [dc_message_t x 128]  = 30,720 bytes  (#showcase)
```

Each allocation: `128 * 240 = 30,720 bytes`. Since `_fmalloc` takes `unsigned int` (max 65,535), this fits. 8 allocations total.

### Far Heap Allocation Strategy

```c
#define DC_MAX_MESSAGES     128

static dc_message_t far *chan_msgs[DC_MAX_CHANNELS];
static int chan_msg_count[DC_MAX_CHANNELS];

/* Called during stub_load_server() */
for (i = 0; i < DC_MAX_CHANNELS; i++) {
    unsigned alloc_size = DC_MAX_MESSAGES * sizeof(dc_message_t);
    chan_msgs[i] = (dc_message_t far *)_fmalloc(alloc_size);
    if (chan_msgs[i])
        _fmemset((void far *)chan_msgs[i], 0, alloc_size);
    chan_msg_count[i] = 0;
}
```

### Near Heap (DGROUP) Budget

| Item | Size |
|------|------|
| dc_state_t (stack/static) | ~966 |
| Stack (default 4KB) | 4,096 |
| String literals (channel names, stub messages, help text) | ~3,000 |
| Static variables (color tables, reaction tables, etc.) | ~200 |
| C runtime overhead | ~2,000 |
| **Total estimated DGROUP** | **~10,262 bytes** |

Well under the 64KB DGROUP limit.

### Rendering: Far-to-Near Scratch Copy

The renderer never bulk-copies messages to near heap. For each visible message:

```c
/* In dc_render_messages(): */
for (i = first_visible; i <= last_visible; i++) {
    dc_message_t far *src = &chan_msgs[ch][i];
    _fmemcpy(&state->scratch, (void far *)src, sizeof(dc_message_t));
    /* Now render from state->scratch (near pointer) */
    render_single_message(state, &state->scratch, row);
    row += lines_used;
}
```

This means at any given moment, only 240 bytes of message data exist in near heap -- the single message currently being rendered.

---

## UI Layout (80x25)

```
Col: 0         18 19 20                                              79
     +----------+--+-------------------------------------------+-----+
  0  | Retro Computing Hub              Connected  Discord v2.0      | Title bar
  1  |══════════════════════════════════════════════════════════════════| Separator
  2  | # general   3  │ [14:02] VintageNerd: Hey everyone! XT-IDE   ░| Content
  3  | # hardware     │   rev4 soldered up. First try boot from CF  ░| area
  4  | # builds       │   card!                                     ░| (20 rows)
  5  | # software     │ [14:03] RetroGamer: Nice! Which CF adapter? ░|
  6  | # marketplace  │ [14:04] VintageNerd: Generic 40-pin IDE to  ░|
  7  | # off-topic    │   CF. DOS 6.22, 32MB partition              ░|
  8  | # help         │ [14:05] ChipCollector: 32MB? Living large.  █|
  9  | # showcase     │   My 8088 only has 20MB                     ░|
 10  |                │ [14:07] BarelyBooting: Anyone seen the      ░|
 11  |                │   NetISA project? ISA card with TLS 1.3     ░|
 12  |                │   offload                                   ░|
 13  |                │ [14:08] DOSenthusiast: Yeah, an 8087 but    ░|
 14  |                │   for crypto. Love it  ♥3 ☺1                ░|
 15  |                │ [14:09] RetroGamer: Real HTTPS from a 286?  ░|
 16  |                │   That works?                               ░|
 17  |                │ [14:10] BarelyBooting: ESP32-S3 handles     ░|
 18  |                │   crypto. DOS sends cleartext via INT 63h.  ░|
 19  |                │   Even 8088 can do it  [↳ 3 replies]        ░|
 20  |                │ [14:11] VintageNerd: Browse github from an  ░|
 21  |                │   XT? Wild                                  ░|
 22  |══════════════════════════════════════════════════════════════════| Separator
 23  | > Hello everyone! This is a test message_                      | Compose
 24  | #general  15 msgs  3 unread  Tab:focus Alt+1-8:ch F1:help F9:♪| Status
     +----------------------------------------------------------------+
```

### Layout Constants

```c
/* Row assignments */
#define DC_TITLE_ROW        0
#define DC_SEP1_ROW         1
#define DC_CONTENT_TOP      2
#define DC_CONTENT_BOT      21
#define DC_CONTENT_ROWS     20    /* rows 2-21 inclusive */
#define DC_SEP2_ROW         22
#define DC_COMPOSE_ROW      23
#define DC_STATUS_ROW       24

/* Column assignments */
#define DC_CHAN_WIDTH        18    /* channel pane: cols 0-17 */
#define DC_CHAN_SEP_COL      18   /* vertical separator at col 18 */
#define DC_MSG_LEFT          19   /* message pane starts at col 19 */
#define DC_MSG_WIDTH         60   /* message pane: cols 19-78 */
#define DC_SCROLL_COL        79   /* scrollbar at col 79 */
```

### Attribute Definitions (discord.h)

```c
/* Discord-specific attributes */
#define DC_ATTR_TITLE       SCR_ATTR(SCR_WHITE, SCR_BLUE)
#define DC_ATTR_TITLE_CONN  SCR_ATTR(SCR_LIGHTGREEN, SCR_BLUE)
#define DC_ATTR_TITLE_VER   SCR_ATTR(SCR_LIGHTCYAN, SCR_BLUE)
#define DC_ATTR_SEP         SCR_ATTR(SCR_DARKGRAY, SCR_BLACK)
#define DC_ATTR_CHAN_SEL     SCR_ATTR(SCR_WHITE, SCR_BLUE)
#define DC_ATTR_CHAN_UNREAD  SCR_ATTR(SCR_WHITE, SCR_BLACK)
#define DC_ATTR_CHAN_READ    SCR_ATTR(SCR_DARKGRAY, SCR_BLACK)
#define DC_ATTR_CHAN_HASH    SCR_ATTR(SCR_DARKGRAY, SCR_BLACK)
#define DC_ATTR_TIMESTAMP   SCR_ATTR(SCR_DARKGRAY, SCR_BLACK)
#define DC_ATTR_MSG_TEXT    SCR_ATTR(SCR_LIGHTGRAY, SCR_BLACK)
#define DC_ATTR_MSG_SELF    SCR_ATTR(SCR_LIGHTGREEN, SCR_BLACK)
#define DC_ATTR_COMPOSE     SCR_ATTR(SCR_WHITE, SCR_BLACK)
#define DC_ATTR_COMPOSE_BG  SCR_ATTR(SCR_DARKGRAY, SCR_BLACK)
#define DC_ATTR_STATUS      SCR_ATTR(SCR_BLACK, SCR_LIGHTGRAY)
#define DC_ATTR_REACTION    SCR_ATTR(SCR_YELLOW, SCR_BLACK)
#define DC_ATTR_THREAD      SCR_ATTR(SCR_CYAN, SCR_BLACK)
#define DC_ATTR_MENTION     SCR_ATTR(SCR_WHITE, SCR_BLUE)
#define DC_ATTR_FLASH_INV   SCR_ATTR(SCR_BLUE, SCR_WHITE)

/* Search attributes (matched from Cathode's search.h) */
#define DC_ATTR_SEARCH_HIT  SCR_ATTR(SCR_BLACK, SCR_YELLOW)
#define DC_ATTR_SEARCH_CUR  SCR_ATTR(SCR_BLACK, SCR_WHITE)

/* Overlay attributes */
#define DC_ATTR_OVERLAY_BG     SCR_ATTR(SCR_WHITE, SCR_BLUE)
#define DC_ATTR_OVERLAY_BORDER SCR_ATTR(SCR_LIGHTCYAN, SCR_BLUE)
#define DC_ATTR_OVERLAY_TEXT   SCR_ATTR(SCR_WHITE, SCR_BLUE)
#define DC_ATTR_OVERLAY_KEY    SCR_ATTR(SCR_YELLOW, SCR_BLUE)
#define DC_ATTR_OVERLAY_SHADOW SCR_ATTR(SCR_DARKGRAY, SCR_BLACK)
```

### Author Color Palette (8 colors, hash-based)

```c
#define DC_NUM_AUTHOR_COLORS 8

static const unsigned char dc_author_colors[DC_NUM_AUTHOR_COLORS] = {
    SCR_ATTR(SCR_LIGHTCYAN, SCR_BLACK),      /* 0 */
    SCR_ATTR(SCR_LIGHTMAGENTA, SCR_BLACK),   /* 1 */
    SCR_ATTR(SCR_YELLOW, SCR_BLACK),         /* 2 */
    SCR_ATTR(SCR_LIGHTBLUE, SCR_BLACK),      /* 3 */
    SCR_ATTR(SCR_LIGHTRED, SCR_BLACK),       /* 4 */
    SCR_ATTR(SCR_LIGHTGREEN, SCR_BLACK),     /* 5 */
    SCR_ATTR(SCR_CYAN, SCR_BLACK),           /* 6 */
    SCR_ATTR(SCR_MAGENTA, SCR_BLACK),        /* 7 */
};

/* Hash author name to color index */
unsigned char dc_author_color(const char *name)
{
    unsigned int hash = 0;
    while (*name) {
        hash = hash * 31 + (unsigned char)*name;
        name++;
    }
    return dc_author_colors[hash % DC_NUM_AUTHOR_COLORS];
}
```

This ensures the same author always gets the same color, distributed across both bright and dim CGA variants.

---

## Module Specifications

### discord_cfg.h -- Feature Flags

```c
#ifndef DISCORD_CFG_H
#define DISCORD_CFG_H

#define FEAT_AUDIO      1    /* PC speaker notifications */
#define FEAT_SEARCH     1    /* Ctrl+F find in messages */
#define FEAT_THREADS    0    /* Thread view (Phase 2) */
#define FEAT_USERS      1    /* Alt+U user list overlay */
#define FEAT_REACTIONS  1    /* CP437 reaction display */
#define FEAT_MULTILINE  1    /* Shift+Enter multi-line compose */

#endif /* DISCORD_CFG_H */
```

Usage pattern throughout code:

```c
#if FEAT_AUDIO
    dc_audio_blip(&state);
#endif
```

### discord.h -- Types, Constants, Prototypes

Contains all `typedef` definitions listed above, all `#define` layout and attribute constants, and the following function prototypes:

```c
/* discord.c -- State machine */
void dc_init(dc_state_t *s);
void dc_switch_channel(dc_state_t *s, int channel_idx);
void dc_send_message(dc_state_t *s, const char *text);
void dc_poll_messages(dc_state_t *s);
int  dc_get_channel_msg_count(int ch_idx);
dc_message_t far *dc_get_channel_msg(int ch_idx, int msg_idx);

/* render_dc.c -- Renderer */
void dc_render_all(dc_state_t *s);
void dc_render_titlebar(dc_state_t *s);
void dc_render_channels(dc_state_t *s);
void dc_render_messages(dc_state_t *s);
void dc_render_compose(dc_state_t *s);
void dc_render_statusbar(dc_state_t *s);
void dc_render_scrollbar(dc_state_t *s);
void dc_render_overlay_users(dc_state_t *s);
void dc_render_overlay_help(dc_state_t *s);
void dc_render_search_highlights(dc_state_t *s);

/* input_dc.c -- Keyboard handler */
void dc_handle_key(dc_state_t *s, int key);

/* audio_dc.c -- PC speaker */
void dc_audio_blip(void);
void dc_audio_chirp(void);
void dc_audio_mention(void);
void dc_audio_error(void);

/* config_dc.c -- Settings persistence */
void dc_config_load(dc_config_t *cfg);
void dc_config_save(dc_config_t *cfg);
void dc_config_defaults(dc_config_t *cfg);

/* search_dc.c -- Find in messages */
void dc_search_init(dc_search_t *s);
void dc_search_start(dc_search_t *s);
void dc_search_cancel(dc_search_t *s);
int  dc_search_handle_key(dc_search_t *s, int key);
void dc_search_execute(dc_search_t *s, int ch_idx);
void dc_search_next(dc_search_t *s, int direction);
int  dc_search_is_hit(dc_search_t *s, int msg_idx, int char_pos, int len);

/* stub_discord.c -- Fake data provider */
void stub_load_server(dc_state_t *s);
void stub_inject_timed_message(dc_state_t *s);
```

### discord.c -- State Machine

**Responsibilities**: Initialize state, switch channels, send messages, poll for new messages. No rendering. No input handling.

#### dc_init

```c
void dc_init(dc_state_t *s)
{
    memset(s, 0, sizeof(dc_state_t));
    s->running = 1;
    s->focus = DC_FOCUS_COMPOSE;
    s->selected_channel = s->config.last_channel;
    s->compose.lines = 1;
    dc_search_init(&s->search);

    stub_load_server(s);
    s->dirty = 1;
}
```

Note: `config` is loaded by `main()` before `dc_init()`, so `last_channel` is available.

#### dc_switch_channel

```c
void dc_switch_channel(dc_state_t *s, int channel_idx)
{
    if (channel_idx < 0 || channel_idx >= s->channel_count) return;
    if (channel_idx == s->selected_channel) return;

    s->selected_channel = channel_idx;
    s->channels[channel_idx].unread = 0;
    s->msg_scroll = 0;

    /* Cancel any active search */
    if (s->search.active)
        dc_search_cancel(&s->search);

    s->dirty = 1;
}
```

Key change from v1: **no save/load cycle**. Messages stay on far heap permanently. Channel switch just changes `selected_channel` and resets scroll. The renderer reads directly from `chan_msgs[selected_channel]` via the scratch buffer.

#### dc_send_message

```c
void dc_send_message(dc_state_t *s, const char *text)
{
    int ch = s->selected_channel;
    int count = chan_msg_count[ch];
    dc_message_t far *m;
    char ts[6];

    if (!text || !text[0]) return;

    /* Circular buffer: if full, shift all messages down by 1 */
    if (count >= DC_MAX_MESSAGES) {
        int j;
        for (j = 0; j < DC_MAX_MESSAGES - 1; j++) {
            _fmemcpy((void far *)&chan_msgs[ch][j],
                     (void far *)&chan_msgs[ch][j + 1],
                     sizeof(dc_message_t));
        }
        count = DC_MAX_MESSAGES - 1;
    }

    m = &chan_msgs[ch][count];
    /* Build timestamp from BIOS tick counter */
    dc_build_timestamp(ts);
    _fstrncpy((char far *)m->author, (char far *)s->config.username,
              DC_MAX_AUTHOR_LEN - 1);
    m->author[DC_MAX_AUTHOR_LEN - 1] = '\0';
    _fstrncpy((char far *)m->text, (char far *)text, DC_MAX_MSG_LEN - 1);
    m->text[DC_MAX_MSG_LEN - 1] = '\0';
    _fstrncpy((char far *)m->timestamp, (char far *)ts, 5);
    m->timestamp[5] = '\0';
    m->author_color = SCR_ATTR(SCR_LIGHTGREEN, SCR_BLACK);
    m->is_self = 1;
    m->reaction_bits = 0;
    m->thread_id = 0;
    chan_msg_count[ch] = count + 1;

    /* Update channel metadata */
    s->channels[ch].msg_count = count + 1;

    /* Clear compose buffer */
    s->compose.buf[0] = '\0';
    s->compose.len = 0;
    s->compose.cursor = 0;
    s->compose.lines = 1;

    /* Scroll to bottom */
    s->msg_scroll = 0;
    s->dirty = 1;
}
```

#### dc_build_timestamp (internal helper)

```c
static void dc_build_timestamp(char *ts)
{
    unsigned long ticks;
    unsigned long secs;
    unsigned char hr, mn;

    _asm {
        xor ax, ax
        int 1Ah
        mov word ptr ticks, dx
        mov word ptr ticks+2, cx
    }

    secs = ticks / 18;
    hr = (unsigned char)((secs / 3600) % 24);
    mn = (unsigned char)((secs / 60) % 60);
    ts[0] = (char)('0' + hr / 10);
    ts[1] = (char)('0' + hr % 10);
    ts[2] = ':';
    ts[3] = (char)('0' + mn / 10);
    ts[4] = (char)('0' + mn % 10);
    ts[5] = '\0';
}
```

#### dc_get_channel_msg_count / dc_get_channel_msg

Accessor functions for far-heap message data, used by renderer and search:

```c
int dc_get_channel_msg_count(int ch_idx)
{
    if (ch_idx < 0 || ch_idx >= DC_MAX_CHANNELS) return 0;
    return chan_msg_count[ch_idx];
}

dc_message_t far *dc_get_channel_msg(int ch_idx, int msg_idx)
{
    if (ch_idx < 0 || ch_idx >= DC_MAX_CHANNELS) return NULL;
    if (msg_idx < 0 || msg_idx >= chan_msg_count[ch_idx]) return NULL;
    return &chan_msgs[ch_idx][msg_idx];
}
```

### render_dc.c -- Renderer

**Responsibilities**: Draw every UI element. Never modifies state (read-only access). Uses `scr_*` functions from `screen.h`.

#### dc_render_all

```c
void dc_render_all(dc_state_t *s)
{
    dc_render_titlebar(s);
    dc_render_separator(DC_SEP1_ROW);
    dc_render_channels(s);
    dc_render_messages(s);
    dc_render_scrollbar(s);
    dc_render_separator(DC_SEP2_ROW);
    dc_render_compose(s);

    if (s->search.active && s->search.editing)
        dc_render_searchbar(s);
    else
        dc_render_statusbar(s);

    /* Overlays (drawn last, on top of everything) */
    if (s->show_users)
        dc_render_overlay_users(s);
    if (s->show_help)
        dc_render_overlay_help(s);
}
```

#### dc_render_titlebar

```c
void dc_render_titlebar(dc_state_t *s)
{
    unsigned char attr = DC_ATTR_TITLE;

    /* Flash effect: invert when flash_ticks > 0 */
    if (s->flash_ticks > 0) {
        attr = DC_ATTR_FLASH_INV;
        s->flash_ticks--;
    }

    scr_fill(0, DC_TITLE_ROW, SCR_WIDTH, 1, ' ', attr);
    scr_puts(1, DC_TITLE_ROW, s->server_name, attr);
    scr_puts(50, DC_TITLE_ROW, "Connected", DC_ATTR_TITLE_CONN);
    scr_puts(66, DC_TITLE_ROW, "Discord v2.0", DC_ATTR_TITLE_VER);
}
```

#### dc_render_separator

```c
static void dc_render_separator(int row)
{
    scr_hline(0, row, SCR_WIDTH, (char)0xCD, DC_ATTR_SEP);  /* CP437 double line */
}
```

#### dc_render_channels

```c
void dc_render_channels(dc_state_t *s)
{
    int i, row;
    char line[DC_CHAN_WIDTH + 1];

    for (i = 0; i < DC_CONTENT_ROWS; i++) {
        scr_fill(0, DC_CONTENT_TOP + i, DC_CHAN_WIDTH, 1, ' ', ATTR_NORMAL);
        /* Draw vertical separator */
        scr_putc(DC_CHAN_SEP_COL, DC_CONTENT_TOP + i, (char)0xB3, DC_ATTR_SEP);
    }

    for (i = 0; i < s->channel_count; i++) {
        unsigned char attr;
        row = DC_CONTENT_TOP + i;

        if (i == s->selected_channel) {
            attr = DC_ATTR_CHAN_SEL;
            scr_fill(0, row, DC_CHAN_WIDTH, 1, ' ', attr);
        } else if (s->channels[i].unread > 0) {
            attr = DC_ATTR_CHAN_UNREAD;
        } else {
            attr = DC_ATTR_CHAN_READ;
        }

        /* "# channel-name" */
        scr_putc(1, row, '#', DC_ATTR_CHAN_HASH);
        scr_putsn(3, row, s->channels[i].name, 12, attr);

        /* Unread count (if > 0 and not selected) */
        if (s->channels[i].unread > 0 && i != s->selected_channel) {
            char cnt[4];
            cnt[0] = ' ';
            if (s->channels[i].unread < 10) {
                cnt[1] = (char)('0' + s->channels[i].unread);
                cnt[2] = '\0';
            } else {
                cnt[1] = (char)('0' + s->channels[i].unread / 10);
                cnt[2] = (char)('0' + s->channels[i].unread % 10);
                cnt[3] = '\0';
            }
            scr_puts(15, row, cnt, DC_ATTR_CHAN_UNREAD);
        }
    }

    /* Focus indicator: highlight border of channel pane when focused */
    if (s->focus == DC_FOCUS_CHANNELS) {
        scr_vline(DC_CHAN_SEP_COL, DC_CONTENT_TOP, DC_CONTENT_ROWS,
                  (char)0xB3, ATTR_HIGHLIGHT);
    }
}
```

#### dc_render_messages

This is the most complex render function. It must:
1. Calculate which messages are visible given scroll position
2. Word-wrap each message's text to fit the message pane width
3. Copy each message from far heap to scratch buffer one at a time
4. Render author (colored), timestamp, text, reactions, thread indicators
5. Handle search highlighting

```c
void dc_render_messages(dc_state_t *s)
{
    int ch = s->selected_channel;
    int total = dc_get_channel_msg_count(ch);
    int row = DC_CONTENT_TOP;
    int visible_rows = DC_CONTENT_ROWS;
    int msg_idx;
    int lines_used;

    /* Clear message area */
    scr_fill(DC_MSG_LEFT, DC_CONTENT_TOP, DC_MSG_WIDTH, DC_CONTENT_ROWS,
             ' ', ATTR_NORMAL);

    if (total == 0) {
        scr_puts(DC_MSG_LEFT + 2, DC_CONTENT_TOP + 8, "No messages yet.",
                 ATTR_DIM);
        return;
    }

    /* Calculate starting message index based on scroll position.
     * msg_scroll=0 means show latest messages (bottom-anchored).
     * We render bottom-up conceptually, then display top-down. */

    /* First pass: calculate how many rows each message needs (word wrap).
     * We work backward from the last message. */
    {
        int rows_needed = 0;
        int start_idx = total - 1 - s->msg_scroll;
        int first_idx = start_idx;

        /* Walk backward to find the first message that fits */
        while (first_idx >= 0 && rows_needed < visible_rows) {
            _fmemcpy(&s->scratch, (void far *)dc_get_channel_msg(ch, first_idx),
                     sizeof(dc_message_t));
            rows_needed += dc_calc_msg_lines(&s->scratch);
            if (rows_needed <= visible_rows)
                first_idx--;
        }
        first_idx++;  /* went one too far */
        if (first_idx < 0) first_idx = 0;

        /* Second pass: render forward from first_idx */
        for (msg_idx = first_idx;
             msg_idx <= start_idx && row <= DC_CONTENT_BOT;
             msg_idx++) {
            _fmemcpy(&s->scratch, (void far *)dc_get_channel_msg(ch, msg_idx),
                     sizeof(dc_message_t));
            lines_used = dc_render_single_message(s, &s->scratch, row,
                                                   msg_idx);
            row += lines_used;
        }
    }
}
```

#### dc_calc_msg_lines (internal helper)

Calculates how many screen rows a message occupies including author line and word-wrapped text:

```c
/* Returns row count for one message:
 * Line 1: [HH:MM] Author: <start of text>
 * Line 2+: continuation lines (word wrap at DC_MSG_WIDTH - 2) */
static int dc_calc_msg_lines(dc_message_t *msg)
{
    int text_width = DC_MSG_WIDTH - 2;  /* 2-char indent for continuation */
    int first_line_text;
    int text_len = (int)strlen(msg->text);
    int lines = 1;
    int remaining;
    int suffix_len = 0;

    /* First line: "[HH:MM] Author: " prefix uses some columns */
    /* Prefix: 8 (timestamp+space) + author_len + 2 (": ") */
    first_line_text = DC_MSG_WIDTH - 8 - (int)strlen(msg->author) - 2;
    if (first_line_text < 10) first_line_text = 10;

#if FEAT_REACTIONS
    if (msg->reaction_bits) suffix_len += 20;  /* worst case reactions */
#endif
#if FEAT_THREADS
    if (msg->thread_id > 0) suffix_len += 15;  /* "[^ N replies]" */
#endif

    remaining = text_len + suffix_len - first_line_text;
    while (remaining > 0) {
        lines++;
        remaining -= text_width;
    }
    return lines;
}
```

#### dc_render_single_message

```c
static int dc_render_single_message(dc_state_t *s, dc_message_t *msg,
                                     int row, int msg_idx)
{
    int col = DC_MSG_LEFT;
    int lines = 0;
    int text_col;
    unsigned char text_attr;

    if (row > DC_CONTENT_BOT) return 0;

    /* Timestamp: [HH:MM] */
    scr_putc(col, row, '[', DC_ATTR_TIMESTAMP);
    scr_putsn(col + 1, row, msg->timestamp, 5, DC_ATTR_TIMESTAMP);
    scr_putc(col + 6, row, ']', DC_ATTR_TIMESTAMP);
    col += 8;

    /* Author name (colored) */
    scr_puts(col, row, msg->author, msg->author_color);
    col += (int)strlen(msg->author);
    scr_puts(col, row, ": ", DC_ATTR_MSG_TEXT);
    col += 2;

    /* Message text with word wrap */
    text_attr = msg->is_self ? DC_ATTR_MSG_SELF : DC_ATTR_MSG_TEXT;
    text_col = col;
    lines = dc_render_wordwrap(s, msg->text, text_col, row,
                                DC_MSG_LEFT + DC_MSG_WIDTH,
                                DC_MSG_LEFT + 2, /* indent for continuation */
                                text_attr, msg_idx);

    /* Reactions (on last line of message) */
#if FEAT_REACTIONS
    if (msg->reaction_bits) {
        int last_row = row + lines - 1;
        dc_render_reactions(msg->reaction_bits, DC_MSG_LEFT + 2, last_row + 1);
        if (last_row + 1 <= DC_CONTENT_BOT) lines++;
    }
#endif

    /* Thread indicator */
#if FEAT_THREADS
    if (msg->thread_id > 0) {
        /* Rendered inline at end of text or on reaction line */
    }
#endif

    return lines;
}
```

#### dc_render_wordwrap

Renders text with word-boundary wrapping, returning the number of lines used:

```c
static int dc_render_wordwrap(dc_state_t *s, const char *text,
                               int start_col, int start_row,
                               int right_edge, int indent_col,
                               unsigned char attr, int msg_idx)
{
    int col = start_col;
    int row = start_row;
    int lines = 1;
    int i = 0;
    int text_len = (int)strlen(text);

    while (i < text_len && row <= DC_CONTENT_BOT) {
        /* Find next word boundary */
        int word_start = i;
        int word_end = i;
        unsigned char ch_attr = attr;

        while (word_end < text_len && text[word_end] != ' ')
            word_end++;

        /* Check if word fits on current line */
        if (col + (word_end - word_start) > right_edge && col > indent_col) {
            /* Wrap to next line */
            row++;
            col = indent_col;
            lines++;
            if (row > DC_CONTENT_BOT) break;
        }

        /* Render word character by character */
        while (i < word_end && col < right_edge) {
#if FEAT_SEARCH
            if (s->search.active) {
                if (dc_search_is_hit(&s->search, msg_idx, i,
                                      s->search.query_len)) {
                    ch_attr = (s->search.matches[s->search.current_idx].msg_idx == msg_idx &&
                               s->search.matches[s->search.current_idx].char_pos == i)
                              ? DC_ATTR_SEARCH_CUR : DC_ATTR_SEARCH_HIT;
                } else {
                    ch_attr = attr;
                }
            }
#endif
            scr_putc(col, row, text[i], ch_attr);
            col++;
            i++;
        }

        /* Skip space */
        if (i < text_len && text[i] == ' ') {
            if (col < right_edge)
                scr_putc(col, row, ' ', attr);
            col++;
            i++;
        }
    }

    return lines;
}
```

#### dc_render_reactions

```c
/* CP437 reaction glyphs and their bit positions */
static const unsigned char reaction_glyphs[8] = {
    0x03, 0x01, 0x04, 0x05, 0x06, 0x0F, 0x10, 0x0E
};

static void dc_render_reactions(unsigned char bits, int col, int row)
{
    int i;
    if (row > DC_CONTENT_BOT) return;

    for (i = 0; i < 8; i++) {
        if (bits & (1 << i)) {
            scr_putc(col, row, (char)reaction_glyphs[i], DC_ATTR_REACTION);
            col++;
            /* Show count placeholder (always "1" in stub mode) */
            scr_putc(col, row, '1', DC_ATTR_REACTION);
            col += 2;  /* glyph + count + space */
        }
    }
}
```

#### dc_render_scrollbar

Reuses Cathode's proportional thumb algorithm:

```c
void dc_render_scrollbar(dc_state_t *s)
{
    int ch = s->selected_channel;
    int total = dc_get_channel_msg_count(ch);
    int vp = DC_CONTENT_ROWS;
    int max_scroll;
    int thumb_row, r;

    /* Estimate total rows (rough: assume 1.5 lines per message avg) */
    int total_lines = (total * 3) / 2;
    max_scroll = total_lines - vp;

    if (max_scroll <= 0) {
        /* No scrollbar needed, clear column */
        for (r = 0; r < vp; r++)
            scr_putc(DC_SCROLL_COL, DC_CONTENT_TOP + r, ' ', ATTR_NORMAL);
        return;
    }

    thumb_row = (s->msg_scroll * (vp - 1)) / max_scroll;
    if (thumb_row >= vp) thumb_row = vp - 1;

    for (r = 0; r < vp; r++) {
        if (r == thumb_row)
            scr_putc(DC_SCROLL_COL, DC_CONTENT_TOP + r, (char)0xDB, ATTR_BORDER);
        else
            scr_putc(DC_SCROLL_COL, DC_CONTENT_TOP + r, (char)0xB0, ATTR_DIM);
    }
}
```

#### dc_render_compose

```c
void dc_render_compose(dc_state_t *s)
{
    unsigned char attr = (s->focus == DC_FOCUS_COMPOSE)
                         ? DC_ATTR_COMPOSE : DC_ATTR_COMPOSE_BG;

    scr_fill(0, DC_COMPOSE_ROW, SCR_WIDTH, 1, ' ', attr);
    scr_putc(0, DC_COMPOSE_ROW, '>', ATTR_DIM);
    scr_putc(1, DC_COMPOSE_ROW, ' ', attr);

    scr_putsn(2, DC_COMPOSE_ROW, s->compose.buf, SCR_WIDTH - 3, attr);

    if (s->focus == DC_FOCUS_COMPOSE) {
        int cpos = s->compose.cursor + 2;
        if (cpos >= SCR_WIDTH) cpos = SCR_WIDTH - 1;
        scr_cursor_show();
        scr_cursor_pos(cpos, DC_COMPOSE_ROW);
    } else {
        scr_cursor_hide();
    }
}
```

For multi-line compose (when `s->compose.lines > 1`), the compose expands upward:

```c
/* Multi-line compose uses row 22 (overwrites separator) and row 23 */
/* Line 1: row 22 (or DC_COMPOSE_ROW - 1 for 2 lines) */
/* Line 2: row 23 (DC_COMPOSE_ROW) */
/* Line 3: replaces status bar (row 24) temporarily */
```

#### dc_render_statusbar

```c
void dc_render_statusbar(dc_state_t *s)
{
    int ch = s->selected_channel;
    int count = dc_get_channel_msg_count(ch);
    char status[81];

    scr_fill(0, DC_STATUS_ROW, SCR_WIDTH, 1, ' ', DC_ATTR_STATUS);

    /* Channel name and stats */
    sprintf(status, " #%-12s %3d msgs  %d unread  Tab:focus Alt+1-8:ch F1:help F9:%c",
            s->channels[ch].name, count, s->channels[ch].unread,
            s->config.sound ? '\x0E' : 'x');  /* musical note or x */
    scr_putsn(0, DC_STATUS_ROW, status, SCR_WIDTH, DC_ATTR_STATUS);
}
```

#### dc_render_searchbar

Replaces status bar while search is active:

```c
static void dc_render_searchbar(dc_state_t *s)
{
    char bar[81];

    scr_fill(0, DC_STATUS_ROW, SCR_WIDTH, 1, ' ', DC_ATTR_STATUS);

    sprintf(bar, " Find: %-40s  %d/%d  N:next Esc:cancel",
            s->search.query,
            s->search.total_matches > 0 ? s->search.current_idx + 1 : 0,
            s->search.total_matches);
    scr_putsn(0, DC_STATUS_ROW, bar, SCR_WIDTH, DC_ATTR_STATUS);

    /* Position cursor in search query */
    scr_cursor_show();
    scr_cursor_pos(7 + s->search.cursor, DC_STATUS_ROW);
}
```

#### dc_render_overlay_help

Centered 50x18 popup with shadow:

```c
void dc_render_overlay_help(dc_state_t *s)
{
    int x = 15, y = 3, w = 50, h = 18;

    /* Shadow (bottom and right edges) */
    scr_fill(x + 1, y + h, w, 1, ' ', DC_ATTR_OVERLAY_SHADOW);
    scr_vline(x + w, y + 1, h, ' ', DC_ATTR_OVERLAY_SHADOW);

    /* Box */
    scr_box(x, y, w, h, DC_ATTR_OVERLAY_BORDER);
    scr_fill(x + 1, y + 1, w - 2, h - 2, ' ', DC_ATTR_OVERLAY_BG);

    /* Title */
    scr_puts(x + 17, y, " Help ", DC_ATTR_OVERLAY_BORDER);

    /* Keybind table (see Keybind Table section below) */
    dc_render_help_content(x + 2, y + 2);

    /* Footer */
    scr_puts(x + 12, y + h - 1, " Press any key ", DC_ATTR_OVERLAY_BORDER);

    (void)s;
}
```

#### dc_render_overlay_users

```c
void dc_render_overlay_users(dc_state_t *s)
{
    int x = 55, y = 2, w = 24, h = 14;
    int i;

    /* Shadow */
    scr_fill(x + 1, y + h, w, 1, ' ', DC_ATTR_OVERLAY_SHADOW);
    scr_vline(x + w, y + 1, h, ' ', DC_ATTR_OVERLAY_SHADOW);

    /* Box */
    scr_box(x, y, w, h, DC_ATTR_OVERLAY_BORDER);
    scr_fill(x + 1, y + 1, w - 2, h - 2, ' ', DC_ATTR_OVERLAY_BG);

    /* Title */
    scr_puts(x + 6, y, " Users Online ", DC_ATTR_OVERLAY_BORDER);

    /* User list */
    for (i = 0; i < 8; i++) {
        unsigned char color = dc_author_colors[i];
        /* Render user name with their assigned color */
        scr_putc(x + 2, y + 2 + i, (char)0xFE, color);  /* bullet */
        scr_puts(x + 4, y + 2 + i, stub_users[i],
                 DC_ATTR_OVERLAY_TEXT);
    }

    (void)s;
}
```

### input_dc.c -- Keyboard Handler

**Responsibilities**: Translate raw scancodes into state machine actions. Handle compose editing. Manage focus transitions.

#### Extended Key Constants

```c
/* ALT+key scan codes (high byte from INT 16h AH=00h) */
#define KEY_ALT_1       0x7800
#define KEY_ALT_2       0x7900
#define KEY_ALT_3       0x7A00
#define KEY_ALT_4       0x7B00
#define KEY_ALT_5       0x7C00
#define KEY_ALT_6       0x7D00
#define KEY_ALT_7       0x7E00
#define KEY_ALT_8       0x7F00
#define KEY_ALT_U       0x1600
#define KEY_ALT_H       0x2300
#define KEY_CTRL_F      0x2106
#define KEY_CTRL_Q      0x1011
#define KEY_F1          0x3B00
#define KEY_F9          0x4300
#define KEY_PGUP        0x4900
#define KEY_PGDN        0x5100
#define KEY_HOME        0x4700
#define KEY_END         0x4F00
#define KEY_DEL         0x5300
#define KEY_SHIFT_ENTER 0x001C  /* Shift+Enter: needs BIOS shift-state check */
```

#### dc_handle_key

```c
void dc_handle_key(dc_state_t *s, int key)
{
    /* Overlays consume all input first */
    if (s->show_help) {
        s->show_help = 0;
        s->dirty = 1;
        return;
    }

    /* Search mode: route to search handler */
    if (s->search.active && s->search.editing) {
        int result = dc_search_handle_key(&s->search, key);
        if (result == 1) {
            /* Execute search */
            dc_search_execute(&s->search, s->selected_channel);
        } else if (result == -1) {
            /* Cancelled */
            dc_search_cancel(&s->search);
        }
        s->dirty = 1;
        return;
    }

    /* Global shortcuts (work in any focus) */
    switch (key) {
    case KEY_CTRL_Q:
    case KEY_ESC:
        /* Quit with confirmation (or cancel overlay) */
        if (s->show_users) { s->show_users = 0; s->dirty = 1; return; }
        s->running = 0;
        return;

    case KEY_TAB:
        s->focus = (s->focus + 1) % 3;
        s->dirty = 1;
        return;

    case KEY_ALT_1: dc_switch_channel(s, 0); return;
    case KEY_ALT_2: dc_switch_channel(s, 1); return;
    case KEY_ALT_3: dc_switch_channel(s, 2); return;
    case KEY_ALT_4: dc_switch_channel(s, 3); return;
    case KEY_ALT_5: dc_switch_channel(s, 4); return;
    case KEY_ALT_6: dc_switch_channel(s, 5); return;
    case KEY_ALT_7: dc_switch_channel(s, 6); return;
    case KEY_ALT_8: dc_switch_channel(s, 7); return;

    case KEY_ALT_U:
        s->show_users = !s->show_users;
        s->dirty = 1;
        return;

    case KEY_F1:
    case KEY_ALT_H:
        s->show_help = 1;
        s->dirty = 1;
        return;

    case KEY_F9:
#if FEAT_AUDIO
        s->config.sound = !s->config.sound;
        s->dirty = 1;
#endif
        return;

    case KEY_CTRL_F:
#if FEAT_SEARCH
        dc_search_start(&s->search);
        s->dirty = 1;
#endif
        return;
    }

    /* Focus-specific input */
    switch (s->focus) {
    case DC_FOCUS_CHANNELS:
        dc_handle_key_channels(s, key);
        break;
    case DC_FOCUS_MESSAGES:
        dc_handle_key_messages(s, key);
        break;
    case DC_FOCUS_COMPOSE:
        dc_handle_key_compose(s, key);
        break;
    }
}
```

#### dc_handle_key_channels

```c
static void dc_handle_key_channels(dc_state_t *s, int key)
{
    switch (key) {
    case KEY_UP:
        if (s->selected_channel > 0)
            dc_switch_channel(s, s->selected_channel - 1);
        break;
    case KEY_DOWN:
        if (s->selected_channel < s->channel_count - 1)
            dc_switch_channel(s, s->selected_channel + 1);
        break;
    case KEY_ENTER:
        s->focus = DC_FOCUS_COMPOSE;
        s->dirty = 1;
        break;
    }
}
```

#### dc_handle_key_messages

```c
static void dc_handle_key_messages(dc_state_t *s, int key)
{
    int total = dc_get_channel_msg_count(s->selected_channel);

    switch (key) {
    case KEY_UP:
        if (s->msg_scroll < total - 1) {
            s->msg_scroll++;
            s->dirty = 1;
        }
        break;
    case KEY_DOWN:
        if (s->msg_scroll > 0) {
            s->msg_scroll--;
            s->dirty = 1;
        }
        break;
    case KEY_PGUP:
        s->msg_scroll += DC_CONTENT_ROWS;
        if (s->msg_scroll > total - 1)
            s->msg_scroll = total - 1;
        s->dirty = 1;
        break;
    case KEY_PGDN:
        s->msg_scroll -= DC_CONTENT_ROWS;
        if (s->msg_scroll < 0)
            s->msg_scroll = 0;
        s->dirty = 1;
        break;
    case 'n':
    case 'N':
#if FEAT_SEARCH
        if (s->search.active) {
            dc_search_next(&s->search, (key == 'N') ? -1 : 1);
            /* Auto-scroll to match */
            if (s->search.total_matches > 0) {
                int mi = s->search.matches[s->search.current_idx].msg_idx;
                s->msg_scroll = total - 1 - mi;
            }
            s->dirty = 1;
        }
#endif
        break;
    }
}
```

#### dc_handle_key_compose

```c
static void dc_handle_key_compose(dc_state_t *s, int key)
{
    dc_compose_t *c = &s->compose;
    int ch = key & 0xFF;  /* ASCII portion */

    switch (key) {
    case KEY_ENTER:
        if (c->len > 0) {
            dc_send_message(s, c->buf);
#if FEAT_AUDIO
            /* No sound on own message */
#endif
        }
        break;

    case KEY_BKSP:
        if (c->cursor > 0) {
            memmove(&c->buf[c->cursor - 1], &c->buf[c->cursor],
                    c->len - c->cursor + 1);
            c->cursor--;
            c->len--;
            s->dirty = 1;
        }
        break;

    case KEY_DEL:
        if (c->cursor < c->len) {
            memmove(&c->buf[c->cursor], &c->buf[c->cursor + 1],
                    c->len - c->cursor);
            c->len--;
            s->dirty = 1;
        }
        break;

    case KEY_LEFT:
        if (c->cursor > 0) { c->cursor--; s->dirty = 1; }
        break;
    case KEY_RIGHT:
        if (c->cursor < c->len) { c->cursor++; s->dirty = 1; }
        break;
    case KEY_HOME:
        c->cursor = 0; s->dirty = 1;
        break;
    case KEY_END:
        c->cursor = c->len; s->dirty = 1;
        break;

    default:
        /* Shift+Enter: insert newline (multi-line compose) */
#if FEAT_MULTILINE
        if (key == KEY_SHIFT_ENTER) {
            if (c->lines < DC_COMPOSE_MAX_LINES && c->len < DC_MAX_COMPOSE - 1) {
                memmove(&c->buf[c->cursor + 1], &c->buf[c->cursor],
                        c->len - c->cursor + 1);
                c->buf[c->cursor] = '\n';
                c->cursor++;
                c->len++;
                c->lines++;
                s->dirty = 1;
            }
            break;
        }
#endif

        /* Printable ASCII */
        if (ch >= 0x20 && ch <= 0x7E && c->len < DC_MAX_COMPOSE - 1) {
            memmove(&c->buf[c->cursor + 1], &c->buf[c->cursor],
                    c->len - c->cursor + 1);
            c->buf[c->cursor] = (char)ch;
            c->cursor++;
            c->len++;
            s->dirty = 1;
        }
        break;
    }
}
```

**Shift+Enter detection**: BIOS INT 16h returns scan code 0x1C for Enter. To distinguish Shift+Enter from Enter, check the BIOS shift-flag byte at 0040:0017h bit 1 (left Shift) or bit 0 (right Shift):

```c
/* Check if Shift is held during Enter keypress */
static int is_shift_pressed(void)
{
    unsigned char flags;
    flags = *(unsigned char far *)0x00400017L;
    return (flags & 0x03) != 0;  /* bits 0-1: right/left shift */
}
```

This check is performed in the main `dc_handle_key_compose` before deciding between Enter (send) and Shift+Enter (newline).

### audio_dc.c -- PC Speaker

Uses PIT (Programmable Interval Timer) channel 2 for tone generation, matching TAKEOVER's pattern.

```c
#include "discord_cfg.h"
#include <dos.h>

#if FEAT_AUDIO

/* Generate a tone on the PC speaker using PIT channel 2 */
static void tone(unsigned int freq_hz, unsigned int duration_ms)
{
    unsigned int divisor;
    unsigned long end_tick;
    unsigned long now;

    if (freq_hz == 0) return;
    divisor = (unsigned int)(1193180UL / freq_hz);

    /* Program PIT channel 2 for square wave */
    outp(0x43, 0xB6);                    /* channel 2, mode 3, lo/hi */
    outp(0x42, divisor & 0xFF);          /* low byte */
    outp(0x42, (divisor >> 8) & 0xFF);   /* high byte */

    /* Enable speaker (bits 0+1 of port 0x61) */
    outp(0x61, inp(0x61) | 0x03);

    /* Wait duration using BIOS tick counter (rough: 1 tick ~= 55ms) */
    /* For sub-55ms durations, use a busy loop */
    {
        unsigned int loops = (unsigned int)(duration_ms * 50U);
        while (loops--) { /* ~20us per empty loop iteration on 8088 */ }
    }

    /* Disable speaker */
    outp(0x61, inp(0x61) & 0xFC);
}

void dc_audio_blip(void)
{
    tone(2000, 20);
}

void dc_audio_chirp(void)
{
    tone(1500, 15);
    tone(2000, 15);
}

void dc_audio_mention(void)
{
    tone(1000, 30);
    tone(1500, 30);
    tone(2000, 30);
}

void dc_audio_error(void)
{
    tone(800, 50);
    tone(400, 50);
}

#else

/* Stubs when audio is disabled */
void dc_audio_blip(void) {}
void dc_audio_chirp(void) {}
void dc_audio_mention(void) {}
void dc_audio_error(void) {}

#endif /* FEAT_AUDIO */
```

### config_dc.c -- Settings Persistence

```c
#include "discord.h"
#include <stdio.h>
#include <string.h>

#define DC_CONFIG_FILE "DISCORD.CFG"

void dc_config_defaults(dc_config_t *cfg)
{
    cfg->sound = 1;
    cfg->notify = 1;
    cfg->last_channel = 0;
    cfg->color_scheme = 0;
    strcpy(cfg->username, "You");
}

void dc_config_load(dc_config_t *cfg)
{
    FILE *f;
    char line[80];
    char key[20], val[60];

    dc_config_defaults(cfg);

    f = fopen(DC_CONFIG_FILE, "r");
    if (!f) return;

    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%19[^=]=%59s", key, val) == 2) {
            if (strcmp(key, "sound") == 0)
                cfg->sound = val[0] - '0';
            else if (strcmp(key, "notify") == 0)
                cfg->notify = val[0] - '0';
            else if (strcmp(key, "last_channel") == 0)
                cfg->last_channel = val[0] - '0';
            else if (strcmp(key, "color_scheme") == 0)
                cfg->color_scheme = val[0] - '0';
            else if (strcmp(key, "username") == 0)
                strncpy(cfg->username, val, DC_MAX_AUTHOR_LEN - 1);
        }
    }

    fclose(f);

    /* Clamp values */
    if (cfg->last_channel < 0 || cfg->last_channel >= DC_MAX_CHANNELS)
        cfg->last_channel = 0;
}

void dc_config_save(dc_config_t *cfg)
{
    FILE *f = fopen(DC_CONFIG_FILE, "w");
    if (!f) return;

    fprintf(f, "sound=%d\n", cfg->sound);
    fprintf(f, "notify=%d\n", cfg->notify);
    fprintf(f, "last_channel=%d\n", cfg->last_channel);
    fprintf(f, "color_scheme=%d\n", cfg->color_scheme);
    fprintf(f, "username=%s\n", cfg->username);

    fclose(f);
}
```

### search_dc.c -- Find in Messages

Adapted from Cathode's `search.c`, operating on far-heap message arrays instead of page buffers.

```c
#include "discord.h"
#include "discord_cfg.h"
#include <string.h>
#include <ctype.h>

#if FEAT_SEARCH

void dc_search_init(dc_search_t *s)
{
    memset(s, 0, sizeof(dc_search_t));
}

void dc_search_start(dc_search_t *s)
{
    s->active = 1;
    s->editing = 1;
    s->query[0] = '\0';
    s->query_len = 0;
    s->cursor = 0;
    s->total_matches = 0;
    s->current_idx = 0;
}

void dc_search_cancel(dc_search_t *s)
{
    s->active = 0;
    s->editing = 0;
    s->total_matches = 0;
}

int dc_search_handle_key(dc_search_t *s, int key)
{
    int ch = key & 0xFF;

    switch (key) {
    case KEY_ENTER:
        s->editing = 0;
        return 1;  /* execute search */
    case KEY_ESC:
        return -1; /* cancel */
    case KEY_BKSP:
        if (s->cursor > 0) {
            memmove(&s->query[s->cursor - 1], &s->query[s->cursor],
                    s->query_len - s->cursor + 1);
            s->cursor--;
            s->query_len--;
        }
        return 0;
    default:
        if (ch >= 0x20 && ch <= 0x7E && s->query_len < DC_SEARCH_MAX - 1) {
            memmove(&s->query[s->cursor + 1], &s->query[s->cursor],
                    s->query_len - s->cursor + 1);
            s->query[s->cursor] = (char)ch;
            s->cursor++;
            s->query_len++;
        }
        return 0;
    }
}

/* Case-insensitive substring search in a far string */
static int far_stristr(const char far *haystack, const char *needle, int needle_len)
{
    int i, j;
    for (i = 0; haystack[i]; i++) {
        for (j = 0; j < needle_len; j++) {
            if (tolower((unsigned char)haystack[i + j]) !=
                tolower((unsigned char)needle[j]))
                break;
        }
        if (j == needle_len) return i;
    }
    return -1;
}

void dc_search_execute(dc_search_t *s, int ch_idx)
{
    int total = dc_get_channel_msg_count(ch_idx);
    int i;
    dc_message_t far *msg;

    s->total_matches = 0;
    s->current_idx = 0;

    if (s->query_len == 0) return;

    for (i = 0; i < total && s->total_matches < DC_SEARCH_MAX_MATCHES; i++) {
        int pos;
        msg = dc_get_channel_msg(ch_idx, i);
        if (!msg) continue;

        /* Search in message text */
        pos = far_stristr((const char far *)msg->text, s->query, s->query_len);
        if (pos >= 0) {
            s->matches[s->total_matches].msg_idx = i;
            s->matches[s->total_matches].char_pos = pos;
            s->total_matches++;
            continue;  /* one match per message */
        }

        /* Search in author name */
        pos = far_stristr((const char far *)msg->author, s->query, s->query_len);
        if (pos >= 0) {
            s->matches[s->total_matches].msg_idx = i;
            s->matches[s->total_matches].char_pos = -1;  /* author match */
            s->total_matches++;
        }
    }
}

void dc_search_next(dc_search_t *s, int direction)
{
    if (s->total_matches == 0) return;
    s->current_idx += direction;
    if (s->current_idx >= s->total_matches)
        s->current_idx = 0;
    if (s->current_idx < 0)
        s->current_idx = s->total_matches - 1;
}

int dc_search_is_hit(dc_search_t *s, int msg_idx, int char_pos, int len)
{
    int i;
    if (!s->active || s->total_matches == 0) return 0;
    for (i = 0; i < s->total_matches; i++) {
        if (s->matches[i].msg_idx == msg_idx &&
            char_pos >= s->matches[i].char_pos &&
            char_pos < s->matches[i].char_pos + len) {
            return 1;
        }
    }
    return 0;
}

#else

void dc_search_init(dc_search_t *s) { memset(s, 0, sizeof(dc_search_t)); }
void dc_search_start(dc_search_t *s) { (void)s; }
void dc_search_cancel(dc_search_t *s) { (void)s; }
int  dc_search_handle_key(dc_search_t *s, int key) { (void)s; (void)key; return -1; }
void dc_search_execute(dc_search_t *s, int ch_idx) { (void)s; (void)ch_idx; }
void dc_search_next(dc_search_t *s, int d) { (void)s; (void)d; }
int  dc_search_is_hit(dc_search_t *s, int mi, int cp, int l) { (void)s; (void)mi; (void)cp; (void)l; return 0; }

#endif /* FEAT_SEARCH */
```

### stub_discord.c -- Fake Data Provider

Expands from v1 (6 channels, ~30 messages) to v2 (8 channels, ~160 messages across channels).

#### Server and Channel Setup

```c
void stub_load_server(dc_state_t *s)
{
    int i;
    unsigned alloc_size;

    strncpy(s->server_name, "Retro Computing Hub", DC_MAX_SERVER_NAME - 1);
    s->channel_count = 8;

    /* Allocate per-channel far message pools */
    alloc_size = DC_MAX_MESSAGES * sizeof(dc_message_t);
    for (i = 0; i < DC_MAX_CHANNELS; i++) {
        chan_msg_count[i] = 0;
        chan_msgs[i] = (dc_message_t far *)_fmalloc(alloc_size);
        if (chan_msgs[i])
            _fmemset((void far *)chan_msgs[i], 0, alloc_size);
    }

    strcpy(s->channels[0].name, "general");
    strcpy(s->channels[1].name, "hardware");
    strcpy(s->channels[2].name, "builds");
    strcpy(s->channels[3].name, "software");
    strcpy(s->channels[4].name, "marketplace");
    strcpy(s->channels[5].name, "off-topic");
    strcpy(s->channels[6].name, "help");
    strcpy(s->channels[7].name, "showcase");

    /* Populate stub messages... */
    stub_populate_general();
    stub_populate_hardware();
    stub_populate_builds();
    stub_populate_software();
    stub_populate_marketplace();
    stub_populate_offtopic();
    stub_populate_help();
    stub_populate_showcase();

    /* Set initial unread counts */
    s->channels[0].unread = 3;
    s->channels[1].unread = 0;
    s->channels[2].unread = 5;
    s->channels[3].unread = 0;
    s->channels[4].unread = 2;
    s->channels[5].unread = 0;
    s->channels[6].unread = 1;
    s->channels[7].unread = 0;

    /* Sync msg_count in channel metadata */
    for (i = 0; i < 8; i++)
        s->channels[i].msg_count = chan_msg_count[i];
}
```

#### Fake Users

```c
static const char *stub_users[8] = {
    "VintageNerd", "RetroGamer", "ChipCollector", "DOSenthusiast",
    "BarelyBooting", "PCBWizard", "AdLibFan", "SocketSlinger"
};
```

Each user's color is computed via `dc_author_color()` (hash-based), ensuring consistency.

#### Stub Message Content (~20 per channel)

**#general** (20 messages): General retro computing chatter. Mix of XT-IDE, NetISA, CF cards, DOS versions. Includes 2 messages with reactions (VintageNerd's XT-IDE success gets hearts and smileys, BarelyBooting's NetISA announcement gets arrows and notes).

**#hardware** (20 messages): ISA cards, capacitor replacement, CRT monitors, VLB cards, 486 motherboards, SIMM types, power supply repair, keyboard switches.
- 3 messages have `thread_id = 1` (capacitor replacement thread, showing "3 replies")

**#builds** (20 messages): Project showcases -- 386 restore, custom ISA cards, serial terminals, Verilog projects, PCB assembly tips.

**#software** (20 messages): DOS games, FreeDOS, Turbo Pascal, DJGPP, DOSBox-X, packet drivers, memory managers, EMM386 vs JEMMEX.

**#marketplace** (15 messages): Buy/sell/trade -- Sound Blaster cards, ISA network cards, SIMMs, keyboards, monitors. Formatted as "WTS:", "WTB:", "FS:".

**#off-topic** (15 messages): Commander X16, Amiga vs DOS, CRT vs LCD, retrobrighting, thrift store finds.

**#help** (15 messages): Troubleshooting -- boot failures, driver issues, IRQ conflicts, memory configuration, DOSBox-X setup.

**#showcase** (15 messages): Finished builds with descriptions -- "Just finished my 486 sleeper build", screenshots, benchmarks.

#### Timed Message Injection

Same as v1 but with expanded message pool and multi-channel injection:

```c
void stub_inject_timed_message(dc_state_t *s)
{
    unsigned long now;
    char ts[6];

    /* Read BIOS tick counter */
    _asm { xor ax, ax; int 1Ah; mov word ptr now, dx; mov word ptr now+2, cx }

    if (s->last_poll_tick == 0) { s->last_poll_tick = now; return; }
    if ((now - s->last_poll_tick) < 91) return;  /* ~5 seconds */
    s->last_poll_tick = now;

    /* Add to #general's far storage */
    stub_add_timed_msg(0, ts);

    /* Update UI state */
    if (s->selected_channel == 0) {
        s->dirty = 1;
#if FEAT_AUDIO
        if (s->config.sound) dc_audio_blip();
#endif
        s->flash_ticks = 2;
    } else {
        s->channels[0].unread++;
        s->dirty = 1;
#if FEAT_AUDIO
        if (s->config.sound) dc_audio_chirp();
#endif
    }
}
```

### main.c -- Entry Point

```c
#include "discord.h"
#include "discord_cfg.h"
#include "screen.h"

int main(void)
{
    dc_state_t state;

    scr_init();
    scr_cursor_hide();
    scr_clear(ATTR_NORMAL);

    /* Load config before init (provides last_channel, username, etc.) */
    dc_config_load(&state.config);

    /* Initialize state machine and stub data */
    dc_init(&state);

    /* Startup visual effect */
    scr_fade_in(8, 40);

    /* Initial render */
    dc_render_all(&state);

    /* Main loop */
    while (state.running) {
        if (scr_kbhit()) {
            int key = scr_getkey();
            dc_handle_key(&state, key);
        }

        dc_poll_messages(&state);

        if (state.dirty) {
            dc_render_all(&state);
            state.dirty = 0;
        }
    }

    /* Save config on exit */
    state.config.last_channel = state.selected_channel;
    dc_config_save(&state.config);

    scr_fade_out(8, 40);
    scr_cursor_show();
    scr_shutdown();
    return 0;
}
```

---

## Complete Keybind Table

| Key | Context | Action |
|-----|---------|--------|
| **Tab** | Any | Cycle focus: Channels -> Messages -> Compose |
| **Alt+1** through **Alt+8** | Any | Quick switch to channel 1-8 |
| **Alt+U** | Any | Toggle user list overlay |
| **Alt+H** / **F1** | Any | Show help overlay |
| **F9** | Any | Toggle PC speaker sound on/off |
| **Ctrl+F** | Any | Start find-in-messages (search bar) |
| **Ctrl+Q** | Any | Quit (closes overlays first) |
| **Escape** | Any | Quit / close overlay / cancel search |
| **Up/Down** | Channels | Select previous/next channel |
| **Enter** | Channels | Switch focus to Compose |
| **Up/Down** | Messages | Scroll 1 message |
| **PgUp/PgDn** | Messages | Scroll 20 messages (one page) |
| **N** | Messages (search active) | Next search match |
| **Shift+N** | Messages (search active) | Previous search match |
| **Printable chars** | Compose | Insert character at cursor |
| **Enter** | Compose | Send message |
| **Shift+Enter** | Compose | Insert newline (max 3 lines) |
| **Backspace** | Compose | Delete character before cursor |
| **Delete** | Compose | Delete character at cursor |
| **Left/Right** | Compose | Move cursor |
| **Home/End** | Compose | Move cursor to start/end |
| **Any key** | Help overlay | Dismiss help |

---

## Notification Behavior

| Event | Audio | Visual |
|-------|-------|--------|
| New message in current channel | 2000 Hz blip, 20ms | Title bar flash (2 ticks) |
| New message in other channel | Two-tone chirp (1500+2000 Hz) | Unread count increments, channel turns bright white |
| Mention (@username) | Three ascending tones (1000/1500/2000 Hz) | Message text highlighted with `DC_ATTR_MENTION` |
| Error (send failure, etc.) | Descending tones (800/400 Hz) | Status bar shows error message |

---

## Settings File Format (DISCORD.CFG)

```ini
sound=1
notify=1
last_channel=0
username=You
color_scheme=0
```

- Plain text, one `key=value` per line
- No sections, no quoting, no comments
- Missing keys get defaults
- Saved on quit and on F9 toggle
- Located in current working directory (same as EXE)

---

## Reserved Module: JSON Parser (json.h / json.c)

Reserved for future Discord API integration. Not implemented in v2.

When the NetISA firmware's HTTP layer is ready, this module will provide:
- Minimal streaming JSON tokenizer (no DOM, no malloc)
- Token types: `{`, `}`, `[`, `]`, string, number, bool, null
- Callback-driven: `json_parse(const char *buf, int len, json_callback_fn cb)`
- Target: under 1KB code, zero heap allocation

The stub layer (`stub_discord.c`) will be replaced by a real data provider that uses `json.c` to parse Discord gateway WebSocket frames.

---

## Test Harness (dc_runtests.c)

Standalone executable that links all Discord modules except `main.c`. Uses assert-style macros. No VGA rendering (tests run headless in DOSBox-X via relay).

### Test Cases

```c
/* Test 1: Initialize state, verify channel count and defaults */
static void test_init(void)
{
    dc_state_t s;
    dc_config_defaults(&s.config);
    dc_init(&s);
    ASSERT(s.channel_count == 8);
    ASSERT(s.selected_channel == 0);
    ASSERT(s.running == 1);
    ASSERT(s.focus == DC_FOCUS_COMPOSE);
    ASSERT(strcmp(s.channels[0].name, "general") == 0);
    ASSERT(strcmp(s.channels[7].name, "showcase") == 0);
}

/* Test 2: Switch channels, verify message loading from far heap */
static void test_channel_switch(void)
{
    dc_state_t s;
    int count0, count1;
    dc_config_defaults(&s.config);
    dc_init(&s);
    count0 = dc_get_channel_msg_count(0);
    ASSERT(count0 > 0);
    dc_switch_channel(&s, 1);
    ASSERT(s.selected_channel == 1);
    count1 = dc_get_channel_msg_count(1);
    ASSERT(count1 > 0);
    ASSERT(s.channels[1].unread == 0);  /* cleared on switch */
}

/* Test 3: Send message, verify it appears in message array */
static void test_send_message(void)
{
    dc_state_t s;
    int before, after;
    dc_message_t far *msg;
    dc_config_defaults(&s.config);
    dc_init(&s);
    before = dc_get_channel_msg_count(0);
    dc_send_message(&s, "Test message");
    after = dc_get_channel_msg_count(0);
    ASSERT(after == before + 1);
    msg = dc_get_channel_msg(0, after - 1);
    ASSERT(msg != NULL);
    /* Verify text via far copy */
    {
        char buf[32];
        _fmemcpy(buf, (void far *)msg->text, 12);
        buf[12] = '\0';
        ASSERT(strcmp(buf, "Test message") == 0);
    }
}

/* Test 4: Send 128+ messages, verify oldest dropped (circular buffer) */
static void test_circular_buffer(void)
{
    dc_state_t s;
    int i;
    dc_message_t far *msg;
    dc_config_defaults(&s.config);
    dc_init(&s);
    /* Fill channel 0 to capacity + extra */
    for (i = 0; i < 140; i++) {
        char txt[20];
        sprintf(txt, "msg_%d", i);
        dc_send_message(&s, txt);
    }
    ASSERT(dc_get_channel_msg_count(0) == DC_MAX_MESSAGES);
    /* Oldest surviving should be from the tail end */
    msg = dc_get_channel_msg(0, DC_MAX_MESSAGES - 1);
    ASSERT(msg != NULL);
}

/* Test 5: Search for text, verify match positions */
static void test_search(void)
{
    dc_state_t s;
    dc_config_defaults(&s.config);
    dc_init(&s);
    dc_search_start(&s.search);
    strcpy(s.search.query, "NetISA");
    s.search.query_len = 6;
    dc_search_execute(&s.search, 0);
    ASSERT(s.search.total_matches > 0);
    ASSERT(s.search.matches[0].msg_idx >= 0);
}

/* Test 6: Config save/load round-trip */
static void test_config(void)
{
    dc_config_t cfg1, cfg2;
    dc_config_defaults(&cfg1);
    cfg1.sound = 0;
    cfg1.last_channel = 3;
    strcpy(cfg1.username, "Tester");
    dc_config_save(&cfg1);
    dc_config_defaults(&cfg2);
    dc_config_load(&cfg2);
    ASSERT(cfg2.sound == 0);
    ASSERT(cfg2.last_channel == 3);
    ASSERT(strcmp(cfg2.username, "Tester") == 0);
}

/* Test 7: Multi-line compose */
static void test_multiline_compose(void)
{
    dc_state_t s;
    dc_config_defaults(&s.config);
    dc_init(&s);
    s.focus = DC_FOCUS_COMPOSE;
    /* Type "Hello" */
    dc_handle_key(&s, 'H');
    dc_handle_key(&s, 'e');
    dc_handle_key(&s, 'l');
    dc_handle_key(&s, 'l');
    dc_handle_key(&s, 'o');
    ASSERT(s.compose.len == 5);
    ASSERT(s.compose.lines == 1);
    /* Shift+Enter should add newline */
    dc_handle_key(&s, KEY_SHIFT_ENTER);
    ASSERT(s.compose.lines == 2);
    ASSERT(s.compose.buf[5] == '\n');
    /* Enter should send */
    dc_handle_key(&s, 'W');
    dc_handle_key(&s, KEY_ENTER);
    ASSERT(s.compose.len == 0);
    ASSERT(s.compose.lines == 1);
}
```

### Test Runner

```c
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL: %s line %d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } else { passes++; } \
} while(0)

int main(void)
{
    int passes = 0, failures = 0;

    printf("Discord v2 Test Harness\n");
    printf("=======================\n\n");

    test_init();
    test_channel_switch();
    test_send_message();
    test_circular_buffer();
    test_search();
    test_config();
    test_multiline_compose();

    printf("\n%d passed, %d failed\n", passes, failures);
    return failures > 0 ? 1 : 0;
}
```

### Running Tests

```
python devenv\dosrun.py --timeout 30 --cwd \dos "DC_TEST.EXE"
```

Exit code 0 = all pass, 1 = failures. Test output captured by dosrun relay.

---

## Performance Budgets

| Operation | Budget (8088) | Budget (486) | Technique |
|-----------|---------------|--------------|-----------|
| Full screen redraw | < 50ms | < 5ms | Direct VGA write, no buffering |
| Channel switch | < 100ms | < 10ms | No copy -- just change selected_channel |
| Message receive + render | < 30ms | < 3ms | Single _fmemcpy + VGA write |
| Search (128 messages) | < 200ms | < 20ms | Linear scan with far_stristr |
| Config load/save | < 50ms | < 5ms | fopen/fscanf, simple parser |
| Compose keystroke | < 5ms | < 1ms | memmove + single row redraw |

### Why These Are Achievable

- **Full redraw < 50ms on 8088**: 80x25 = 2,000 cells x 2 bytes = 4,000 bytes to VGA buffer. 8088 at 4.77 MHz can write ~100KB/s to I/O. 4KB = ~40ms. With selective redraw (only dirty regions), easily under 50ms.
- **Channel switch < 100ms**: v2 eliminates the near-heap copy. Channel switch changes one integer (`selected_channel`) and sets `dirty=1`. The next render cycle reads from far heap. The render itself is the only cost.
- **Search < 200ms**: 128 messages x 200 chars = 25,600 chars to scan. `far_stristr` on 8088 at ~1 char/5 cycles = ~128K chars/sec. 25,600 chars = ~200ms. Tight but feasible. On 286+ this is trivial.

---

## Acceptance Criteria

| # | Criterion | Verification |
|---|-----------|-------------|
| 1 | Builds clean at `-w4`, under 40KB EXE | `wmake` output shows 0 warnings, `dir DISCORD.EXE` shows < 40,960 bytes |
| 2 | Startup shows fade-in, title bar, channels, stub messages | Visual inspection in DOSBox-X |
| 3 | Tab cycles focus; Alt+1-8 switches channels; compose works | Keyboard testing in DOSBox-X |
| 4 | Multi-line compose (Shift+Enter) up to 3 lines | Type text, Shift+Enter, verify newline in compose area |
| 5 | Scroll bar tracks position in message history | PgUp/PgDn, verify thumb position changes |
| 6 | Ctrl+F find highlights matches, N/Shift+N cycles | Search for "NetISA", verify yellow highlights, N cycles |
| 7 | Unread counts update on channel switch and new messages | Switch channels, wait for timed message, verify counts |
| 8 | PC speaker sounds on new messages (F9 toggles) | Wait for timed message, hear blip. F9, verify silence |
| 9 | Alt+U shows/hides user list overlay | Press Alt+U, verify popup with 8 users. Press again to dismiss |
| 10 | F1 shows help overlay with all keybinds | Press F1, verify centered popup. Any key dismisses |
| 11 | Settings persist to DISCORD.CFG | Toggle F9, quit, restart, verify sound state preserved |
| 12 | Reactions display as CP437 characters | Verify heart, smiley, etc. on stub messages with reactions |
| 13 | Thread indicators show reply count | Verify "[-> 3 replies]" suffix on threaded messages in #hardware |
| 14 | Test harness passes all 7 tests via DOSBox-X relay | `python devenv\dosrun.py --timeout 30 --cwd \dos "DC_TEST.EXE"` exits 0 |
| 15 | Runs correctly in DOSBox-X with VGA and EGA machine types | Test with `machine=svga_s3` and `machine=ega` |

---

## File Manifest

```
discord/
  discord_cfg.h     Feature flags (#define FEAT_*)
  discord.h         Types, constants, prototypes (all structs, all functions)
  discord.c         State machine (init, switch, send, poll, timestamp helper)
  render_dc.c       Renderer (titlebar, channels, messages, compose, status,
                     scrollbar, search bar, user overlay, help overlay,
                     word wrap, reactions, thread indicators)
  input_dc.c        Keyboard handler (global shortcuts, per-focus handlers,
                     compose editing with multi-line, shift detection)
  audio_dc.c        PC speaker (PIT channel 2: blip, chirp, mention, error)
  config_dc.c       DISCORD.CFG load/save (INI-style, fopen/fprintf/fscanf)
  stub_discord.c    Fake data provider (8 channels, ~160 messages, 8 users,
                     reactions, threads, timed injection)
  search_dc.c       Find-in-messages (Ctrl+F, far_stristr, match tracking)
  main.c            Entry point, main loop, fade in/out, config lifecycle
  dc_runtests.c     Test harness (7 tests, standalone EXE, headless)
```

**Shared code (from lib/ and cathode/):**
- `screen.c` / `screen.h` -- VGA text buffer rendering (scr_* functions)
- `utf8.c` / `utf8.h` -- UTF-8 to CP437 mapping (for future API text)

**Reserved (not implemented in v2):**
- `json.h` / `json.c` -- Streaming JSON tokenizer for Discord API

---

## Changes from v1

| Area | v1 | v2 |
|------|----|----|
| Max messages per channel | 32 | 128 |
| Channels | 6 | 8 |
| Near-heap message array | `messages[32]` in `dc_state_t` (7.4KB) | Eliminated. Single 240-byte scratch buffer |
| Channel switch | Save near->far, load far->near (bulk copy) | Change `selected_channel` only (no copy) |
| Rendering source | Near-heap array | Far pointer -> scratch (one msg at a time) |
| Compose | Single line, 77 chars | Multi-line (3 lines, 237 chars), Shift+Enter |
| Search | None | Ctrl+F with match highlighting |
| Reactions | None | Bitfield + CP437 display |
| Threads | None | Data model + indicator (UI deferred) |
| Audio | None | PC speaker (4 notification types) |
| User list | None | Alt+U popup overlay |
| Help | None | F1 popup overlay |
| Settings | None | DISCORD.CFG persistence |
| Test harness | None | 7 automated tests |
| Author colors | 6, sequential | 8, hash-based (consistent per author) |
| Stub data | ~30 messages | ~160 messages, themed per channel |

---

## Phase 2 Notes (FEAT_THREADS)

Thread view is designed into the data model (`thread_id` field in `dc_message_t`) but the interactive thread UI is deferred behind `FEAT_THREADS=0`.

When enabled, thread view will:
1. Detect Enter on a message with `thread_id > 0` in Messages focus
2. Replace the message pane with thread messages (filtered by `thread_id`)
3. Render a "Thread: [topic]" title in the message pane header
4. Escape returns to the main channel view
5. Thread messages are stored in the same per-channel far array (filtered by `thread_id` match)

This requires no new data structures -- just a `viewing_thread_id` field in `dc_state_t` and a filter pass in `dc_render_messages()`.
