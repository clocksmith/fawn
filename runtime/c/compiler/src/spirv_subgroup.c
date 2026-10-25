/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "spirv_internal.h"

/* Ordinary SPIR-V 1.3 subgroup arithmetic, never a host reduction or a
 * workgroup-sized replacement. Execution scope is the constant Subgroup=3. */
bool spirv_subgroup_builtin(Emitter *e, const IrInstruction *ir,
                            const NumericBuiltin *builtin, TypeId leaf) {
    if (builtin->kind >= BUILTIN_QUAD_BROADCAST && builtin->kind <= BUILTIN_QUAD_SWAP_Y) {
        const uint32_t operand = builtin->kind == BUILTIN_QUAD_BROADCAST ? e->values[ir->operands.scalar.right] :
            spirv_unsigned_constant(e, builtin->kind == BUILTIN_QUAD_SWAP_DIAGONAL ? 2u : builtin->kind == BUILTIN_QUAD_SWAP_X ? 0u : 1u);
        EMIT(builtin->kind == BUILTIN_QUAD_BROADCAST ? 365u : 366u, spirv_target_type(e,ir->type), e->values[ir->result],
            ID_THREE_U32,e->values[ir->operands.scalar.left],operand);
        return true;
    }
    if (builtin->family == BUILTIN_SUBGROUP_EXCHANGE) {
        static const uint32_t operations[] = {334u,335u,339u,333u,337u,338u,345u,346u,347u,348u};
        const uint32_t opcode = operations[(uint32_t)builtin->kind - (uint32_t)BUILTIN_SUBGROUP_ALL];
        const uint32_t type = spirv_target_type(e,ir->type), result = e->values[ir->result];
        if (builtin->arity == 0u) { EMIT(opcode,type,result,ID_THREE_U32); }
        else if (builtin->arity == 1u) { EMIT(opcode,type,result,ID_THREE_U32,e->values[ir->operands.scalar.left]); }
        else { EMIT(opcode,type,result,ID_THREE_U32,e->values[ir->operands.scalar.left],e->values[ir->operands.scalar.right]); }
        return true;
    }
    uint32_t opcode = 0u, operation = 0u;
    switch (builtin->kind) {
        case BUILTIN_SUBGROUP_ADD:
        case BUILTIN_SUBGROUP_EXCLUSIVE_ADD:
        case BUILTIN_SUBGROUP_INCLUSIVE_ADD:
            opcode = (leaf == IR_F32 || leaf == IR_F16) ? 350u : 349u;
            break;
        case BUILTIN_SUBGROUP_MUL:
        case BUILTIN_SUBGROUP_EXCLUSIVE_MUL:
        case BUILTIN_SUBGROUP_INCLUSIVE_MUL:
            opcode = (leaf == IR_F32 || leaf == IR_F16) ? 352u : 351u;
            break;
        case BUILTIN_SUBGROUP_MIN:
            opcode = (leaf == IR_F32 || leaf == IR_F16) ? 355u : leaf == IR_I32 ? 353u : 354u;
            break;
        case BUILTIN_SUBGROUP_MAX:
            opcode = (leaf == IR_F32 || leaf == IR_F16) ? 358u : leaf == IR_I32 ? 356u : 357u;
            break;
        case BUILTIN_SUBGROUP_AND: opcode = 359u; break;
        case BUILTIN_SUBGROUP_OR: opcode = 360u; break;
        case BUILTIN_SUBGROUP_XOR: opcode = 361u; break;
        case BUILTIN_FMA: case BUILTIN_STEP: case BUILTIN_SMOOTHSTEP: case BUILTIN_SATURATE:
        case BUILTIN_MIX: case BUILTIN_COUNT_ONE_BITS:
        case BUILTIN_REVERSE_BITS: case BUILTIN_COUNT_LEADING_ZEROS: case BUILTIN_COUNT_TRAILING_ZEROS:
        case BUILTIN_FIRST_LEADING_BIT: case BUILTIN_FIRST_TRAILING_BIT: case BUILTIN_UNPACK4X_U8:
        case BUILTIN_PACK4X8_SNORM:
        case BUILTIN_PACK4X8_UNORM:
        case BUILTIN_PACK2X16_SNORM:
        case BUILTIN_PACK2X16_UNORM:
        case BUILTIN_PACK2X16_FLOAT:
        case BUILTIN_UNPACK4X8_SNORM:
        case BUILTIN_UNPACK4X8_UNORM:
        case BUILTIN_UNPACK2X16_SNORM:
        case BUILTIN_UNPACK2X16_UNORM:
        case BUILTIN_UNPACK2X16_FLOAT:
        case BUILTIN_QUANTIZE_F16:
        case BUILTIN_DPDX:
        case BUILTIN_DPDY:
        case BUILTIN_FWIDTH:
        case BUILTIN_DPDX_FINE:
        case BUILTIN_DPDY_FINE:
        case BUILTIN_FWIDTH_FINE:
        case BUILTIN_DPDX_COARSE:
        case BUILTIN_DPDY_COARSE:
        case BUILTIN_FWIDTH_COARSE:
        case BUILTIN_SUBGROUP_ALL:
        case BUILTIN_SUBGROUP_ANY:
        case BUILTIN_SUBGROUP_BALLOT:
        case BUILTIN_SUBGROUP_ELECT:
        case BUILTIN_SUBGROUP_BROADCAST:
        case BUILTIN_SUBGROUP_BROADCAST_FIRST:
        case BUILTIN_SUBGROUP_SHUFFLE:
        case BUILTIN_SUBGROUP_SHUFFLE_XOR:
        case BUILTIN_SUBGROUP_SHUFFLE_UP:
        case BUILTIN_SUBGROUP_SHUFFLE_DOWN:
        case BUILTIN_QUAD_BROADCAST:
        case BUILTIN_QUAD_SWAP_DIAGONAL:
        case BUILTIN_QUAD_SWAP_X:
        case BUILTIN_QUAD_SWAP_Y:
        case BUILTIN_FREXP: case BUILTIN_MODF: case BUILTIN_LDEXP:
        case BUILTIN_EXTRACT_BITS: case BUILTIN_INSERT_BITS:
        case BUILTIN_PACK4X_I8: case BUILTIN_PACK4X_U8: case BUILTIN_PACK4X_I8_CLAMP:
        case BUILTIN_PACK4X_U8_CLAMP: case BUILTIN_UNPACK4X_I8:
        case BUILTIN_DOT4_I8_PACKED: case BUILTIN_DOT4_U8_PACKED:
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
