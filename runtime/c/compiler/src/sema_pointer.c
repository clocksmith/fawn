/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "sema.h"
#include <string.h>

enum { POINTER_READ = 1u, POINTER_WRITE = 2u };

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

static bool global_effect(Source *s, uint32_t function, SourceReference root, const unsigned char *effects, uint64_t *used, unsigned char *out) {
    CompilerWorkspace *c = s->compiler;
    *out = 0u;
    for (uint32_t f = 0u; f < c->function_count; ++f) {
        if (!charge(s,used,1u)) return false;
        c->functions[f].state.graph.color = 0u;
    }
    uint32_t current = function;
    c->functions[current].state.graph.parent = NO_ID;
    c->functions[current].state.graph.scan = c->functions[current].instruction_start;
    c->functions[current].state.graph.color = 1u;
    while (current != NO_ID) {
        FunctionRecord *fn = &c->functions[current];
        if (fn->state.graph.scan == fn->instruction_end) {
            fn->state.graph.color = 2u; current = fn->state.graph.parent; continue;
        }
        if (!charge(s,used,1u)) return false;
        const IrInstruction *ir = &c->instructions[fn->state.graph.scan++];
        if (ir->opcode == IR_CALL) {
            FunctionRecord *child = &c->functions[ir->operands.scalar.binding];
            for (uint32_t i=0u;i<child->formal_count;++i) {
                if (!charge(s,used,1u)) return false;
                if (!type_pointer(c,c->formals[child->formal_start+i].type.resolved.type)) continue;
                const uint32_t value = c->typed_operands[ir->operands.scalar.left+i];
                if (value == 0u || value > c->instruction_count) continue;
                const IrInstruction *address = &c->instructions[value-1u];
                if (address->opcode == IR_ADDRESS && address->operands.access.root == root.index &&
                    ((address->operands.access.category == ROOT_RESOURCE && root.kind == SOURCE_RESOURCE) ||
                    (address->operands.access.category == ROOT_WORKGROUP && root.kind == SOURCE_WORKGROUP)))
                    *out |= effects[child->formal_start+i];
            }
            if (child->state.graph.color != 0u) continue;
            child->state.graph.color = 1u; child->state.graph.parent = current;
            child->state.graph.scan = child->instruction_start;
            current = ir->operands.scalar.binding; continue;
        }
        if (ir->opcode != IR_ACCESS_LOAD && ir->opcode != IR_ACCESS_STORE && !atomic_opcode(ir->opcode)) continue;
        if (ir->operands.access.root != root.index ||
            ((ir->operands.access.category != ROOT_RESOURCE || root.kind != SOURCE_RESOURCE) &&
            (ir->operands.access.category != ROOT_WORKGROUP || root.kind != SOURCE_WORKGROUP))) continue;
        if (ir->opcode != IR_ACCESS_STORE && ir->opcode != IR_ATOMIC_STORE) *out |= POINTER_READ;
        if (ir->opcode != IR_ACCESS_LOAD && ir->opcode != IR_ATOMIC_LOAD) *out |= POINTER_WRITE;
    }
    return true;
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
        const bool pointer = type_pointer(c,c->formals[function->formal_start+i].type.resolved.type);
        if (check && pointer && (origin.kind == SOURCE_RESOURCE || origin.kind == SOURCE_WORKGROUP)) {
            unsigned char direct;
            if (!global_effect(s,callee,origin,effects,used,&direct)) return false;
            if ((effect != 0u && (direct & POINTER_WRITE) != 0u) ||
                (direct != 0u && (effect & POINTER_WRITE) != 0u))
                return source_error(s,DOE_COMPILER_SEMANTIC_ERROR,call->source_offset,
                    "pointer argument aliases a directly accessed module variable while either may be written");
        }
        if (check && pointer && (origin.kind == SOURCE_POINTER_LOCAL || origin.kind == SOURCE_POINTER_FORMAL ||
            origin.kind == SOURCE_RESOURCE || origin.kind == SOURCE_WORKGROUP)) {
            uint32_t other = c->arguments[argument].next;
            for (uint32_t j = i + 1u; j < call->right; ++j) {
                if (!charge(s, used, 1u)) return false;
                const SourceReference second = c->expression_references[c->arguments[other].expression];
                if (type_pointer(c,c->formals[function->formal_start+j].type.resolved.type) &&
                    origin.kind == second.kind && origin.index == second.index &&
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
            if (reference.kind == SOURCE_BARRIER || reference.kind == SOURCE_SUBGROUP || reference.kind == SOURCE_DERIVATIVE)
                function->flags |= FUNCTION_COLLECTIVE_USE;
        }
        for (uint32_t i = function->instruction_start; i < function->instruction_end; ++i) {
            if (!charge(s,&used,1u)) return false;
            const IrInstruction *ir = &c->instructions[i];
            if ((ir->opcode == IR_ACCESS_LOAD || ir->opcode == IR_ACCESS_STORE || atomic_opcode(ir->opcode)) &&
                ir->operands.access.category == ROOT_POINTER) {
                if (ir->opcode != IR_ACCESS_STORE && ir->opcode != IR_ATOMIC_STORE) effects[ir->operands.access.root] |= POINTER_READ;
                if (ir->opcode != IR_ACCESS_LOAD && ir->opcode != IR_ATOMIC_LOAD) effects[ir->operands.access.root] |= POINTER_WRITE;
                function->flags |= FUNCTION_POINTER_USE;
            }
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
        for (uint32_t i = function->expression_start; i < function->expression_end; ++i) {
            if (!charge(s, &used, 1u)) return false;
            const SourceReference reference = c->expression_references[i];
            if (reference.kind == SOURCE_FUNCTION &&
                !call_effects(s, &c->expressions[i], reference.index, effects, &used, &changed, true)) return false;
        }
    }
    return true;
}
