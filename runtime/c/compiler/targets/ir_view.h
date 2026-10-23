/* SPDX-License-Identifier: MIT
 * Read-only identities shared by text targets. No grammar, code generation,
 * target address policy, allocation or diagnostic mutation lives here. */
#ifndef DOE_TARGET_IR_VIEW_H
#define DOE_TARGET_IR_VIEW_H
#include "../src/internal.h"

static inline bool target_value_type(const CompilerWorkspace *c, uint32_t id, TypeId *type) {
    if (id > c->limits.instructions) {
        const uint32_t formal = id - c->limits.instructions - 1u;
        if (formal >= c->formal_count) return false;
        *type = c->formals[formal].type.resolved.type;
        return true;
    }
    if (id == 0u || id > c->instruction_count || c->instructions[id - 1u].result != id) return false;
    *type = c->instructions[id - 1u].type;
    return true;
}

static inline const char *target_access_path(const CompilerWorkspace *c, uint32_t path,
                                             uint32_t *steps, uint32_t capacity, uint32_t *count) {
    *count = 0u;
    while (path != NO_ID) {
        if (path >= c->access_count || *count == capacity)
            return "access path exceeds its identity or traversal bounds";
        steps[(*count)++] = path;
        const uint32_t parent = c->accesses[path].parent;
        if (parent != NO_ID && parent >= path) return "access path is cyclic or unordered";
        path = parent;
    }
    return NULL;
}

static inline const ResourceRecord *target_runtime_array(const CompilerWorkspace *c, uint32_t index) {
    if (index >= c->resource_count) return NULL;
    const ResourceRecord *r = &c->resources[index];
    if (r->kind != DOE_COMPILER_STORAGE_BUFFER ||
        (r->flags & DOE_COMPILER_RESOURCE_RUNTIME_TAIL) == 0u || r->tail_stride == 0u) return NULL;
    return r;
}
#endif
