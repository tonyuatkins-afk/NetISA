/*
 * claude.c - Chat state machine and API communication
 *
 * Manages conversation state, message sending/receiving,
 * agent mode command execution, and keyboard dispatch.
 * Messages are stored on the far heap via _fmalloc.
 */

#include "claude.h"
#include <string.h>
#include <stdio.h>
#include <malloc.h>
#include <dos.h>

/* Extended key codes */
#define KEY_F2      0x3C00
#define KEY_F4      0x3E00
#define KEY_PGUP    0x4900
#define KEY_PGDN    0x5100
#define KEY_CTRL_Q  0x11

/* Forward: compute display lines for a message */
static int calc_display_lines(cl_message_t far *m);

/* Copy a near string into a far message's text field */
static void set_msg_text(cl_message_t far *m, const char *text)
{
    int len = (int)strlen(text);
    if (len >= MAX_MSG_LEN) len = MAX_MSG_LEN - 1;
    _fmemcpy((void far *)m->text, (const void far *)text, len);
    m->text[len] = '\0';
}

static void add_message(cl_state_t *s, char role, const char *text)
{
    cl_message_t far *m;
    int i;

    if (!s->messages) return;

    if (s->message_count >= MAX_MESSAGES) {
        /* Drop oldest message: copy element-by-element to avoid
         * overlapping _fmemcpy (undefined behavior) */
        int j;
        for (j = 0; j < MAX_MESSAGES - 1; j++)
            _fmemcpy((void far *)&s->messages[j],
                     (const void far *)&s->messages[j + 1],
                     sizeof(cl_message_t));
        s->message_count = MAX_MESSAGES - 1;
    }

    m = &s->messages[s->message_count];
    m->role = role;
    set_msg_text(m, text);
    m->display_lines = calc_display_lines(m);
    s->message_count++;

    /* Recalculate total display lines */
    s->total_display_lines = 0;
    for (i = 0; i < s->message_count; i++)
        s->total_display_lines += s->messages[i].display_lines + 1;

    /* Auto-scroll to bottom */
    s->scroll_pos = 0;
}

/* Count display rows for a single line segment (no newlines) */
static int wrap_lines(int seg_len, int first_w, int cont_w)
{
    int lines;
    int remaining;

    if (seg_len <= first_w) return 1;
    lines = 1;
    remaining = seg_len - first_w;
    while (remaining > 0) {
        remaining -= cont_w;
        lines++;
    }
    return lines;
}

/* Calculate how many screen rows a message occupies.
 * Handles both word wrap and explicit \n characters. */
static int calc_display_lines(cl_message_t far *m)
{
    int prefix_len;
    int first_w, cont_w;
    int total;
    const char far *p;
    int seg_len;
    int first_seg;
    char role = m->role;

    switch (role) {
    case ROLE_USER:   prefix_len = 5;  break;  /* "You: " */
    case ROLE_CLAUDE: prefix_len = 8;  break;  /* "Claude: " */
    case ROLE_SYSTEM: prefix_len = 2;  break;  /* "> " for command line */
    default:          prefix_len = 0;  break;
    }

    /* System messages: count newlines (each line renders independently) */
    if (role == ROLE_SYSTEM) {
        p = (const char far *)m->text;
        total = 1;
        while (*p) {
            if (*p == '\n') total++;
            p++;
        }
        return total;
    }

    first_w = 80 - prefix_len;
    cont_w = 80 - 2;
    if (first_w <= 0) first_w = 1;
    if (cont_w <= 0) cont_w = 1;

    /* Walk through text, splitting on \n, wrapping each segment */
    p = (const char far *)m->text;
    total = 0;
    first_seg = 1;

    while (*p || first_seg) {
        /* Measure segment length up to next \n or end */
        seg_len = 0;
        while (p[seg_len] != '\0' && p[seg_len] != '\n') seg_len++;

        if (first_seg) {
            total += wrap_lines(seg_len, first_w, cont_w);
            first_seg = 0;
        } else {
            total += wrap_lines(seg_len, cont_w, cont_w);
        }

        p += seg_len;
        if (*p == '\n') p++;
        if (*p == '\0' && !first_seg) break;
    }

    if (total == 0) total = 1;
    return total;
}

void cl_init(cl_state_t *s)
{
    unsigned long alloc_size;

    memset(s, 0, sizeof(cl_state_t));
    strcpy(s->model, "claude-sonnet-4-20250514");
    s->exec_mode = EXEC_OFF;
    s->running = 1;
    s->composing = 1;

    /* Allocate far heap message pool */
    alloc_size = (unsigned long)MAX_MESSAGES * sizeof(cl_message_t);
    if (alloc_size <= 65535UL) {
        s->messages = (cl_message_t far *)_fmalloc((unsigned)alloc_size);
        if (s->messages)
            _fmemset((void far *)s->messages, 0, (unsigned)alloc_size);
    }

    if (!s->messages) {
        /* Far heap alloc failed — can't store messages */
        return;
    }

    add_message(s, ROLE_CLAUDE,
        "Hello! I'm Claude, running on a vintage PC via NetISA. "
        "I can chat, answer questions, and in Agent mode (F4) I can "
        "run DOS commands on this machine. How can I help?");
}

void cl_shutdown(cl_state_t *s)
{
    if (s->messages) {
        _ffree(s->messages);
        s->messages = (cl_message_t far *)0;
    }
}

void cl_send_message(cl_state_t *s, const char *text)
{
    static char response[MAX_MSG_LEN];

    add_message(s, ROLE_USER, text);
    s->waiting = 1;

    /* Render the "thinking" state before blocking on the stub delay */
    cl_render_titlebar(s);
    cl_render_chat(s);
    cl_render_compose(s);
    cl_render_statusbar(s);

    /* Use stub for now; real API goes through INT 63h */
    stub_claude_respond(text, response, MAX_MSG_LEN);

    cl_process_response(s, response);
}

void cl_process_response(cl_state_t *s, const char *text)
{
    char cmd[256];

    add_message(s, ROLE_CLAUDE, text);
    s->waiting = 0;

    /* Check for [EXEC] tags if agent mode enabled */
    if (s->exec_mode != EXEC_OFF && cl_parse_exec(text, cmd, sizeof(cmd))) {
        if (s->exec_mode == EXEC_ASK) {
            s->pending_exec = 1;
            strncpy(s->pending_cmd, cmd, sizeof(s->pending_cmd) - 1);
            s->pending_cmd[sizeof(s->pending_cmd) - 1] = '\0';
        } else if (s->exec_mode == EXEC_AUTO) {
            /* Run agent loop iteratively to avoid recursive stack growth.
             * Each iteration: execute command, send output, get response.
             * Stops when Claude's response has no [EXEC] tag. */
            int depth;
            for (depth = 0; depth < 5; depth++) {
                /* Static: not re-entrant, but avoids ~2KB stack per
                 * iteration which overflows the DOS stack. */
                static char output[MAX_MSG_LEN];
                static char sys_msg[MAX_MSG_LEN];
                static char send_buf[MAX_MSG_LEN];
                static char response[MAX_MSG_LEN];
                int len, slen;

                len = cl_exec_command(cmd, output, sizeof(output));
                if (len < 0) break;

                slen = snprintf(sys_msg, sizeof(sys_msg), "> %s\n%s",
                                cmd, output);
                if (slen >= (int)sizeof(sys_msg))
                    sys_msg[sizeof(sys_msg) - 1] = '\0';
                add_message(s, ROLE_SYSTEM, sys_msg);

                slen = snprintf(send_buf, sizeof(send_buf),
                                "Command output:\n%s", output);
                if (slen >= (int)sizeof(send_buf))
                    send_buf[sizeof(send_buf) - 1] = '\0';

                /* Send output to Claude and get next response */
                add_message(s, ROLE_USER, send_buf);
                s->waiting = 1;
                cl_render_titlebar(s);
                cl_render_chat(s);
                cl_render_compose(s);
                cl_render_statusbar(s);
                stub_claude_respond(send_buf, response, MAX_MSG_LEN);
                add_message(s, ROLE_CLAUDE, response);
                s->waiting = 0;

                /* Check if new response has another [EXEC] tag */
                if (!cl_parse_exec(response, cmd, sizeof(cmd)))
                    break;
            }
        }
    }
}

void cl_execute_pending(cl_state_t *s)
{
    char output[MAX_MSG_LEN];
    char sys_msg[MAX_MSG_LEN];
    char send_buf[MAX_MSG_LEN];
    int len, slen;

    len = cl_exec_command(s->pending_cmd, output, sizeof(output));
    s->pending_exec = 0;

    if (len >= 0) {
        slen = snprintf(sys_msg, sizeof(sys_msg), "> %s\n%s",
                        s->pending_cmd, output);
        if (slen >= (int)sizeof(sys_msg))
            sys_msg[sizeof(sys_msg) - 1] = '\0';
        add_message(s, ROLE_SYSTEM, sys_msg);

        slen = snprintf(send_buf, sizeof(send_buf),
                        "Command output:\n%s", output);
        if (slen >= (int)sizeof(send_buf))
            send_buf[sizeof(send_buf) - 1] = '\0';
        cl_send_message(s, send_buf);
    }
}

void cl_skip_pending(cl_state_t *s)
{
    s->pending_exec = 0;
    add_message(s, ROLE_CLAUDE, "(Command skipped by user)");
}

void cl_poll(cl_state_t *s)
{
    /* Thinking animation handled in render.
     * In production: check INT 63h for async response. */
    (void)s;
}

void cl_handle_key(cl_state_t *s, int key)
{
    int ch = key & 0xFF;
    int scan = key & 0xFF00;

    /* Global: Ctrl+Q = quit */
    if (ch == KEY_CTRL_Q) {
        s->running = 0;
        return;
    }

    /* Global: F2 = new conversation */
    if (ch == 0 && scan == KEY_F2) {
        /* Reset state but keep far allocation */
        s->message_count = 0;
        s->scroll_pos = 0;
        s->total_display_lines = 0;
        s->compose_buf[0] = '\0';
        s->compose_len = 0;
        s->compose_cursor = 0;
        s->waiting = 0;
        s->pending_exec = 0;
        s->composing = 1;
        if (s->messages)
            _fmemset((void far *)s->messages, 0,
                     (unsigned)((unsigned long)MAX_MESSAGES * sizeof(cl_message_t)));
        add_message(s, ROLE_CLAUDE,
            "Hello! I'm Claude, running on a vintage PC via NetISA. "
            "I can chat, answer questions, and in Agent mode (F4) I can "
            "run DOS commands on this machine. How can I help?");
        return;
    }

    /* Global: F4 = cycle exec mode */
    if (ch == 0 && scan == KEY_F4) {
        switch (s->exec_mode) {
        case EXEC_OFF:  s->exec_mode = EXEC_ASK;  break;
        case EXEC_ASK:  s->exec_mode = EXEC_AUTO; break;
        case EXEC_AUTO: s->exec_mode = EXEC_OFF;  break;
        }
        return;
    }

    /* Pending exec: Y/N only */
    if (s->pending_exec) {
        if (ch == 'y' || ch == 'Y') {
            cl_execute_pending(s);
        } else if (ch == 'n' || ch == 'N') {
            cl_skip_pending(s);
        }
        return;
    }

    /* Page Up / Page Down: scroll chat (don't pass to compose) */
    if (ch == 0) {
        switch (scan) {
        case KEY_PGUP:
            s->scroll_pos += CL_CHAT_ROWS;
            /* Clamp scroll to prevent negative vis_start in renderer */
            {
                int max_scroll = s->total_display_lines - CL_CHAT_ROWS;
                if (max_scroll < 0) max_scroll = 0;
                if (s->scroll_pos > max_scroll)
                    s->scroll_pos = max_scroll;
            }
            return;
        case KEY_PGDN:
            s->scroll_pos -= CL_CHAT_ROWS;
            if (s->scroll_pos < 0) s->scroll_pos = 0;
            return;
        }
    }

    /* Don't accept compose input while waiting */
    if (s->waiting) return;

    /* Compose input */
    cl_compose_key(s, key);
}
