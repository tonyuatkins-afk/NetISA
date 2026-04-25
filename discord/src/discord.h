/*
 * discord.h - Discord v2 DOS client types and prototypes
 *
 * Central header for the entire Discord client. All modules include
 * this file. Memory design: channels store metadata only; messages
 * live in a shared near-heap pool for the active channel (reloaded
 * on switch). Per-channel persistence uses far-heap arrays.
 *
 * Target: 8088 real mode, OpenWatcom C, small memory model.
 * int = 2 bytes, near pointers = 2 bytes, far pointers = 4 bytes.
 */

#ifndef DISCORD_H
#define DISCORD_H

#include "screen.h"
#include "discord_cfg.h"
#include <malloc.h>     /* _fmalloc, _ffree, _fmemcpy, _fmemset */

/* ================================================================
 * Constants
 * ================================================================ */

/* Data limits */
#define DC_MAX_CHANNELS     8
#define DC_MAX_MESSAGES     128
#define DC_MAX_MSG_LEN      200
#define DC_MAX_AUTHOR_LEN   16
#define DC_MAX_CHAN_NAME     20
#define DC_MAX_SERVER_NAME  24

/* Multi-line compose: 3 lines x 79 visible cols = 237 chars max */
#define DC_MAX_COMPOSE      237
#define DC_COMPOSE_COLS     77
#define DC_COMPOSE_MAX_LINES 3

/* Search limits */
#define DC_SEARCH_MAX           40
#define DC_SEARCH_MAX_MATCHES   32

/* Layout geometry (80x25 text mode) */
#define DC_CHAN_WIDTH        18
#define DC_MSG_LEFT          19
#define DC_MSG_WIDTH         60

#define DC_TITLE_ROW         0
#define DC_SEP1_ROW          1
#define DC_CONTENT_TOP       2
#define DC_CONTENT_BOT       21
#define DC_CONTENT_ROWS      20
#define DC_SEP2_ROW          22
#define DC_COMPOSE_ROW       23
#define DC_STATUS_ROW        24

/* Focus states */
#define DC_FOCUS_CHANNELS    0
#define DC_FOCUS_MESSAGES    1
#define DC_FOCUS_COMPOSE     2

/* Author color palette */
#define DC_NUM_COLORS        6

/* ================================================================
 * Key codes
 * ================================================================ */

/* ASCII keys (guard against redefinition from screen.h) */
#ifndef KEY_ENTER
#define KEY_ENTER           0x0D
#endif
#ifndef KEY_ESC
#define KEY_ESC             0x1B
#endif
#ifndef KEY_TAB
#define KEY_TAB             0x09
#endif
#ifndef KEY_BACKSPACE
#define KEY_BACKSPACE       0x08
#endif

/* Extended keys (scan code in high byte, ASCII=0 in low byte) */
#ifndef KEY_UP
#define KEY_UP              0x4800
#endif
#ifndef KEY_DOWN
#define KEY_DOWN            0x5000
#endif
#ifndef KEY_LEFT
#define KEY_LEFT            0x4B00
#endif
#ifndef KEY_RIGHT
#define KEY_RIGHT           0x4D00
#endif
#ifndef KEY_PGUP
#define KEY_PGUP            0x4900
#endif
#ifndef KEY_PGDN
#define KEY_PGDN            0x5100
#endif
#ifndef KEY_HOME
#define KEY_HOME            0x4700
#endif
#ifndef KEY_END
#define KEY_END             0x4F00
#endif
#ifndef KEY_DEL
#define KEY_DEL             0x5300
#endif

/* Function keys */
#ifndef KEY_F1
#define KEY_F1              0x3B00
#endif
#ifndef KEY_F9
#define KEY_F9              0x4300
#endif

/* Shift+Enter: scan 0x1C with ASCII 0x0A (line feed) */
#define KEY_SHIFT_ENTER     0x1C0A

/* Ctrl keys (ASCII control codes) */
#define KEY_CTRL_F          0x06
#define KEY_CTRL_Q          0x11

/* Alt+number: channel quick-switch (Alt+1=0x7800 .. Alt+8=0x7F00) */
#define KEY_ALT_1           0x7800
#define KEY_ALT_2           0x7900
#define KEY_ALT_3           0x7A00
#define KEY_ALT_4           0x7B00
#define KEY_ALT_5           0x7C00
#define KEY_ALT_6           0x7D00
#define KEY_ALT_7           0x7E00
#define KEY_ALT_8           0x7F00

/* Alt+letter shortcuts */
#define KEY_ALT_U           0x1600
#define KEY_ALT_H           0x2300

/* ================================================================
 * Attribute constants (Discord design language)
 * ================================================================ */

/* General */
#define ATTR_NORMAL         SCR_ATTR(SCR_LIGHTGRAY, SCR_BLACK)
#define ATTR_HIGHLIGHT      SCR_ATTR(SCR_WHITE, SCR_BLACK)
#define ATTR_DIM            SCR_ATTR(SCR_DARKGRAY, SCR_BLACK)

/* Title bar */
#define ATTR_TITLE          SCR_ATTR(SCR_WHITE, SCR_BLUE)
#define ATTR_TITLE_SERVER   SCR_ATTR(SCR_YELLOW, SCR_BLUE)
#define ATTR_TITLE_STATUS   SCR_ATTR(SCR_LIGHTGREEN, SCR_BLUE)

/* Channel list */
#define ATTR_CHAN_NORMAL     SCR_ATTR(SCR_DARKGRAY, SCR_BLACK)
#define ATTR_CHAN_UNREAD     SCR_ATTR(SCR_WHITE, SCR_BLACK)
#define ATTR_CHAN_SELECTED   SCR_ATTR(SCR_BLACK, SCR_CYAN)

/* Message area */
#define ATTR_MSG_TIME       SCR_ATTR(SCR_DARKGRAY, SCR_BLACK)
#define ATTR_MSG_SELF       SCR_ATTR(SCR_LIGHTGREEN, SCR_BLACK)
#define ATTR_MSG_TEXT       SCR_ATTR(SCR_LIGHTGRAY, SCR_BLACK)

/* Compose bar */
#define ATTR_COMPOSE        SCR_ATTR(SCR_WHITE, SCR_BLACK)
#define ATTR_COMPOSE_PROMPT SCR_ATTR(SCR_DARKGRAY, SCR_BLACK)

/* Status bar */
#undef  ATTR_STATUS
#define ATTR_STATUS         SCR_ATTR(SCR_LIGHTGRAY, SCR_BLUE)

/* Separators and scrollbar */
#define ATTR_SEP            SCR_ATTR(SCR_DARKGRAY, SCR_BLACK)
#define ATTR_SCROLLBAR      SCR_ATTR(SCR_DARKGRAY, SCR_BLACK)
#define ATTR_SCROLLTHUMB    SCR_ATTR(SCR_WHITE, SCR_BLACK)

/* Search highlights */
#define ATTR_SEARCH_HIT     SCR_ATTR(SCR_BLACK, SCR_YELLOW)
#define ATTR_SEARCH_CUR     SCR_ATTR(SCR_BLACK, SCR_WHITE)

/* Reactions */
#define ATTR_REACTION       SCR_ATTR(SCR_YELLOW, SCR_BLACK)

/* Threads */
#define ATTR_THREAD         SCR_ATTR(SCR_DARKGRAY, SCR_BLACK)

/* Overlay panels (user list, help) */
#define ATTR_OVERLAY_BG     SCR_ATTR(SCR_LIGHTGRAY, SCR_BLUE)
#define ATTR_OVERLAY_TITLE  SCR_ATTR(SCR_WHITE, SCR_BLUE)
#define ATTR_OVERLAY_KEY    SCR_ATTR(SCR_YELLOW, SCR_BLUE)

/* Notification flash (inverted title) */
#define ATTR_FLASH          SCR_ATTR(SCR_BLUE, SCR_WHITE)

/* ================================================================
 * Reaction CP437 glyph mapping
 * ================================================================ */

#if FEAT_REACTIONS
/* heart, smiley, diamond, club, spade, star, right-tri, music-note */
static const unsigned char reaction_glyphs[8] = {
    0x03, 0x01, 0x04, 0x05, 0x06, 0x0F, 0x10, 0x0E
};
#endif

/* ================================================================
 * Structures
 * ================================================================ */

/* Message: 240 bytes (padded with _reserved for alignment) */
typedef struct {
    char            author[DC_MAX_AUTHOR_LEN];  /* 16 */
    char            text[DC_MAX_MSG_LEN];        /* 200 */
    char            timestamp[6];                /* "HH:MM\0" */
    unsigned char   author_color;                /* 1 */
    int             is_self;                     /* 2 */
    unsigned char   reaction_idx;                /* 1: index into reaction_glyphs, 0xFF=none */
    unsigned char   reaction_count;              /* 1 */
    int             thread_count;                /* 2: replies in thread, 0=no thread */
    unsigned char   _reserved[12];               /* pad to 240 */
} dc_message_t;

/* Channel metadata (no message storage here) */
typedef struct {
    char    name[DC_MAX_CHAN_NAME];
    int     unread;
    int     mention;    /* @mention flag for highlight */
} dc_channel_t;

/* Multi-line compose buffer */
typedef struct {
    char    buf[DC_MAX_COMPOSE + 1];    /* 238 bytes */
    int     len;
    int     cursor;
    int     scroll_line;    /* first visible line in compose area */
} dc_compose_t;

/* Search: single match position */
typedef struct {
    int     msg_idx;    /* index into messages[] */
    int     offset;     /* byte offset within message text */
} dc_search_pos_t;

/* Search state */
typedef struct {
    char            query[DC_SEARCH_MAX + 1];
    int             query_len;
    int             active;         /* search overlay visible */
    int             match_count;
    int             current_match;  /* index into matches[] */
    dc_search_pos_t matches[DC_SEARCH_MAX_MATCHES];
} dc_search_t;

/* Runtime configuration (loaded from DISCORD.CFG) */
typedef struct {
    int     sound;           /* 0=off, 1=on (F9 toggle) */
    int     notify;          /* 0=off, 1=on */
    int     last_channel;    /* 0-7: last selected channel */
    int     color_scheme;    /* 0=default (reserved) */
    char    username[DC_MAX_AUTHOR_LEN];
} dc_config_t;

/* Full client state */
typedef struct {
    char            server_name[DC_MAX_SERVER_NAME];
    dc_channel_t    channels[DC_MAX_CHANNELS];
    int             channel_count;
    int             selected_channel;

    /* Active channel message pool (near heap) */
    dc_message_t    messages[DC_MAX_MESSAGES];
    int             msg_count;
    int             msg_scroll;

    /* Scratch message for building before insert */
    dc_message_t    scratch_msg;

    int             focus;
    dc_compose_t    compose;
    int             running;
    int             dirty;              /* screen needs full redraw */

    unsigned long   last_poll_tick;

    /* Overlay flags */
    int             show_userlist;      /* Alt+U overlay */
    int             show_help;          /* F1/Alt+H help overlay */

    /* Notification flash */
    int             flash_ticks;        /* countdown for title flash */

    /* Search state */
#if FEAT_SEARCH
    dc_search_t     search;
#endif

    /* Configuration */
    dc_config_t     config;
} dc_state_t;

/* ================================================================
 * Author color function
 * ================================================================ */

/* Returns an attribute byte for the given author name (hash-based). */
unsigned char dc_author_color(const char *name);

/* ================================================================
 * Function prototypes by module
 * ================================================================ */

/* --- discord.c: state machine --- */
void dc_init(dc_state_t *s);
void dc_switch_channel(dc_state_t *s, int channel_idx);
void dc_send_message(dc_state_t *s, const char *text);
void dc_poll_messages(dc_state_t *s);

/* --- render_dc.c: screen rendering --- */
void dc_render_all(dc_state_t *s);
void dc_render_titlebar(dc_state_t *s);
void dc_render_channels(dc_state_t *s);
void dc_render_messages(dc_state_t *s);
void dc_render_compose(dc_state_t *s);
void dc_render_statusbar(dc_state_t *s);
void dc_render_separator(int row);
void dc_render_scrollbar(dc_state_t *s);
void dc_render_userlist(dc_state_t *s);
void dc_render_help(dc_state_t *s);
#if FEAT_SEARCH
void dc_render_search_bar(dc_state_t *s);
#endif

/* --- input_dc.c: keyboard handler --- */
void dc_handle_key(dc_state_t *s, int key);

/* --- audio_dc.c: PC speaker notifications --- */
#if FEAT_AUDIO
void dc_audio_init(void);
void dc_audio_beep_mention(void);
void dc_audio_beep_message(void);
void dc_audio_shutdown(void);
#endif

/* --- config_dc.c: configuration file handling --- */
void dc_config_load(dc_config_t *cfg);
void dc_config_save(dc_config_t *cfg);
void dc_config_defaults(dc_config_t *cfg);

/* --- search_dc.c: message search --- */
#if FEAT_SEARCH
void dc_search_open(dc_state_t *s);
void dc_search_close(dc_state_t *s);
void dc_search_update(dc_state_t *s);
void dc_search_next(dc_state_t *s);
void dc_search_prev(dc_state_t *s);
void dc_search_handle_key(dc_state_t *s, int key);
#endif

/* --- stub_discord.c: fake data for offline testing --- */
void stub_load_server(dc_state_t *s);
void stub_load_channel_msgs(dc_state_t *s, int ch_idx);
void stub_save_channel_msgs(dc_state_t *s);
void stub_inject_timed_message(dc_state_t *s);

#endif /* DISCORD_H */
