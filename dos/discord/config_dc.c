#include "discord.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_FILE "DISCORD.CFG"

void dc_config_defaults(dc_config_t *cfg)
{
    cfg->sound = 1;
    cfg->notify = 1;
    cfg->last_channel = 0;
    cfg->color_scheme = 0;
    strncpy(cfg->username, "You", DC_MAX_AUTHOR_LEN - 1);
    cfg->username[DC_MAX_AUTHOR_LEN - 1] = '\0';
}

void dc_config_load(dc_config_t *cfg)
{
    FILE *fp;
    char line[80];
    char key[32], val[48];

    dc_config_defaults(cfg);
    fp = fopen(CONFIG_FILE, "r");
    if (!fp) return;

    while (fgets(line, sizeof(line), fp)) {
        /* Parse key=value lines */
        /* Skip comments (#) and blank lines */
        /* Handle: sound, notify, last_channel, color_scheme, username */
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        if (sscanf(line, "%31[^=]=%47[^\n\r]", key, val) == 2) {
            if (strcmp(key, "sound") == 0) cfg->sound = atoi(val);
            else if (strcmp(key, "notify") == 0) cfg->notify = atoi(val);
            else if (strcmp(key, "last_channel") == 0) cfg->last_channel = atoi(val);
            else if (strcmp(key, "color_scheme") == 0) cfg->color_scheme = atoi(val);
            else if (strcmp(key, "username") == 0) {
                strncpy(cfg->username, val, DC_MAX_AUTHOR_LEN - 1);
                cfg->username[DC_MAX_AUTHOR_LEN - 1] = '\0';
            }
        }
    }
    fclose(fp);

    /* Clamp values */
    if (cfg->last_channel < 0 || cfg->last_channel >= DC_MAX_CHANNELS)
        cfg->last_channel = 0;
}

void dc_config_save(dc_config_t *cfg)
{
    FILE *fp = fopen(CONFIG_FILE, "w");
    if (!fp) return;
    fprintf(fp, "# Discord v2 Settings\n");
    fprintf(fp, "sound=%d\n", cfg->sound);
    fprintf(fp, "notify=%d\n", cfg->notify);
    fprintf(fp, "last_channel=%d\n", cfg->last_channel);
    fprintf(fp, "color_scheme=%d\n", cfg->color_scheme);
    fprintf(fp, "username=%s\n", cfg->username);
    fclose(fp);
}
