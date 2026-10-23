/* SPDX-License-Identifier: MIT
 * Resolve selected function closure once; query memory dependencies with a
 * completed-type map. Shared DAG edges never expand into repeated subtrees. */
#ifndef DOE_TARGET_CLOSURE_H
#define DOE_TARGET_CLOSURE_H
#include "scratch.h"
#include <string.h>
enum { TARGET_CLOSURE_DEPTH = 96 };
typedef struct {
    unsigned char *functions, *types;
    uint32_t function_count, type_count;
} TargetClosure;
static inline bool target_spend_count(uint64_t *work, uint64_t count, const char **error) {
    if (*work < count) {
        *error = "target closure exceeds bounded traversal work";
        return false;
    }
    *work -= count;
    return true;
}
static inline bool target_spend(uint64_t *work, const char **error) {
    return target_spend_count(work, 1u, error);
}
static inline bool target_closure_bind(const CompilerWorkspace *c, DoeTargetScratch scratch,
        TargetClosure *closure) {
    size_t bytes;
    if (!doe_target_scratch_bytes(c, &bytes) || scratch.data == NULL ||
        scratch.capacity < bytes) return false;
    *closure = (TargetClosure){scratch.data, scratch.data + c->function_count,
        c->function_count, c->type_count + 7u};
    return true;
}
static inline bool target_closure_build(const CompilerWorkspace *c, uint32_t selected,
        TargetClosure *closure, uint64_t *work, const char **error) {
    struct { uint32_t function, cursor; } frames[TARGET_CLOSURE_DEPTH];
    if (!target_spend_count(work, closure->function_count, error)) return false;
    memset(closure->functions, 0, closure->function_count);
    for (uint32_t entry = 0u; entry < c->entry_count; ++entry) {
        if (selected != NO_ID && selected != entry) continue;
        uint32_t depth = 0u, function = c->entries[entry].function;
        if (function >= closure->function_count) {
            *error = "target entry function identity is invalid";
            return false;
        }
        if (closure->functions[function] == 2u) continue;
        closure->functions[function] = 1u;
        frames[0].function = function;
        frames[0].cursor = c->functions[function].instruction_start;
        for (;;) {
            if (!target_spend(work, error)) return false;
            function = frames[depth].function;
            const FunctionRecord *f = &c->functions[function];
            if (f->instruction_start > f->instruction_end || f->instruction_end > c->instruction_count) {
                *error = "target function instruction range is invalid";
                return false;
            }
            if (frames[depth].cursor == f->instruction_end) {
                closure->functions[function] = 2u;
                if (depth == 0u) break;
                --depth;
                continue;
            }
            const IrInstruction *ir = &c->instructions[frames[depth].cursor++];
            if (ir->opcode != IR_CALL) continue;
            const uint32_t callee = ir->operands.scalar.binding;
            if (callee >= closure->function_count) {
                *error = "target call graph has an invalid function identity";
                return false;
            }
            if (closure->functions[callee] == 2u) continue;
            if (closure->functions[callee] == 1u) {
                *error = "target call graph is cyclic";
                return false;
            }
            if (depth + 1u == TARGET_CLOSURE_DEPTH) {
                *error = "target call graph exceeds bounded nesting";
                return false;
            }
            closure->functions[callee] = 1u;
            ++depth;
            frames[depth].function = callee;
            frames[depth].cursor = c->functions[callee].instruction_start;
        }
    }
    return true;
}
static inline bool target_function_used(const TargetClosure *closure, uint32_t function) {
    return function < closure->function_count && closure->functions[function] == 2u;
}
/* Arrays/structures call child helpers; vector transfers are leaves. The type
 * map is independent of live function closure and reset for each dependency. */
static inline bool target_memory_dependency(const CompilerWorkspace *c, TargetClosure *closure,
        TypeId root, TypeId wanted, uint64_t *work, const char **error) {
    struct { TypeId type; uint32_t child; } frames[TARGET_CLOSURE_DEPTH];
    if (root >= closure->type_count || wanted >= closure->type_count) {
        *error = "target memory type identity is invalid";
        return false;
    }
    if (!target_spend_count(work, closure->type_count, error)) return false;
    memset(closure->types, 0, closure->type_count);
    uint32_t depth = 0u;
    frames[0].type = root;
    frames[0].child = 0u;
    closure->types[root] = 1u;
    for (;;) {
        if (!target_spend(work, error)) return false;
        const TypeId type = frames[depth].type;
        if (type == wanted) return true;
        const TypeRecord *t = type >= 7u ? &c->types[type - 7u] : NULL;
        const uint32_t children = t != NULL && t->kind == TYPE_STRUCT ? t->count :
            t != NULL && (t->kind == TYPE_ARRAY || t->kind == TYPE_MATRIX) ? 1u : 0u;
        if (t != NULL && t->kind == TYPE_STRUCT &&
            (t->element > c->member_count || t->count > c->member_count - t->element)) {
            *error = "target memory member range is invalid";
            return false;
        }
        if (frames[depth].child == children) {
            closure->types[type] = 2u;
            if (depth == 0u) return false;
            --depth;
            continue;
        }
        const TypeId child = (t->kind == TYPE_ARRAY || t->kind == TYPE_MATRIX) ? t->element :
            c->members[t->element + frames[depth].child].type;
        ++frames[depth].child;
        if (child >= closure->type_count) {
            *error = "target memory type identity is invalid";
            return false;
        }
        if (closure->types[child] == 2u) continue;
        if (closure->types[child] == 1u) {
            *error = "target memory type graph is cyclic";
            return false;
        }
        if (depth + 1u == TARGET_CLOSURE_DEPTH) {
            *error = "target memory type exceeds bounded nesting";
            return false;
        }
        closure->types[child] = 1u;
        ++depth;
        frames[depth].type = child;
        frames[depth].child = 0u;
    }
}
/* Reuse the type map during declaration ordering, before memory-helper queries.
 * Values0/1 mean unseen/active; completed depth d is encoded as d+2. */
static inline bool target_type_order_begin(TargetClosure *closure, uint64_t *work,
        const char **error) {
    if (!target_spend_count(work, closure->type_count, error)) return false;
    memset(closure->types, 0, closure->type_count);
    return true;
}
static inline bool target_type_depth(const CompilerWorkspace *c, TargetClosure *closure,
        TypeId root, unsigned *out, uint64_t *work, const char **error) {
    struct { TypeId type; uint32_t child, maximum; } frames[TARGET_CLOSURE_DEPTH];
    if (root >= closure->type_count) {
        *error = "target declaration type identity is invalid";
        return false;
    }
    if (closure->types[root] >= 2u) {
        *out = (unsigned)closure->types[root] - 2u;
        return true;
    }
    uint32_t depth = 0u;
    frames[0].type = root;
    frames[0].child = frames[0].maximum = 0u;
    closure->types[root] = 1u;
    for (;;) {
        if (!target_spend(work, error)) return false;
        const TypeId type = frames[depth].type;
        const TypeRecord *t = type >= 7u ? &c->types[type - 7u] : NULL;
        const bool sequential = t != NULL &&
            (t->kind == TYPE_ARRAY || t->kind == TYPE_RUNTIME_ARRAY || t->kind == TYPE_MATRIX);
        const bool structure = t != NULL && t->kind == TYPE_STRUCT;
        const uint32_t children = structure ? t->count : sequential ? 1u : 0u;
        if (structure && (t->element > c->member_count || t->count > c->member_count - t->element)) {
            *error = "target declaration member range is invalid";
            return false;
        }
        if (frames[depth].child == children) {
            const uint32_t value = frames[depth].maximum + (sequential || structure ? 1u : 0u);
            if (value >= TARGET_CLOSURE_DEPTH) {
                *error = "target declaration type exceeds bounded nesting";
                return false;
            }
            closure->types[type] = (unsigned char)(value + 2u);
            if (depth == 0u) { *out = value; return true; }
            --depth;
            continue;
        }
        const TypeId child = structure ? c->members[t->element + frames[depth].child].type : t->element;
        if (child >= closure->type_count) {
            *error = "target declaration child identity is invalid";
            return false;
        }
        if (closure->types[child] >= 2u) {
            const uint32_t value = (uint32_t)closure->types[child] - 2u;
            if (value > frames[depth].maximum) frames[depth].maximum = value;
            ++frames[depth].child;
            continue;
        }
        if (closure->types[child] == 1u || depth + 1u == TARGET_CLOSURE_DEPTH) {
            *error = "target declaration graph is cyclic or exceeds nesting";
            return false;
        }
        closure->types[child] = 1u;
        ++depth;
        frames[depth].type = child;
        frames[depth].child = frames[depth].maximum = 0u;
    }
}
static inline bool target_access_helper_used(const CompilerWorkspace *c, TargetClosure *closure,
        uint32_t category, uint32_t root, TypeId type, bool store,
        uint64_t *work, const char **error) {
    for (uint32_t function = 0u; function < c->function_count; ++function) {
        if (!target_spend(work, error)) return false;
        if (!target_function_used(closure, function)) continue;
        const FunctionRecord *f = &c->functions[function];
        for (uint32_t i = f->instruction_start; i < f->instruction_end; ++i) {
            if (!target_spend(work, error)) return false;
            const IrInstruction *ir = &c->instructions[i];
            if (ir->opcode != (store ? IR_ACCESS_STORE : IR_ACCESS_LOAD) ||
                ir->operands.access.category != category || ir->operands.access.root != root) continue;
            if (target_memory_dependency(c, closure, ir->type, type, work, error)) return true;
            if (*error != NULL) return false;
        }
    }
    return false;
}
#endif
