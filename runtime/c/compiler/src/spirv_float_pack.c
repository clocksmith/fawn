/* SPDX-License-Identifier: MIT */
#include "spirv_internal.h"

static uint32_t floating(Emitter *e, uint32_t bits) {
    return spirv_constant_value(e, (ConstantValue){.type=IR_F32,.bits=bits});
}
static uint32_t scale_bits(NumericBuiltinKind kind) {
    const bool four = float_pack_width(kind) == 4u;
    return float_pack_signed(kind) ? (four ? 0x42fe0000u : 0x46fffe00u) :
        (four ? 0x437f0000u : 0x477fff00u);
}
bool spirv_float_pack_plan(Emitter *e, const NumericBuiltin *builtin) {
    if (builtin->kind == BUILTIN_QUANTIZE_F16 || float_pack_unpack(builtin->kind) ||
        builtin->kind == BUILTIN_PACK2X16_FLOAT) return true;
    if (e->math_import == 0u) e->math_import = spirv_reserve_ids(e, 1u);
    (void)floating(e, float_pack_signed(builtin->kind) ? 0xbf800000u : 0u);
    (void)floating(e, 0x3f800000u);
    (void)floating(e, 0x3f000000u);
    (void)floating(e, scale_bits(builtin->kind));
    const uint32_t width = float_pack_width(builtin->kind), bits = 32u / width;
    (void)spirv_unsigned_constant(e, (1u << bits) - 1u);
    for (uint32_t i = 0u; i < width; ++i) (void)spirv_unsigned_constant(e, i * bits);
    return true;
}
bool spirv_float_pack(Emitter *e, const IrInstruction *ir, const NumericBuiltin *builtin) {
    const uint32_t input = e->values[ir->operands.scalar.left], result = e->values[ir->result];
    if (builtin->kind == BUILTIN_QUANTIZE_F16) {
        EMIT(OP_QUANTIZE_TO_F16, spirv_target_type(e, ir->type), result, input);
        return true;
    }
    if (builtin->extended_instruction != 0u) {
        EMIT(OP_EXT_INST, spirv_target_type(e, ir->type), result, e->math_import,
            builtin->extended_instruction, input);
        return true;
    }
    const uint32_t width = float_pack_width(builtin->kind), bits = 32u / width;
    uint32_t partial = 0u;
    for (uint32_t i = 0u; i < width; ++i) {
        const uint32_t ids = spirv_reserve_ids(e, 9u);
        EMIT(OP_COMPOSITE_EXTRACT, ID_F32, ids, input, i);
        EMIT(OP_EXT_INST, ID_F32, ids+1u, e->math_import, 43u, ids,
            floating(e,float_pack_signed(builtin->kind) ? 0xbf800000u : 0u), floating(e,0x3f800000u));
        EMIT(OP_FMUL, ID_F32, ids+2u, ids+1u, floating(e,scale_bits(builtin->kind)));
        EMIT(OP_FADD, ID_F32, ids+3u, ids+2u, floating(e,0x3f000000u));
        EMIT(OP_EXT_INST, ID_F32, ids+4u, e->math_import, 8u, ids+3u);
        EMIT(OP_CONVERT_F_TO_S, ID_I32, ids+5u, ids+4u);
        EMIT(OP_BITCAST, ID_U32, ids+6u, ids+5u);
        EMIT(OP_BIT_AND, ID_U32, ids+7u, ids+6u, spirv_unsigned_constant(e,(1u<<bits)-1u));
        EMIT(OP_SHIFT_LEFT, ID_U32, ids+8u, ids+7u, spirv_unsigned_constant(e,i*bits));
        if (i == 0u) partial = ids+8u;
        else {
            const uint32_t next = i+1u == width ? result : spirv_reserve_ids(e,1u);
            EMIT(OP_BIT_OR, ID_U32, next, partial, ids+8u);
            partial = next;
        }
    }
    return true;
}
