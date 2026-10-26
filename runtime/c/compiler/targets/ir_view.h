/* SPDX-License-Identifier: MIT
 * Read-only identities shared by text targets. No grammar, code generation,
 * target address policy, allocation or diagnostic mutation lives here. */
#ifndef DOE_TARGET_IR_VIEW_H
#define DOE_TARGET_IR_VIEW_H
#include "../src/internal.h"

/* Logical index extent shared by target views. This reports typed shape only;
 * guard selection, storage policy and proof-based elision remain callers' work. */
static inline bool target_index_extent(const CompilerWorkspace *c, TypeId type,
    uint32_t *count, bool *runtime) {
    for (uint32_t depth = 0u; depth <= c->type_count; ++depth) {
        if (type < IR_PRIMITIVE_COUNT || type - IR_PRIMITIVE_COUNT >= c->type_count) return false;
        const TypeRecord *record = &c->types[type - IR_PRIMITIVE_COUNT];
        if (record->kind == TYPE_POINTER) { type = record->element; continue; }
        if (record->kind == TYPE_RUNTIME_ARRAY) {
            *count = 0u; *runtime = true; return true;
        }
        if (record->kind != TYPE_ARRAY && record->kind != TYPE_VECTOR && record->kind != TYPE_MATRIX) return false;
        if (record->count == 0u) return false;
        *count = record->count; *runtime = false; return true;
    }
    return false;
}

static inline uint32_t target_immediate_root(const CompilerWorkspace *c, const EntryRecord *entry) {
    for (uint32_t i = 0u; i < entry->state.resolved.workgroup_count; ++i) {
        const uint32_t root = c->entry_references[entry->state.resolved.workgroup_first+i];
        if (c->workgroups[root].state.resolved.address_space == MODULE_IMMEDIATE) return root;
    }
    return NO_ID;
}

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

static inline bool target_gather_component(const CompilerWorkspace *c, uint32_t id, uint32_t *component) {
    if (id == 0u || id > c->instruction_count) return false;
    const IrInstruction *value = &c->instructions[id - 1u];
    if (value->result != id || value->opcode != IR_CONSTANT ||
        (value->type != IR_I32 && value->type != IR_U32) || value->operands.scalar.left != 0u ||
        value->operands.scalar.literal > 3u) return false;
    *component = value->operands.scalar.literal;
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
