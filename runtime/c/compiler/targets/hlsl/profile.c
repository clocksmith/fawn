/* SPDX-License-Identifier: MIT
 * Positive compatibility certificate for a closed typed emission subset.
 * Unknown/new operations retain SM6; never infer compatibility from flags==0. */
#include "private.h"
#include "../../src/stage.h"

static bool ordinary_type_id(const CompilerWorkspace *module, TypeId type)
{
    if (type >= IR_PRIMITIVE_COUNT) return type - IR_PRIMITIVE_COUNT < module->type_count;
    switch ((IrType)type) {
    case IR_VOID: case IR_BOOL: case IR_I32: case IR_U32: case IR_F32: return true;
    case IR_ABSTRACT_INT: case IR_ABSTRACT_FLOAT: case IR_F16: case IR_PRIMITIVE_COUNT:
        return false;
    }
    return false;
}

static bool ordinary_type(const CompilerWorkspace *module, const TypeRecord *type)
{
    switch (type->kind) {
    case TYPE_VECTOR:
        return type->element == IR_BOOL || type->element == IR_I32 ||
            type->element == IR_U32 || type->element == IR_F32;
    case TYPE_ARRAY: case TYPE_RUNTIME_ARRAY: case TYPE_POINTER:
        return ordinary_type_id(module,type->element);
    case TYPE_STRUCT: return true;
    default: return false; /* Matrices, images, samplers, atomics and helper-result types. */
    }
}

static bool ordinary_instruction(const CompilerWorkspace *module, const IrInstruction *ir)
{
    if (!ordinary_type_id(module,ir->type)) return false;
    switch (ir->opcode) {
    case IR_INPUT:
        return ir->operands.scalar.binding < module->entry_input_count &&
            module->entry_inputs[ir->operands.scalar.binding].kind <= INPUT_LOCAL_INDEX;
    case IR_CONSTANT:
    case IR_ADD: case IR_SUBTRACT: case IR_MULTIPLY: case IR_COMPARE:
    case IR_IF: case IR_ELSE: case IR_END_IF: case IR_RETURN:
    case IR_NOT: case IR_SHORT_BEGIN: case IR_SHORT_END: case IR_LOCAL_LOAD:
    case IR_LOCAL_STORE: case IR_NEGATE:
    case IR_COMPLEMENT: case IR_BIT_AND:
    case IR_BIT_OR: case IR_BIT_XOR: case IR_SHIFT_LEFT: case IR_SHIFT_RIGHT:
    case IR_BITCAST: case IR_CALL: case IR_COMPOSITE:
    case IR_ACCESS_LOAD: case IR_ACCESS_STORE: case IR_VALUE_ACCESS: case IR_SWIZZLE:
    case IR_ARRAY_LENGTH: case IR_ADDRESS: case IR_SELECT: case IR_SWITCH_BEGIN:
    case IR_SWITCH_CASE: case IR_END_CASE: case IR_END_SWITCH: case IR_SWITCH_BREAK:
        return true;
    case IR_DIVIDE: case IR_REMAINDER: {
        /* FXC X3556 rejects reachable signed division under preserved WX.
         * Do not replace signed arithmetic or depend on optimizer folding. */
        const TypeId leaf = type_leaf(module,ir->type);
        return leaf == IR_U32 || leaf == IR_F32;
    }
    case IR_ROBUST_LOAD: case IR_UNIFORM_LOAD: case IR_MAX: case IR_ROBUST_STORE:
    /* Existing do-once loop bodies provoke FXC X3557 under strict WX/O3.
     * Retain DXC rather than suppress warnings or alter legacy target text. */
    case IR_LOOP_BEGIN: case IR_LOOP_TEST: case IR_LOOP_CONTINUING: case IR_END_LOOP:
    case IR_BREAK: case IR_CONTINUE: case IR_BREAK_IF:
    /* Inline numeric casts can provoke FXC X3577 on the required NaN guard.
     * Preserve that guard and strict flags by keeping conversion IR on DXC. */
    case IR_CONVERT:
    case IR_BUILTIN: case IR_WORKGROUP_BARRIER: case IR_HANDLE: case IR_IMAGE:
    case IR_STAGE_INPUT: case IR_DISCARD: case IR_ATOMIC_LOAD: case IR_ATOMIC_STORE:
    case IR_ATOMIC_ADD: case IR_ATOMIC_SUBTRACT: case IR_ATOMIC_MAX: case IR_ATOMIC_MIN:
    case IR_ATOMIC_AND: case IR_ATOMIC_OR: case IR_ATOMIC_XOR: case IR_ATOMIC_EXCHANGE:
    case IR_ATOMIC_COMPARE_EXCHANGE: case IR_DEFERRED_OVERRIDE:
        return false;
    }
    return false;
}

uint32_t hlsl_profile(HlslEmitter *emitter)
{
    const CompilerWorkspace *module = emitter->module;
    if (emitter->uses_native16) return 620u;
    const FunctionRecord *entry = &module->functions[module->entries[emitter->entry].function];
    if (stage_kind(entry) != DOE_COMPILER_STAGE_COMPUTE || emitter->uses_subgroups ||
        emitter->uses_wave_counter || emitter->uses_array_views || emitter->uses_raw_storage ||
        emitter->uses_draw_offsets || module->workgroup_count != 0u) return 600u;
    /* Charge a conservative linear upper bound to the existing composed work
     * budget. Insufficient certification budget preserves the SM6 baseline. */
    const uint64_t items = (uint64_t)emitter->closure.type_count+module->type_count+
        module->member_count+module->constant_count+module->edge_count+
        2u*(uint64_t)module->resource_count+module->function_count+
        module->instruction_count+module->formal_count+module->local_count;
    const char *work_error = NULL;
    if (!target_spend_count(&emitter->traversal_left,4u*items,&work_error)) return 600u;
    /* Reuse the existing completed-type scratch after emission. One linear
     * resource scan marks storage tails; no recursive type expansion or new map. */
    memset(emitter->closure.types,0,emitter->closure.type_count);
    for (uint32_t r = 0u; r < module->resource_count; ++r) {
        const ResourceRecord *resource = &module->resources[r];
        if (resource->tail_type == NO_ID) continue;
        if (resource->kind != DOE_COMPILER_STORAGE_BUFFER || resource->tail_type < IR_PRIMITIVE_COUNT ||
            resource->tail_type-IR_PRIMITIVE_COUNT >= module->type_count ||
            module->types[resource->tail_type-IR_PRIMITIVE_COUNT].kind != TYPE_RUNTIME_ARRAY) return 600u;
        emitter->closure.types[resource->tail_type] = 1u;
    }
    /* Types/constants/decomposition helpers can be printed module-wide even
     * when their function is unreachable. Inspect that whole declaration set. */
    for (uint32_t i = 0u; i < module->type_count; ++i) {
        const TypeRecord *type = &module->types[i];
        if (!ordinary_type(module,type)) return 600u;
        if (type->kind == TYPE_RUNTIME_ARRAY && !emitter->closure.types[i+IR_PRIMITIVE_COUNT]) return 600u;
    }
    for (uint32_t i = 0u; i < module->member_count; ++i)
        if (!ordinary_type_id(module,module->members[i].type)) return 600u;
    for (uint32_t i = 0u; i < module->constant_count; ++i) {
        const ConstantRecord *constant = &module->constants[i];
        if (constant->type < IR_PRIMITIVE_COUNT ||
            (type_flags(module,constant->type) & TYPE_ABSTRACT) != 0u) continue;
        if (constant->kind == CONSTANT_ZERO) continue;
        if (constant->kind != CONSTANT_REPEAT && constant->kind != CONSTANT_COMPOSITE) return 600u;
        const uint32_t count = constant->kind == CONSTANT_REPEAT ? 1u : constant->count;
        for (uint32_t j = 0u; j < count; ++j)
            if (!ordinary_type_id(module,module->constant_edges[constant->first+j].type)) return 600u;
    }
    for (uint32_t i = 0u; i < module->resource_count; ++i) {
        const ResourceRecord *resource = &module->resources[i];
        if (!ordinary_type_id(module,resource->type) ||
            (resource->kind != DOE_COMPILER_UNIFORM_BUFFER && resource->kind != DOE_COMPILER_STORAGE_BUFFER) ||
            (resource->access != DOE_COMPILER_READ && resource->access != DOE_COMPILER_READ_WRITE) ||
            (resource->kind == DOE_COMPILER_UNIFORM_BUFFER && resource->access != DOE_COMPILER_READ)) return 600u;
    }
    for (uint32_t f = 0u; f < module->function_count; ++f) {
        if (!target_function_used(&emitter->closure,f)) continue;
        const FunctionRecord *function = &module->functions[f];
        if (!ordinary_type_id(module,function->result_type)) return 600u;
        for (uint32_t i = 0u; i < function->formal_count; ++i)
            if (!ordinary_type_id(module,module->formals[function->formal_start+i].type.resolved.type)) return 600u;
        for (uint32_t i = 0u; i < function->local_count; ++i)
            if (!ordinary_type_id(module,module->locals[function->local_start+i].type)) return 600u;
        for (uint32_t i = function->instruction_start; i < function->instruction_end; ++i)
            if (!ordinary_instruction(module,&module->instructions[i])) return 600u;
    }
    return 501u;
}
