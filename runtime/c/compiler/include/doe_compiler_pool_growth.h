/* SPDX-License-Identifier: MIT
 * Caller-owned workspace growth. The compiler's typed pool report is the only
 * retry authority; language errors and allocation failures are terminal. */
#ifndef DOE_COMPILER_POOL_GROWTH_H
#define DOE_COMPILER_POOL_GROWTH_H
#include "doe_compiler_stages.h"
#include <stdbool.h>

static inline bool doe_compiler_grow_pool_capacity(size_t *capacity, size_t minimum) {
    if (minimum <= *capacity || minimum > UINT32_MAX - 1u) return false;
    size_t next = *capacity <= (UINT32_MAX - 1u) / 2u ? *capacity * 2u : UINT32_MAX - 1u;
    if (next < minimum) next = minimum;
    *capacity = next;
    return true;
}
static inline bool doe_compiler_grow_pool_depth(uint32_t *capacity, size_t minimum, uint32_t maximum) {
    if (minimum <= *capacity || minimum > maximum) return false;
    *capacity = maximum;
    return true;
}
static inline bool doe_compiler_grow_stage_pool(DoeCompilerStageCapacities *stages,
    DoeCompilerStatus status, uint32_t pool, size_t minimum) {
    if (status != DOE_COMPILER_INPUT_ERROR && status != DOE_COMPILER_OUTPUT_TOO_SMALL) return false;
    DoeCompilerTypedCapacities *c = &stages->module.typed;
    size_t *capacity = NULL;
    switch (pool) {
        case DOE_COMPILER_POOL_TOKENS: capacity = &c->tokens; break;
        case DOE_COMPILER_POOL_STATEMENTS: capacity = &c->statements; break;
        case DOE_COMPILER_POOL_EXPRESSIONS: capacity = &c->expressions; break;
        case DOE_COMPILER_POOL_TYPES: capacity = &c->types; break;
        case DOE_COMPILER_POOL_MEMBERS: capacity = &c->members; break;
        case DOE_COMPILER_POOL_RESOURCES: capacity = &c->resources; break;
        case DOE_COMPILER_POOL_ACCESS_STEPS: capacity = &c->access_steps; break;
        case DOE_COMPILER_POOL_CONSTANTS: capacity = &c->constants; break;
        case DOE_COMPILER_POOL_CONSTANT_EDGES: capacity = &c->constant_edges; break;
        case DOE_COMPILER_POOL_IR: capacity = &c->ir_instructions; break;
        case DOE_COMPILER_POOL_IR_OPERANDS: capacity = &c->ir_operands; break;
        case DOE_COMPILER_POOL_TARGET_DECLARATIONS: capacity = &c->target_declarations; break;
        case DOE_COMPILER_POOL_TRAVERSAL: capacity = &c->traversal_items; break;
        case DOE_COMPILER_POOL_WORDS: capacity = &c->words; break;
        case DOE_COMPILER_POOL_ENTRIES: capacity = &stages->module.entries; break;
        case DOE_COMPILER_POOL_ENTRY_INPUTS: capacity = &stages->module.entry_inputs; break;
        case DOE_COMPILER_POOL_ENTRY_REFERENCES: capacity = &stages->module.entry_references; break;
        case DOE_COMPILER_POOL_INTERFACE_ATTRIBUTES: capacity = &stages->interface_attributes; break;
        case DOE_COMPILER_POOL_STAGE_INTERFACES: capacity = &stages->stage_interfaces; break;
        case DOE_COMPILER_POOL_TYPE_SYNTAX:
            /* The canonical workspace derives generic syntax slots as tokens/2. */
            if (minimum > (UINT32_MAX - 1u) / 2u) return false;
            minimum *= 2u;
            capacity = &c->tokens;
            break;
        case DOE_COMPILER_POOL_EXPRESSION_DEPTH:
            return doe_compiler_grow_pool_depth(&c->expression_depth, minimum, DOE_COMPILER_TYPED_MAX_EXPRESSION_DEPTH);
        case DOE_COMPILER_POOL_CONTROL_DEPTH:
            return doe_compiler_grow_pool_depth(&c->control_depth, minimum, DOE_COMPILER_TYPED_MAX_CONTROL_DEPTH);
        case DOE_COMPILER_POOL_TYPE_SYNTAX_DEPTH:
            return doe_compiler_grow_pool_depth(&c->type_syntax_depth, minimum, DOE_COMPILER_TYPED_MAX_TYPE_SYNTAX_DEPTH);
        default: return false; /* Source bounds and unknown reports never authorize growth. */
    }
    return doe_compiler_grow_pool_capacity(capacity, minimum);
}
#endif
