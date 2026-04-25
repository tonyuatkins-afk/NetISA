/*
 * claude.h - Claude DOS client types and constants
 *
 * Core state structure, message types, and function prototypes for
 * the Claude chat client running on vintage ISA PCs via NetISA.
 *
 * Memory design: messages are stored on the far heap to stay under
 * the 64KB DGROUP limit in small model. The near state struct holds
 * only metadata, compose buffer, and a pointer to the far pool.
 */

#ifndef CLAUDE_H
#define CLAUDE_H

#include "screen.h"

/* Limits */
#define MAX_MESSAGES        30
#define MAX_MSG_LEN         512
#define MAX_COMPOSE_LEN     234   /* 3 lines x 78 chars */

/* Layout constants */
#define CL_TITLE_ROW       0
#define CL_SEP1_ROW        1
#define CL_CHAT_TOP        2
#define CL_CHAT_BOT        19
#define CL_CHAT_ROWS       18
#define CL_SEP2_ROW        20
#define CL_COMPOSE_TOP     21
#define CL_COMPOSE_BOT     23
#define CL_COMPOSE_ROWS    3
#define CL_STATUS_ROW      24

/* Execution modes */
typedef enum {
    EXEC_OFF,       /* Chat only, no command execution */
    EXEC_ASK,       /* Claude proposes commands, user confirms */
    EXEC_AUTO       /* Claude runs commands freely */
} exec_mode_t;

/* Message roles */
#define ROLE_USER       'U'
#define ROLE_CLAUDE     'C'
#define ROLE_SYSTEM     'S'

/* Message structure (~518 bytes each, stored on far heap) */
typedef struct {
    char role;                  /* 'U' = user, 'C' = claude, 'S' = system */
    char text[MAX_MSG_LEN];
    int display_lines;          /* wrapped line count for scrolling */
} cl_message_t;

/* Client state (near heap, ~300 bytes) */
typedef struct {
    cl_message_t far *messages; /* far-heap allocated pool */
    int message_count;
    int scroll_pos;             /* scroll offset in display lines */
    int total_display_lines;

    char compose_buf[MAX_COMPOSE_LEN + 1];
    int compose_len;
    int compose_cursor;

    int running;
    int waiting;                /* 1 = waiting for Claude response */
    int composing;              /* 1 = compose area has focus */
    int pending_exec;           /* 1 = waiting for user Y/N on command */
    char pending_cmd[256];      /* command waiting for approval */

    exec_mode_t exec_mode;
    char model[32];             /* "claude-sonnet-4-20250514" */
} cl_state_t;

/* State machine (claude.c) */
void cl_init(cl_state_t *s);
void cl_shutdown(cl_state_t *s);
void cl_send_message(cl_state_t *s, const char *text);
void cl_process_response(cl_state_t *s, const char *text);
void cl_execute_pending(cl_state_t *s);
void cl_skip_pending(cl_state_t *s);
void cl_poll(cl_state_t *s);
void cl_handle_key(cl_state_t *s, int key);

/* Rendering (chat.c) */
void cl_render_titlebar(cl_state_t *s);
void cl_render_chat(cl_state_t *s);
void cl_render_statusbar(cl_state_t *s);

/* Compose (compose.c) */
void cl_render_compose(cl_state_t *s);
void cl_compose_key(cl_state_t *s, int key);

/* Agent (agent.c) */
int cl_parse_exec(const char *response, char *cmd, int cmd_max);
int cl_exec_command(const char *cmd, char *output, int max_len);

/* Splash screen (splash.c) */
void cl_splash(void);

/* Stub (stub_claude.c) */
int stub_claude_respond(const char *user_msg, char *response, int max_len);

#endif /* CLAUDE_H */
