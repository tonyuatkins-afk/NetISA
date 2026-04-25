/*
 * unlock/unlock.h - Feature unlock matrix.
 * Copyright (c) 2026 Tony Atkins. MIT License.
 */
#ifndef HEARO_UNLOCK_H
#define HEARO_UNLOCK_H

#include "../hearo.h"

typedef hbool (*unlock_check_fn)(const hw_profile_t *hw);

typedef struct {
    unlock_id_t      id;
    const char      *name;
    const char      *desc;
    const char      *req;       /* requirement string for locked entries */
    unlock_check_fn  check;
} unlock_rule_t;

/* Walk the rule table, mark each entry unlocked or locked. */
void unlock_evaluate(const hw_profile_t *hw);

/* How many entries are currently unlocked. */
u16 unlock_count_enabled(void);

/* Lookup by id. NULL if id out of range. */
const unlock_entry_t *unlock_get(unlock_id_t id);

/* Pointer to the full table; iterate through UL_COUNT entries. */
const unlock_entry_t *unlock_get_all(void);

/* Read access to the rule table for tooling. */
const unlock_rule_t  *unlock_rules(void);
u16 unlock_rule_count(void);

#endif
