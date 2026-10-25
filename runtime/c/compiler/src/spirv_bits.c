/* SPDX-License-Identifier: MIT */
#include "spirv_internal.h"

bool spirv_integer_builtin(Emitter *e, const IrInstruction *ir, const NumericBuiltin *builtin) {
    const uint32_t result = e->values[ir->result], input = e->values[ir->operands.scalar.left];
    const uint32_t type = spirv_target_type(e, ir->type);
    if (builtin->kind == BUILTIN_REVERSE_BITS || builtin->kind == BUILTIN_COUNT_ONE_BITS) {
        EMIT(builtin->kind == BUILTIN_REVERSE_BITS ? OP_BIT_REVERSE : OP_BIT_COUNT, type, result, input);
        return true;
    }
    const TypeId leaf = type_leaf(e->program, ir->type);
    const uint32_t extended = builtin_extended_instruction(builtin, leaf);
    if (builtin->kind == BUILTIN_FIRST_LEADING_BIT || builtin->kind == BUILTIN_FIRST_TRAILING_BIT) {
        EMIT(OP_EXT_INST, type, result, e->math_import, extended, input);
        return true;
    }
    if (builtin->kind != BUILTIN_COUNT_LEADING_ZEROS && builtin->kind != BUILTIN_COUNT_TRAILING_ZEROS)
        return spirv_emit_error(e, ir->source_offset, "invalid integer builtin lowering");
    const bool leading = builtin->kind == BUILTIN_COUNT_LEADING_ZEROS;
    const uint32_t width = type_vector_width(e->program, ir->type), scalar = spirv_scalar_type(leaf);
    const uint32_t bound = spirv_constant_value(e, (ConstantValue){.type = leaf, .bits = leading ? 31u : 32u});
    uint32_t lanes[4];
    for (uint32_t i = 0u; i < width; ++i) {
        uint32_t value = input;
        if (width != 1u) {
            value = spirv_reserve_ids(e, 1u);
            EMIT(OP_COMPOSITE_EXTRACT, scalar, value, input, i);
        }
        const uint32_t bit = spirv_reserve_ids(e, 1u);
        lanes[i] = width == 1u ? result : spirv_reserve_ids(e, 1u);
        EMIT(OP_EXT_INST, scalar, bit, e->math_import, extended, value);
        if (leading) {
            /* FindUMsb(0) is all ones; 31 - all-ones is exactly 32. */
            EMIT(OP_ISUB, scalar, lanes[i], bound, bit);
        } else {
            const uint32_t zero = spirv_reserve_ids(e, 1u);
            EMIT(OP_IEQUAL, ID_BOOL, zero, value, spirv_zero_constant(e, leaf));
            EMIT(OP_SELECT, scalar, lanes[i], zero, bound, bit);
        }
    }
    if (width != 1u) {
        uint32_t *words = NULL;
        if (!spirv_instruction_space(e, OP_COMPOSITE_CONSTRUCT, (size_t)width + 2u, &words)) return false;
        words[0] = type;
        words[1] = result;
        for (uint32_t i = 0u; i < width; ++i) words[2u + i] = lanes[i];
    }
    return true;
}
