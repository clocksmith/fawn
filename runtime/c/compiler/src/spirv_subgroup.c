/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "spirv_internal.h"

/* Ordinary SPIR-V 1.3 subgroup arithmetic, never a host reduction or a
 * workgroup-sized replacement. Execution scope is the constant Subgroup=3. */
bool spirv_subgroup_builtin(Emitter *e, const IrInstruction *ir,
                            const NumericBuiltin *builtin, TypeId leaf) {
    uint32_t opcode = 0u, operation = 0u;
    switch (builtin->kind) {
        case BUILTIN_SUBGROUP_ADD:
        case BUILTIN_SUBGROUP_EXCLUSIVE_ADD:
        case BUILTIN_SUBGROUP_INCLUSIVE_ADD:
            opcode = leaf == IR_F32 ? 350u : 349u;
            break;
        case BUILTIN_SUBGROUP_MUL:
        case BUILTIN_SUBGROUP_EXCLUSIVE_MUL:
        case BUILTIN_SUBGROUP_INCLUSIVE_MUL:
            opcode = leaf == IR_F32 ? 352u : 351u;
            break;
        case BUILTIN_SUBGROUP_MIN:
            opcode = leaf == IR_F32 ? 355u : leaf == IR_I32 ? 353u : 354u;
            break;
        case BUILTIN_SUBGROUP_MAX:
            opcode = leaf == IR_F32 ? 358u : leaf == IR_I32 ? 356u : 357u;
            break;
        case BUILTIN_SUBGROUP_AND: opcode = 359u; break;
        case BUILTIN_SUBGROUP_OR: opcode = 360u; break;
        case BUILTIN_SUBGROUP_XOR: opcode = 361u; break;
        case BUILTIN_ABS: case BUILTIN_SIGN: case BUILTIN_FLOOR: case BUILTIN_ROUND:
        case BUILTIN_TRUNC: case BUILTIN_FRACT: case BUILTIN_SIN: case BUILTIN_COS:
        case BUILTIN_TAN: case BUILTIN_ASIN: case BUILTIN_ACOS: case BUILTIN_ATAN:
        case BUILTIN_ATAN2: case BUILTIN_POW:
        case BUILTIN_SINH: case BUILTIN_COSH: case BUILTIN_ASINH: case BUILTIN_ACOSH:
        case BUILTIN_ATANH: case BUILTIN_LOG: case BUILTIN_LOG2: case BUILTIN_EXP2:
        case BUILTIN_INVERSE_SQRT: case BUILTIN_DEGREES: case BUILTIN_RADIANS:
        case BUILTIN_MIN: case BUILTIN_MAX: case BUILTIN_CLAMP: case BUILTIN_EXP:
        case BUILTIN_TANH: case BUILTIN_SQRT: case BUILTIN_DOT: case BUILTIN_LENGTH:
        case BUILTIN_TRANSPOSE: case BUILTIN_DETERMINANT: case BUILTIN_CEIL:
        case BUILTIN_DISTANCE: case BUILTIN_NORMALIZE: case BUILTIN_CROSS: case BUILTIN_REFLECT:
        case BUILTIN_FACE_FORWARD: case BUILTIN_REFRACT: case BUILTIN_ALL: case BUILTIN_ANY:
            return spirv_emit_error(e, ir->source_offset, "non-subgroup builtin reached collective lowering");
    }
    if (builtin->kind == BUILTIN_SUBGROUP_INCLUSIVE_ADD || builtin->kind == BUILTIN_SUBGROUP_INCLUSIVE_MUL)
        operation = 1u;
    else if (builtin->kind == BUILTIN_SUBGROUP_EXCLUSIVE_ADD || builtin->kind == BUILTIN_SUBGROUP_EXCLUSIVE_MUL)
        operation = 2u;
    EMIT(opcode, spirv_target_type(e, ir->type), e->values[ir->result], ID_THREE_U32,
         operation, e->values[ir->operands.scalar.left]);
    return true;
}
