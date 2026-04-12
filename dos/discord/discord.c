/*
 * discord.c - Discord client state machine
 */

#include "discord.h"
#include <string.h>
#include <dos.h>

/* Defined in stub_discord.c */
extern void stub_load_server(dc_state_t *s);
extern void stub_load_channel_msgs(dc_state_t *s, int ch_idx);
extern void stub_save_channel_msgs(dc_state_t *s);
extern void stub_inject_timed_message(dc_state_t *s);

void dc_init(dc_state_t *s)
{
    memset(s, 0, sizeof(dc_state_t));
    s->running = 1;
    s->focus = DC_FOCUS_COMPOSE;
    s->selected_channel = 0;
    s->compose.len = 0;
    s->compose.cursor = 0;
    s->compose.buf[0] = '\0';

    stub_load_server(s);

    /* Load initial channel messages (avoid dc_switch_channel which would
       save the empty near buffer over channel 0's far storage first) */
    stub_load_channel_msgs(s, 0);
    s->dirty = 1;
}

void dc_switch_channel(dc_state_t *s, int channel_idx)
{
    if (channel_idx < 0 || channel_idx >= s->channel_count)
        return;

    /* Save current channel's messages */
    stub_save_channel_msgs(s);

    s->selected_channel = channel_idx;
    s->channels[channel_idx].unread = 0;
    s->msg_scroll = 0;

    /* Load new channel's messages */
    stub_load_channel_msgs(s, channel_idx);
    s->dirty = 1;
}

void dc_send_message(dc_state_t *s, const char *text)
{
    dc_message_t *m;
    unsigned long now;
    char ts[6];

    if (!text || !text[0]) return;

    /* If pool is full, drop oldest message to make room */
    if (s->msg_count >= DC_MAX_MESSAGES) {
        memmove(&s->messages[0], &s->messages[1],
                (DC_MAX_MESSAGES - 1) * sizeof(dc_message_t));
        s->msg_count = DC_MAX_MESSAGES - 1;
    }

    /* Build timestamp from BIOS ticks */
    _asm {
        xor ax, ax
        int 1Ah
        mov word ptr now, dx
        mov word ptr now+2, cx
    }
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

    m = &s->messages[s->msg_count];
    strcpy(m->author, "You");
    strncpy(m->text, text, DC_MAX_MSG_LEN - 1);
    m->text[DC_MAX_MSG_LEN - 1] = '\0';
    strcpy(m->timestamp, ts);
    m->author_color = SCR_ATTR(SCR_LIGHTGREEN, SCR_BLACK);
    m->is_self = 1;
    s->msg_count++;

    /* Save to persistent storage */
    stub_save_channel_msgs(s);

    /* Clear compose */
    s->compose.buf[0] = '\0';
    s->compose.len = 0;
    s->compose.cursor = 0;

    /* Scroll to bottom */
    s->msg_scroll = 0;
    s->dirty = 1;
}

void dc_poll_messages(dc_state_t *s)
{
    stub_inject_timed_message(s);
}
