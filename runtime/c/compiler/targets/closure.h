/* SPDX-License-Identifier: MIT
 * Read-only target closure queries over validated function/access/type IDs.
 * No retained bitmap or semantic mutation; every repeated walk spends work. */
#ifndef DOE_TARGET_CLOSURE_H
#define DOE_TARGET_CLOSURE_H
#include "../src/internal.h"
enum { TARGET_CLOSURE_DEPTH = 96 };

static inline bool target_spend(uint64_t *work, const char **error) {
    if (*work == 0u) {
        *error = "target closure exceeds bounded traversal work";
        return false;
    }
    --*work;
    return true;
}

static inline bool target_function_reachable(const CompilerWorkspace *c, uint32_t selected,
        uint32_t wanted, uint64_t *work, const char **error) {
    struct { uint32_t function, cursor; } frames[TARGET_CLOSURE_DEPTH];
    for (uint32_t entry = 0u; entry < c->entry_count; ++entry) {
        if (selected != NO_ID && selected != entry) continue;
        uint32_t depth = 0u, function = c->entries[entry].function;
        if (function >= c->function_count) {
            *error = "target entry function identity is invalid";
            return false;
        }
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
            if (function == wanted) return true;
            if (frames[depth].cursor == f->instruction_end) {
                if (depth == 0u) break;
                --depth;
                continue;
            }
            const IrInstruction *ir = &c->instructions[frames[depth].cursor++];
            if (ir->opcode != IR_CALL) continue;
            const uint32_t callee = ir->operands.scalar.binding;
            if (callee >= c->function_count || depth + 1u == TARGET_CLOSURE_DEPTH) {
                *error = "target call graph exceeds identity or nesting bounds";
                return false;
            }
            for (uint32_t i = 0u; i <= depth; ++i) {
                if (frames[i].function == callee) {
                    *error = "target call graph is cyclic";
                    return false;
                }
            }
            ++depth;
            frames[depth].function = callee;
            frames[depth].cursor = c->functions[callee].instruction_start;
        }
    }
    return false;
}

/* Memory helpers recurse through arrays/structures; vector transfers are leaves
 * and do not call scalar helpers. This is helper dependency, not WGSL subtyping. */
static inline bool target_memory_dependency(const CompilerWorkspace *c, TypeId root,
        TypeId wanted, uint64_t *work, const char **error) {
    struct { TypeId type; uint32_t child; } frames[TARGET_CLOSURE_DEPTH];
    uint32_t depth = 0u;
    frames[0].type = root;
    frames[0].child = 0u;
    for (;;) {
        if (!target_spend(work, error)) return false;
        const TypeId type = frames[depth].type;
        if (type == wanted) return true;
        if (type >= 7u && type - 7u >= c->type_count) {
            *error = "target memory type identity is invalid";
            return false;
        }
        const TypeRecord *t = type >= 7u ? &c->types[type - 7u] : NULL;
        uint32_t children = t != NULL && t->kind == TYPE_STRUCT ? t->count :
            t != NULL && t->kind == TYPE_ARRAY ? 1u : 0u;
        if (t != NULL && t->kind == TYPE_STRUCT &&
            (t->element > c->member_count || t->count > c->member_count - t->element)) {
            *error = "target memory member range is invalid";
            return false;
        }
        if (frames[depth].child == children) {
            if (depth == 0u) return false;
            --depth;
            continue;
        }
        const TypeId child = t->kind == TYPE_ARRAY ? t->element :
            c->members[t->element + frames[depth].child].type;
        ++frames[depth].child;
        if (depth + 1u == TARGET_CLOSURE_DEPTH) {
            *error = "target memory type exceeds bounded nesting";
            return false;
        }
        for (uint32_t i = 0u; i <= depth; ++i) {
            if (frames[i].type == child) {
                *error = "target memory type graph is cyclic";
                return false;
            }
        }
        ++depth;
        frames[depth].type = child;
        frames[depth].child = 0u;
    }
}

static inline bool target_access_helper_used(const CompilerWorkspace *c, uint32_t selected,
        uint32_t category, uint32_t root, TypeId type, bool store,
        uint64_t *work, const char **error) {
    for (uint32_t function = 0u; function < c->function_count; ++function) {
        if (!target_function_reachable(c, selected, function, work, error)) {
            if (*error != NULL) return false;
            continue;
        }
        const FunctionRecord *f = &c->functions[function];
        for (uint32_t i = f->instruction_start; i < f->instruction_end; ++i) {
            if (!target_spend(work, error)) return false;
            const IrInstruction *ir = &c->instructions[i];
            if (ir->opcode != (store ? IR_ACCESS_STORE : IR_ACCESS_LOAD) ||
                ir->operands.access.category != category || ir->operands.access.root != root) continue;
            if (target_memory_dependency(c, ir->type, type, work, error)) return true;
            if (*error != NULL) return false;
        }
    }
    return false;
}
#endif
