/* SPDX-License-Identifier: MIT
 * Shared typed interface span validation; target attributes remain target-owned. */
#ifndef DOE_TARGET_STAGE_VIEW_H
#define DOE_TARGET_STAGE_VIEW_H
#include "../src/stage.h"
static inline const StageEntry *target_stage_entry(const Source *source,
        const CompilerWorkspace *c, uint32_t index, const char **error) {
    if (source->stages == NULL || index >= c->entry_count) {
        *error = "graphics entry has no typed interface workspace";
        return NULL;
    }
    const StageEntry *entry = &source->stages->entries[index];
    if (entry->formal_first > c->formal_count || entry->formal_count > c->formal_count - entry->formal_first ||
        entry->interface_first > source->stages->interface_count ||
        entry->interface_count > source->stages->interface_count - entry->interface_first) {
        *error = "graphics interface span is invalid";
        return NULL;
    }
    return entry;
}
#endif
