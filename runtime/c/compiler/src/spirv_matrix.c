/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "spirv_internal.h"

bool spirv_matrix_construct(Emitter *e, const IrInstruction *ir) {
    const CompilerWorkspace *c = e->program;
    const TypeRecord *matrix = &c->types[ir->type - 7u];
    const uint32_t rows = type_vector_width(c, matrix->element);
    if (ir->operands.composite.count != matrix->count * rows)
        return spirv_emit_error(e, ir->source_offset, "invalid matrix scalar constructor shape");
    uint32_t columns[4];
    for (uint32_t col = 0u; col < matrix->count; ++col) {
        columns[col] = spirv_reserve_ids(e, 1u);
        uint32_t *words;
        if (!spirv_instruction_space(e, OP_COMPOSITE_CONSTRUCT, (size_t)rows + 2u, &words)) return false;
        words[0] = spirv_target_type(e, matrix->element);
        words[1] = columns[col];
        for (uint32_t row = 0u; row < rows; ++row)
            words[row + 2u] = e->values[c->typed_operands[ir->operands.composite.first + col * rows + row]];
    }
    uint32_t *words;
    if (!spirv_instruction_space(e, OP_COMPOSITE_CONSTRUCT, (size_t)matrix->count + 2u, &words)) return false;
    words[0] = spirv_target_type(e, ir->type);
    words[1] = e->values[ir->result];
    for (uint32_t col = 0u; col < matrix->count; ++col) words[col + 2u] = columns[col];
    return !e->failed;
}
bool spirv_matrix_arithmetic(Emitter *e, const IrInstruction *ir) {
    const CompilerWorkspace *c = e->program;
    const TypeId left_type = ir->operands.scalar.binding, right_type = ir->operands.scalar.literal;
    uint32_t left = e->values[ir->operands.scalar.left], right = e->values[ir->operands.scalar.right];
    if (ir->opcode == IR_MULTIPLY) {
        uint32_t opcode;
        if (left_type < 7u) {
            const uint32_t temporary = left;
            left = right;
            right = temporary;
            opcode = OP_MATRIX_TIMES_SCALAR;
        } else if (right_type < 7u) opcode = OP_MATRIX_TIMES_SCALAR;
        else if (!type_matrix(c, left_type)) opcode = OP_VECTOR_TIMES_MATRIX;
        else if (!type_matrix(c, right_type)) opcode = OP_MATRIX_TIMES_VECTOR;
        else opcode = OP_MATRIX_TIMES_MATRIX;
        EMIT(opcode, spirv_target_type(e, ir->type), e->values[ir->result], left, right);
        return true;
    }
    const TypeRecord *matrix = &c->types[ir->type - 7u];
    uint32_t columns[4];
    for (uint32_t col = 0u; col < matrix->count; ++col) {
        const uint32_t x = spirv_reserve_ids(e, 1u), y = spirv_reserve_ids(e, 1u);
        columns[col] = spirv_reserve_ids(e, 1u);
        EMIT(OP_COMPOSITE_EXTRACT, spirv_target_type(e, matrix->element), x, left, col);
        EMIT(OP_COMPOSITE_EXTRACT, spirv_target_type(e, matrix->element), y, right, col);
        EMIT(ir->opcode == IR_ADD ? OP_FADD : OP_FSUB, spirv_target_type(e, matrix->element), columns[col], x, y);
    }
    uint32_t *words;
    if (!spirv_instruction_space(e, OP_COMPOSITE_CONSTRUCT, (size_t)matrix->count + 2u, &words)) return false;
    words[0] = spirv_target_type(e, ir->type);
    words[1] = e->values[ir->result];
    for (uint32_t col = 0u; col < matrix->count; ++col) words[col + 2u] = columns[col];
    return !e->failed;
}
/* Matrix layout decorations attach to a containing structure member, also when
 * that member contains arrays of matrices. Nested structures own their members. */
bool spirv_matrix_member(Emitter *e, uint32_t structure, uint32_t member, TypeId type) {
    const CompilerWorkspace *c = e->program;
    while (type >= 7u) {
        const TypeRecord *t = &c->types[type - 7u];
        if (t->kind == TYPE_MATRIX) {
            EMIT(OP_MEMBER_DECORATE, structure, member, 5u);
            EMIT(OP_MEMBER_DECORATE, structure, member, 7u, (uint32_t)t->state.layout.stride);
            return true;
        }
        if (t->kind != TYPE_ARRAY && t->kind != TYPE_RUNTIME_ARRAY) break;
        type = t->element;
    }
    return true;
}
