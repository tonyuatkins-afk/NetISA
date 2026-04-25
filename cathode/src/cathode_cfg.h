/*
 * cathode_cfg.h - Compile-time feature flags for Cathode browser
 *
 * Phase 1 features (parser, fetch, UTF-8, URL, scroll, search,
 * bookmarks, spinner) are always compiled.
 * Phase 2/3 features are guarded by these flags.
 */

#ifndef CATHODE_CFG_H
#define CATHODE_CFG_H

/* Phase 2: Interaction polish */
#define FEAT_MOUSE    1    /* INT 33h mouse support */
#define FEAT_TABS     0    /* Multi-tab browsing (Phase 2b) */
#define FEAT_THEMES   0    /* VGA DAC color themes */

/* Phase 3: Compatibility expansion */
#define FEAT_TABLES   0    /* Buffered table rendering */
#define FEAT_FORMS    0    /* Form field display and GET submission */
#define FEAT_IMAGES   0    /* Half-block image rendering */

#endif /* CATHODE_CFG_H */
