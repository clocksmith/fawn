/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "spirv_internal.h"

static uint32_t storage_class(const StageInterface *leaf) {
    return leaf->direction == DOE_COMPILER_INTERFACE_INPUT ? 1u : 3u;
}
static uint32_t builtin_decoration(const StageInterface *leaf) {
    switch (leaf->builtin) {
        case DOE_COMPILER_BUILTIN_POSITION:
            return leaf->direction == DOE_COMPILER_INTERFACE_INPUT ? 15u : 0u;
        case DOE_COMPILER_BUILTIN_VERTEX_INDEX: return 42u;
        case DOE_COMPILER_BUILTIN_INSTANCE_INDEX: return 43u;
        case DOE_COMPILER_BUILTIN_FRONT_FACING: return 17u;
        case DOE_COMPILER_BUILTIN_FRAG_DEPTH: return 22u;
        case DOE_COMPILER_BUILTIN_SAMPLE_INDEX: return 18u;
        case DOE_COMPILER_BUILTIN_SAMPLE_MASK: return 20u;
        default: return NO_ID;
    }
}
uint64_t spirv_stage_id_budget(const Source *source) {
    if (source->stages == NULL) return 0u;
    const uint64_t interfaces = source->stages->interface_capacity;
    const uint64_t instructions = source->compiler->limits.instructions;
    /* At most two temporary IDs per output leaf on each return, plus input
     * loads, sample-mask pointers, variables and one composite per formal. */
    if (interfaces != 0u && instructions > UINT32_MAX / interfaces / 2u) return UINT32_MAX;
    return 2u * interfaces * instructions + 4u * interfaces;
}
bool spirv_stage_plan(Emitter *e) {
    if (e->source->stages == NULL) return true;
    StageWorkspace *stages = e->source->stages;
    for (uint32_t i = 0u; i < stages->interface_count; ++i) {
        StageInterface *leaf = &stages->interfaces[i];
        leaf->target = spirv_reserve_ids(e, 1u);
        if (spirv_pointer_type(e, leaf->target_type, storage_class(leaf)) == 0u) return false;
        if (leaf->builtin == DOE_COMPILER_BUILTIN_SAMPLE_MASK) {
            if (spirv_pointer_type(e, IR_U32, storage_class(leaf)) == 0u || spirv_unsigned_constant(e, 0u) == 0u) return false;
        }
        if (leaf->builtin == DOE_COMPILER_BUILTIN_SAMPLE_INDEX || leaf->sampling == DOE_COMPILER_SAMPLING_SAMPLE)
            e->sample_rate_shading = true;
    }
    return !e->failed;
}
bool spirv_stage_annotations(Emitter *e) {
    if (e->source->stages == NULL) return true;
    const StageWorkspace *stages = e->source->stages;
    for (uint32_t i = 0u; i < stages->interface_count; ++i) {
        const StageInterface *leaf = &stages->interfaces[i];
        if (leaf->builtin != 0u) {
            EMIT(OP_DECORATE, leaf->target, 11u, builtin_decoration(leaf));
        } else { EMIT(OP_DECORATE, leaf->target, 30u, leaf->location); }
        if (leaf->interpolation == DOE_COMPILER_INTERPOLATION_FLAT ||
            (leaf->direction == DOE_COMPILER_INTERFACE_INPUT &&
             (leaf->builtin == DOE_COMPILER_BUILTIN_SAMPLE_INDEX || leaf->builtin == DOE_COMPILER_BUILTIN_SAMPLE_MASK ||
              leaf->builtin == DOE_COMPILER_BUILTIN_FRONT_FACING))) { EMIT(OP_DECORATE, leaf->target, 14u); }
        if (leaf->interpolation == DOE_COMPILER_INTERPOLATION_LINEAR) { EMIT(OP_DECORATE, leaf->target, 13u); }
        if (leaf->sampling == DOE_COMPILER_SAMPLING_CENTROID) { EMIT(OP_DECORATE, leaf->target, 16u); }
        if (leaf->sampling == DOE_COMPILER_SAMPLING_SAMPLE) { EMIT(OP_DECORATE, leaf->target, 17u); }
        if ((leaf->flags & DOE_COMPILER_INTERFACE_INVARIANT) != 0u) { EMIT(OP_DECORATE, leaf->target, 18u); }
    }
    return true;
}
bool spirv_stage_variables(Emitter *e) {
    if (e->source->stages == NULL) return true;
    const StageWorkspace *stages = e->source->stages;
    for (uint32_t i = 0u; i < stages->interface_count; ++i) {
        const StageInterface *leaf = &stages->interfaces[i];
        EMIT(OP_VARIABLE, spirv_pointer_type(e, leaf->target_type, storage_class(leaf)), leaf->target, storage_class(leaf));
    }
    return true;
}
bool spirv_stage_modes(Emitter *e, uint32_t index) {
    const CompilerWorkspace *c = e->program;
    const FunctionRecord *function = &c->functions[c->entries[index].function];
    if ((function->flags & FUNCTION_FRAGMENT) == 0u) return true;
    EMIT(OP_EXECUTION_MODE, function->state.target.function, 7u); /* OriginUpperLeft. */
    const StageEntry *entry = &e->source->stages->entries[index];
    for (uint32_t i = entry->interface_first; i < entry->interface_first + entry->interface_count; ++i) {
        const StageInterface *leaf = &e->source->stages->interfaces[i];
        if (leaf->direction == DOE_COMPILER_INTERFACE_OUTPUT && leaf->builtin == DOE_COMPILER_BUILTIN_FRAG_DEPTH) {
            EMIT(OP_EXECUTION_MODE, function->state.target.function, 12u); /* DepthReplacing. */
        }
    }
    return true;
}
static bool load_leaf(Emitter *e, const StageInterface *leaf, uint32_t result) {
    uint32_t pointer = leaf->target;
    if (leaf->builtin == DOE_COMPILER_BUILTIN_SAMPLE_MASK) {
        pointer = spirv_reserve_ids(e, 1u);
        EMIT(OP_ACCESS_CHAIN, spirv_pointer_type(e, IR_U32, 1u), pointer, leaf->target, spirv_unsigned_constant(e, 0u));
    }
    EMIT(OP_LOAD, spirv_target_type(e, leaf->type), result, pointer);
    return true;
}
bool spirv_stage_inputs(Emitter *e, uint32_t index) {
    const CompilerWorkspace *c = e->program;
    const StageWorkspace *stages = e->source->stages;
    const StageEntry *entry = &stages->entries[index];
    uint32_t cursor = entry->interface_first;
    for (uint32_t p = 0u; p < entry->formal_count; ++p) {
        const uint32_t formal = entry->formal_first + p;
        const TypeId type = c->formals[formal].type.resolved.type;
        const bool structure = type >= IR_PRIMITIVE_COUNT && c->types[type - IR_PRIMITIVE_COUNT].kind == TYPE_STRUCT;
        const uint32_t count = structure ? c->types[type - IR_PRIMITIVE_COUNT].count : 1u;
        const uint32_t first = spirv_reserve_ids(e, count);
        for (uint32_t j = 0u; j < count; ++j)
            if (!load_leaf(e, &stages->interfaces[cursor++], first + j)) return false;
        uint32_t result = first;
        if (structure) {
            result = spirv_reserve_ids(e, 1u);
            uint32_t *words;
            if (!spirv_instruction_space(e, OP_COMPOSITE_CONSTRUCT, (size_t)count + 2u, &words)) return false;
            words[0] = spirv_target_type(e, type);
            words[1] = result;
            for (uint32_t j = 0u; j < count; ++j) words[j + 2u] = first + j;
        }
        e->values[c->limits.instructions + 1u + formal] = result;
    }
    return !e->failed;
}
bool spirv_stage_return(Emitter *e, const IrInstruction *instruction) {
    const StageWorkspace *stages = e->source->stages;
    const FunctionRecord *function = &e->program->functions[e->entry->function];
    const StageEntry *entry = &stages->entries[function->entry_index];
    for (uint32_t i = entry->interface_first; i < entry->interface_first + entry->interface_count; ++i) {
        const StageInterface *leaf = &stages->interfaces[i];
        if (leaf->direction != DOE_COMPILER_INTERFACE_OUTPUT) continue;
        uint32_t value = e->values[instruction->operands.scalar.left];
        if (leaf->member != NO_ID) {
            value = spirv_reserve_ids(e, 1u);
            EMIT(OP_COMPOSITE_EXTRACT, spirv_target_type(e, leaf->type), value,
                e->values[instruction->operands.scalar.left], leaf->member);
        }
        uint32_t pointer = leaf->target;
        if (leaf->builtin == DOE_COMPILER_BUILTIN_SAMPLE_MASK) {
            pointer = spirv_reserve_ids(e, 1u);
            EMIT(OP_ACCESS_CHAIN, spirv_pointer_type(e, IR_U32, 3u), pointer, leaf->target, spirv_unsigned_constant(e, 0u));
        }
        EMIT(OP_STORE, pointer, value);
    }
    return spirv_instruction(e, OP_RETURN, NULL, 0u);
}
