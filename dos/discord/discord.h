/*
 * discord.h - Discord client state and types
 *
 * Memory design: channels store only name/unread flag. Messages are
 * in a single shared pool for the active channel (reloaded on switch).
 * This keeps the state struct well under 64KB for small model.
 */

#ifndef DISCORD_H
#define DISCORD_H

#include "screen.h"

/* Limits */
#define DC_MAX_CHANNELS     8
#define DC_MAX_MESSAGES     32
#define DC_MAX_MSG_LEN      200
#define DC_MAX_AUTHOR_LEN   16
#define DC_MAX_CHAN_NAME     20
#define DC_MAX_SERVER_NAME  24
#define DC_MAX_COMPOSE      160

/* Focus states */
#define DC_FOCUS_CHANNELS   0
#define DC_FOCUS_MESSAGES   1
#define DC_FOCUS_COMPOSE    2

/* Layout constants */
#define DC_CHAN_WIDTH        20
#define DC_MSG_LEFT         21
#define DC_MSG_WIDTH        59
#define DC_TITLE_ROW        0
#define DC_CONTENT_TOP      1
#define DC_CONTENT_BOT      22
#define DC_COMPOSE_ROW      23
#define DC_STATUS_ROW       24
#define DC_CONTENT_ROWS     22

/* Author color palette */
#define DC_NUM_COLORS       6

/* Message structure (~232 bytes each) */
typedef struct {
    char author[DC_MAX_AUTHOR_LEN];
    char text[DC_MAX_MSG_LEN];
    char timestamp[6];
    unsigned char author_color;
    int is_self;
} dc_message_t;

/* Channel structure (just metadata, no messages) */
typedef struct {
    char name[DC_MAX_CHAN_NAME];
    int unread;
} dc_channel_t;

/* Compose buffer */
typedef struct {
    char buf[DC_MAX_COMPOSE + 1];
    int len;
    int cursor;
} dc_compose_t;

/* Full client state (~8.5KB, fits in near heap) */
typedef struct {
    char server_name[DC_MAX_SERVER_NAME];
    dc_channel_t channels[DC_MAX_CHANNELS];
    int channel_count;
    int selected_channel;

    /* Active channel message pool */
    dc_message_t messages[DC_MAX_MESSAGES];
    int msg_count;
    int msg_scroll;

    int focus;
    dc_compose_t compose;
    int running;
    unsigned long last_poll_tick;
} dc_state_t;

/* State machine */
void dc_init(dc_state_t *s);
void dc_switch_channel(dc_state_t *s, int channel_idx);
void dc_send_message(dc_state_t *s, const char *text);
void dc_poll_messages(dc_state_t *s);
void dc_handle_key(dc_state_t *s, int key);

/* Rendering */
void dc_render_titlebar(dc_state_t *s);
void dc_render_channels(dc_state_t *s);
void dc_render_messages(dc_state_t *s);
void dc_render_compose(dc_state_t *s);
void dc_render_statusbar(dc_state_t *s);

#endif /* DISCORD_H */
