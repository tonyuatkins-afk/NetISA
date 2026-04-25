/*
 * discord.c - Discord v2 client state machine
 *
 * Core logic for the Discord DOS client. Manages channel switching,
 * message sending, polling, and author color hashing. Message arrays
 * live on the far heap (allocated in stub_discord.c) to stay within
 * the 64KB DGROUP limit of small model.
 *
 * Target: 8088, OpenWatcom C, small memory model.
 */

#include "discord.h"
#include <string.h>
#include <dos.h>
#include <malloc.h>

/* Far-heap message storage, owned by stub_discord.c */
extern dc_message_t far *chan_msgs[DC_MAX_CHANNELS];
extern int chan_msg_count[DC_MAX_CHANNELS];

/* Stub entry points */
extern void stub_load_server(dc_state_t *s);
extern void stub_inject_timed_message(dc_state_t *s);

/* Author color palette: 8 distinct foreground colors on black */
static const unsigned char color_table[8] = {
    SCR_ATTR(SCR_LIGHTCYAN,    SCR_BLACK),
    SCR_ATTR(SCR_LIGHTMAGENTA, SCR_BLACK),
    SCR_ATTR(SCR_YELLOW,       SCR_BLACK),
    SCR_ATTR(SCR_LIGHTBLUE,    SCR_BLACK),
    SCR_ATTR(SCR_LIGHTRED,     SCR_BLACK),
    SCR_ATTR(SCR_LIGHTGREEN,   SCR_BLACK),
    SCR_ATTR(SCR_CYAN,         SCR_BLACK),
    SCR_ATTR(SCR_BROWN,        SCR_BLACK)
};

/*
 * dc_author_color - Hash an author name to one of 8 colors.
 *
 * Simple additive hash: sum all character values, mod 8,
 * then index into the color table.
 */
unsigned char dc_author_color(const char *name)
{
    unsigned int hash = 0;
    while (*name) {
        hash += (unsigned char)*name;
        name++;
    }
    return color_table[hash % 8];
}

/*
 * dc_get_channel_msg_count - Return message count for a channel.
 */
int dc_get_channel_msg_count(int ch_idx)
{
    if (ch_idx < 0 || ch_idx >= DC_MAX_CHANNELS)
        return 0;
    return chan_msg_count[ch_idx];
}

/*
 * dc_get_channel_msg - Return far pointer to a specific message.
 *
 * Returns NULL if indices are out of bounds or the channel
 * has no allocated storage.
 */
dc_message_t far *dc_get_channel_msg(int ch_idx, int msg_idx)
{
    if (ch_idx < 0 || ch_idx >= DC_MAX_CHANNELS)
        return (dc_message_t far *)0;
    if (msg_idx < 0 || msg_idx >= chan_msg_count[ch_idx])
        return (dc_message_t far *)0;
    if (!chan_msgs[ch_idx])
        return (dc_message_t far *)0;
    return &chan_msgs[ch_idx][msg_idx];
}

/*
 * dc_make_timestamp - Build "HH:MM" string from BIOS tick counter.
 *
 * Reads INT 1Ah tick count (18.2 ticks/sec). Converts to hours
 * and minutes using integer division.
 */
static void dc_make_timestamp(char *buf)
{
    unsigned long ticks;
    unsigned int hrs, mins;

    _asm {
        xor ax, ax
        int 1Ah
        mov word ptr ticks, dx
        mov word ptr ticks+2, cx
    }

    /* 1092 ticks per minute (18.2 * 60), 65520 per hour */
    hrs = (unsigned int)(ticks / 1092 / 60) % 24;
    mins = (unsigned int)((ticks / 1092) % 60);

    buf[0] = (char)('0' + hrs / 10);
    buf[1] = (char)('0' + hrs % 10);
    buf[2] = ':';
    buf[3] = (char)('0' + mins / 10);
    buf[4] = (char)('0' + mins % 10);
    buf[5] = '\0';
}

/*
 * dc_init - Initialize client state.
 *
 * Zeros the state struct, loads server/channel data from the stub,
 * and selects the last-used channel from saved config.
 */
void dc_init(dc_state_t *s)
{
    int ch;

    memset(s, 0, sizeof(dc_state_t));
    s->running = 1;
    s->focus = DC_FOCUS_COMPOSE;

    /* Load config before stub_load_server so last_channel is available */
    dc_config_defaults(&s->config);

    stub_load_server(s);

    /* Restore last channel from config, clamped to valid range */
    ch = s->config.last_channel;
    if (s->channel_count > 0) {
        if (ch < 0 || ch >= s->channel_count)
            ch = s->channel_count - 1;
    } else {
        ch = 0;
    }
    s->selected_channel = ch;

    /* Clear unread flag on the initial channel */
    if (ch < s->channel_count)
        s->channels[ch].unread = 0;

    /* Load initial channel's messages from far heap to near buffer */
    stub_load_channel_msgs(s, ch);

    s->dirty = 1;
}

/*
 * dc_switch_channel - Change the active channel.
 *
 * Validates the index, updates selection, clears the unread
 * indicator, and resets scroll position.
 */
void dc_switch_channel(dc_state_t *s, int channel_idx)
{
    if (channel_idx < 0 || channel_idx >= s->channel_count)
        return;

    /* Save current channel's messages back to far heap */
    stub_save_channel_msgs(s);

    s->selected_channel = channel_idx;
    s->channels[channel_idx].unread = 0;
    s->msg_scroll = 0;

    /* Load new channel's messages from far heap */
    stub_load_channel_msgs(s, channel_idx);

    s->dirty = 1;
}

/*
 * dc_send_message - Post a message to the current channel.
 *
 * Appends to the far-heap message array. If the array is full,
 * shifts all messages down by one to make room (dropping the
 * oldest message).
 */
void dc_send_message(dc_state_t *s, const char *text)
{
    int ch;
    int count;
    dc_message_t far *m;
    char ts[6];

    if (!text || !text[0]) return;

    ch = s->selected_channel;
    if (ch < 0 || ch >= DC_MAX_CHANNELS) return;
    if (!chan_msgs[ch]) return;

    count = chan_msg_count[ch];

    /* If at capacity, shift all messages down by 1 (drop oldest) */
    if (count >= DC_MAX_MESSAGES) {
        _fmemcpy((void far *)&chan_msgs[ch][0],
                 (void far *)&chan_msgs[ch][1],
                 (unsigned)((DC_MAX_MESSAGES - 1) * sizeof(dc_message_t)));
        count = DC_MAX_MESSAGES - 1;
    }

    /* Build timestamp */
    dc_make_timestamp(ts);

    /* Write new message into the far-heap array */
    m = &chan_msgs[ch][count];

    _fstrncpy((char far *)m->text, (const char far *)text,
              DC_MAX_MSG_LEN - 1);
    m->text[DC_MAX_MSG_LEN - 1] = '\0';

    _fstrncpy((char far *)m->author, (const char far *)s->config.username,
              DC_MAX_AUTHOR_LEN - 1);
    m->author[DC_MAX_AUTHOR_LEN - 1] = '\0';

    m->author_color = dc_author_color(s->config.username);
    m->is_self = 1;

    _fstrncpy((char far *)m->timestamp, (const char far *)ts, 5);
    m->timestamp[5] = '\0';

    chan_msg_count[ch] = count + 1;

    s->msg_scroll = 0;
    s->dirty = 1;
}

/*
 * dc_poll_messages - Check for incoming messages.
 *
 * Delegates to the stub layer which injects timed fake messages
 * for offline testing.
 */
void dc_poll_messages(dc_state_t *s)
{
    stub_inject_timed_message(s);
}
