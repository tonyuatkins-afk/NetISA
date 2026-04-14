/*
 * discord_cfg.h - Feature flags for Discord v2 DOS client
 *
 * Toggle features at compile time. Disabled features compile out
 * completely, saving code size for 8088 small-model builds.
 */

#ifndef DISCORD_CFG_H
#define DISCORD_CFG_H

#define FEAT_AUDIO      1    /* PC speaker notifications */
#define FEAT_SEARCH     1    /* Ctrl+F find in messages */
#define FEAT_THREADS    0    /* Thread view (Phase 2) */
#define FEAT_USERS      1    /* Alt+U user list overlay */
#define FEAT_REACTIONS  1    /* CP437 reaction display */
#define FEAT_MULTILINE  1    /* Shift+Enter multi-line compose */

#endif /* DISCORD_CFG_H */
