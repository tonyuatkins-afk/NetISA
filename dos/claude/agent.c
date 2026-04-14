/*
 * agent.c - Agent mode: command parsing, execution, output capture
 *
 * Scans Claude responses for [EXEC]...[/EXEC] tags, executes
 * DOS commands via COMMAND.COM, and captures stdout+stderr.
 */

#include "claude.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define EXEC_OPEN   "[EXEC]"
#define EXEC_CLOSE  "[/EXEC]"
#define TEMP_FILE   "C:\\CLTEMP.TXT"

int cl_parse_exec(const char *response, char *cmd, int cmd_max)
{
    const char *start;
    const char *end;
    int len;

    start = strstr(response, EXEC_OPEN);
    if (!start) return 0;

    start += strlen(EXEC_OPEN);
    end = strstr(start, EXEC_CLOSE);
    if (!end) return 0;

    len = (int)(end - start);
    if (len <= 0 || len >= cmd_max) return 0;

    memcpy(cmd, start, len);
    cmd[len] = '\0';
    return 1;
}

int cl_exec_command(const char *cmd, char *output, int max_len)
{
    char full_cmd[300];
    FILE *f;
    int len;

    /* Reject commands with dangerous shell metacharacters */
    {
        const char *p = cmd;
        while (*p) {
            if (*p == '|' || *p == '&' || *p == ';' || *p == '%' ||
                *p == '<' || *p == '>' || *p == '`' || *p == '$') {
                return -1;  /* rejected: shell metacharacter */
            }
            p++;
        }
    }

    /* NOTE: stderr is not captured. DOS COMMAND.COM does not support
     * 2>&1 redirection. Any stderr output will write directly to the
     * console/VGA buffer, temporarily corrupting the chat UI. The
     * screen is redrawn after the command completes. */
    snprintf(full_cmd, sizeof(full_cmd), "%s > %s", cmd, TEMP_FILE);
    system(full_cmd);

    f = fopen(TEMP_FILE, "r");
    if (!f) return -1;

    len = (int)fread(output, 1, max_len - 1, f);
    output[len] = '\0';
    fclose(f);
    remove(TEMP_FILE);

    return len;
}
