/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "sema.h"
#include <string.h>

enum { POINTER_READ = 1u, POINTER_WRITE = 2u };

bool sema_pointer_argument(Source *s, uint32_t expression, const ExpressionValue *value) {
    const CompilerWorkspace *c = s->compiler;
    const PointerValue pointer = value->pointer;
    const TypeId root = pointer.category == ROOT_POINTER ?
        c->types[c->formals[pointer.root].type.resolved.type - 7u].element :
        c->locals[pointer.root].type;
    /* A proper subobject cannot have its containing finite, acyclic type.
     * This identity check also applies during non-emitting overload visits. */
    if (c->types[pointer.type - 7u].element != root)
        return source_error(s, DOE_COMPILER_UNSUPPORTED, c->expressions[expression].source_offset,
            "subobject pointer arguments require unrestricted_pointer_parameters");
    return true;
}

static bool charge(Source *s, uint64_t *used, uint64_t count) {
    if (count > UNIFORMITY_WORK_LIMIT - *used)
        return source_error(s, DOE_COMPILER_UNSUPPORTED, s->length, "pointer effect analysis work limit exceeded");
    *used += count;
    return true;
}

static bool merge_effect(unsigned char *effects, uint32_t formal, unsigned char value) {
    const unsigned char previous = effects[formal];
    effects[formal] |= value;
    return previous != effects[formal];
}

static bool call_effects(Source *s, const Expression *call, uint32_t callee,
                         unsigned char *effects, uint64_t *used, bool *changed, bool check) {
    const CompilerWorkspace *c = s->compiler;
    const FunctionRecord *function = &c->functions[callee];
    uint32_t argument = call->left;
    for (uint32_t i = 0u; i < call->right; ++i) {
        if (!charge(s, used, 1u)) return false;
        const SourceReference origin = c->expression_references[c->arguments[argument].expression];
        const unsigned char effect = effects[function->formal_start + i];
        if (origin.kind == SOURCE_POINTER_FORMAL && merge_effect(effects, origin.index, effect)) *changed = true;
        if (check && (origin.kind == SOURCE_POINTER_LOCAL || origin.kind == SOURCE_POINTER_FORMAL)) {
            uint32_t other = c->arguments[argument].next;
            for (uint32_t j = i + 1u; j < call->right; ++j) {
                if (!charge(s, used, 1u)) return false;
                const SourceReference second = c->expression_references[c->arguments[other].expression];
                if (origin.kind == second.kind && origin.index == second.index &&
                    ((effect | effects[function->formal_start + j]) & POINTER_WRITE) != 0u)
                    return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, call->source_offset,
                        "pointer arguments alias while a corresponding parameter may be written");
                other = c->arguments[other].next;
            }
        }
        argument = c->arguments[argument].next;
    }
    return true;
}

bool sema_pointer_aliases(Source *s) {
    CompilerWorkspace *c = s->compiler;
    /* Complete semantic Walk has ended. Borrow the already charged analysis
     * union for one byte per formal; closures and uniformity reset it later.
     * Source references and typed IR remain live in distinct storage. */
    if (c->walk_count != 0u || c->formal_count > c->limits.tokens / 4u)
        return source_error(s, DOE_COMPILER_INPUT_ERROR, s->length, "pointer analysis phase boundary is invalid");
    unsigned char *effects = c->uniformity;
    uint64_t used = 0u;
    if (!charge(s, &used, c->formal_count)) return false;
    memset(effects, 0, c->formal_count);
    for (uint32_t f = 0u; f < c->function_count; ++f) {
        if (!charge(s, &used, 1u)) return false;
        FunctionRecord *function = &c->functions[f];
        for (uint32_t i = function->expression_start; i < function->expression_end; ++i) {
            if (!charge(s, &used, 1u)) return false;
            const SourceReference reference = c->expression_references[i];
            if (reference.kind == SOURCE_POINTER_READ) effects[reference.index] |= POINTER_READ;
            if (reference.kind == SOURCE_POINTER_LOCAL || reference.kind == SOURCE_POINTER_FORMAL ||
                reference.kind == SOURCE_POINTER_READ) function->flags |= FUNCTION_POINTER_USE;
            if (reference.kind == SOURCE_BARRIER || reference.kind == SOURCE_SUBGROUP)
                function->flags |= FUNCTION_COLLECTIVE_USE;
        }
        for (uint32_t i = function->statement_start; i < function->statement_end; ++i) {
            if (!charge(s, &used, 1u)) return false;
            const SourceReference reference = c->statement_references[i];
            if (reference.kind == SOURCE_POINTER_WRITE) {
                effects[reference.index] |= POINTER_WRITE;
                function->flags |= FUNCTION_POINTER_USE;
            }
        }
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (uint32_t f = 0u; f < c->function_count; ++f) {
            if (!charge(s, &used, 1u)) return false;
            FunctionRecord *function = &c->functions[f];
            for (uint32_t i = function->expression_start; i < function->expression_end; ++i) {
                if (!charge(s, &used, 1u)) return false;
                const SourceReference reference = c->expression_references[i];
                if (reference.kind != SOURCE_FUNCTION) continue;
                const uint32_t previous = function->flags;
                function->flags |= c->functions[reference.index].flags & (FUNCTION_POINTER_USE | FUNCTION_COLLECTIVE_USE);
                changed |= previous != function->flags;
                if (!call_effects(s, &c->expressions[i], reference.index, effects, &used, &changed, false)) return false;
            }
        }
    }
    for (uint32_t f = 0u; f < c->function_count; ++f) {
        if (!charge(s, &used, 1u)) return false;
        const FunctionRecord *function = &c->functions[f];
        if ((function->flags & (FUNCTION_POINTER_USE | FUNCTION_COLLECTIVE_USE)) ==
            (FUNCTION_POINTER_USE | FUNCTION_COLLECTIVE_USE))
            return source_error(s, DOE_COMPILER_UNSUPPORTED, function->name.offset,
                "pointer write-through and collective uniformity require additional dependency analysis");
        for (uint32_t i = function->expression_start; i < function->expression_end; ++i) {
            if (!charge(s, &used, 1u)) return false;
            const SourceReference reference = c->expression_references[i];
            if (reference.kind == SOURCE_FUNCTION &&
                !call_effects(s, &c->expressions[i], reference.index, effects, &used, &changed, true)) return false;
        }
    }
    return true;
}
