/* SPDX-License-Identifier: MIT */
#include "sema.h"

bool packed_signed(NumericBuiltinKind kind) {
    return kind == BUILTIN_PACK4X_I8 || kind == BUILTIN_PACK4X_I8_CLAMP ||
        kind == BUILTIN_UNPACK4X_I8 || kind == BUILTIN_DOT4_I8_PACKED;
}
bool packed_pack(NumericBuiltinKind kind) {
    return kind == BUILTIN_PACK4X_I8 || kind == BUILTIN_PACK4X_U8 ||
        kind == BUILTIN_PACK4X_I8_CLAMP || kind == BUILTIN_PACK4X_U8_CLAMP;
}
bool packed_dot(NumericBuiltinKind kind) {
    return kind == BUILTIN_DOT4_I8_PACKED || kind == BUILTIN_DOT4_U8_PACKED;
}
static int64_t byte_value(uint64_t bits, uint32_t lane, bool sign) {
    const uint32_t value = (uint32_t)(bits >> (lane * 8u)) & 255u;
    return sign && value >= 128u ? (int64_t)value - 256 : (int64_t)value;
}
bool packed_constant(Source *s, NumericBuiltinKind kind, const ConstantValue *args,
                      TypeId result_type, ConstantValue *out) {
    const bool sign = packed_signed(kind);
    uint32_t packed = 0u;
    int64_t dot = 0;
    ConstantValue lanes[4];
    for (uint32_t i = 0u; i < 4u; ++i) {
        if (packed_pack(kind)) {
            ConstantValue value;
            if (!constant_project(s, args[0], i, &value)) return false;
            uint32_t bits = (uint32_t)value.bits;
            if (kind == BUILTIN_PACK4X_U8_CLAMP && bits > 255u) bits = 255u;
            if (kind == BUILTIN_PACK4X_I8_CLAMP) {
                const int64_t integer = (bits & UINT32_C(0x80000000)) != 0u ?
                    (int64_t)bits - INT64_C(4294967296) : (int64_t)bits;
                if (integer < -128) bits = UINT32_C(0xffffff80);
                else if (integer > 127) bits = 127u;
            }
            packed |= (bits & 255u) << (i * 8u);
        } else if (packed_dot(kind)) {
            dot += byte_value(args[0].bits, i, sign) * byte_value(args[1].bits, i, sign);
        } else {
            lanes[i] = (ConstantValue){.type = sign ? IR_I32 : IR_U32,
                .bits = (uint32_t)byte_value(args[0].bits, i, sign)};
        }
    }
    if (!packed_pack(kind) && !packed_dot(kind))
        return constant_composite(s, result_type, lanes, 4u, false, out);
    *out = (ConstantValue){.type = result_type, .bits = packed_pack(kind) ? packed : (uint32_t)dot};
    return true;
}
static bool literal(Source *s, uint32_t offset, TypeId type, uint32_t bits, IrValue *out) {
    return sema_append(s, (IrInstruction){.opcode = IR_CONSTANT, .type = type,
        .source_offset = offset, .operands.scalar.literal = bits}, out);
}
static bool binary(Source *s, uint32_t offset, IrOpcode op, IrValue left, IrValue right, IrValue *out) {
    return sema_append(s, (IrInstruction){.opcode = op, .type = left.type, .source_offset = offset,
        .operands.scalar = {left.id, right.id, left.type, right.type}}, out);
}
static bool cast(Source *s, uint32_t offset, TypeId type, IrValue input, IrValue *out) {
    return sema_append(s, (IrInstruction){.opcode = IR_BITCAST, .type = type, .source_offset = offset,
        .operands.scalar = {.left = input.id, .binding = input.type}}, out);
}
static bool unpack_lane(Source *s, uint32_t offset, IrValue packed, uint32_t lane, bool sign, IrValue *out) {
    IrValue shift, value, mask;
    if (!literal(s, offset, IR_U32, sign ? 24u - lane * 8u : lane * 8u, &shift) ||
        !binary(s, offset, sign ? IR_SHIFT_LEFT : IR_SHIFT_RIGHT, packed, shift, &value)) return false;
    if (sign) {
        if (!cast(s, offset, IR_I32, value, &value) || !literal(s, offset, IR_U32, 24u, &shift)) return false;
        return binary(s, offset, IR_SHIFT_RIGHT, value, shift, out);
    }
    return literal(s, offset, IR_U32, 255u, &mask) && binary(s, offset, IR_BIT_AND, value, mask, out);
}
static bool pack_lane(Source *s, uint32_t offset, NumericBuiltinKind kind, IrValue input,
                       uint32_t lane, IrValue *out) {
    const TypeId type = packed_signed(kind) ? IR_I32 : IR_U32;
    IrValue value, lower, upper, mask, shift;
    if (!sema_append(s, (IrInstruction){.opcode = IR_SWIZZLE, .type = type, .source_offset = offset,
        .operands.scalar = {input.id, 1u, input.type, lane}}, &value)) return false;
    if (kind == BUILTIN_PACK4X_I8_CLAMP || kind == BUILTIN_PACK4X_U8_CLAMP) {
        if (!literal(s, offset, type, type == IR_I32 ? UINT32_C(0xffffff80) : 0u, &lower) ||
            !literal(s, offset, type, type == IR_I32 ? 127u : 255u, &upper) ||
            !sema_append(s, (IrInstruction){.opcode = IR_BUILTIN, .type = type, .source_offset = offset,
                .operands.scalar = {value.id, lower.id, upper.id, BUILTIN_CLAMP}}, &value)) return false;
    }
    if (type == IR_I32 && !cast(s, offset, IR_U32, value, &value)) return false;
    return literal(s, offset, IR_U32, 255u, &mask) &&
        binary(s, offset, IR_BIT_AND, value, mask, &value) &&
        literal(s, offset, IR_U32, lane * 8u, &shift) &&
        binary(s, offset, IR_SHIFT_LEFT, value, shift, out);
}
bool sema_packed_integer(Source *s, uint32_t offset, const NumericBuiltin *builtin,
                          ExpressionValue *args, bool emit, ExpressionValue *out) {
    for (uint32_t i = 0u; i < builtin->arity; ++i)
        if (!sema_ensure_value(s, &args[i], emit)) return false;
    if (!emit) return true;
    CompilerWorkspace *c = s->compiler;
    const NumericBuiltinKind kind = builtin->kind;
    const bool pack = packed_pack(kind), dot = packed_dot(kind);
    uint32_t first = 0u;
    if (!pack && !dot) {
        if (c->limits.operands - c->operand_count < 4u)
            return source_pool_error(s, DOE_COMPILER_POOL_IR_OPERANDS, (size_t)c->operand_count + 4u,
                offset, "packed unpack operand capacity exceeded");
        first = c->operand_count;
        c->operand_count += 4u;
    }
    IrValue sum = {0};
    for (uint32_t i = 0u; i < 4u; ++i) {
        IrValue value;
        if (pack) {
            if (!pack_lane(s, offset, kind, args[0].value, i, &value)) return false;
        } else {
            if (!unpack_lane(s, offset, args[0].value, i, packed_signed(kind), &value)) return false;
            if (dot) {
                IrValue other;
                if (!unpack_lane(s, offset, args[1].value, i, packed_signed(kind), &other) ||
                    !binary(s, offset, IR_MULTIPLY, value, other, &value)) return false;
            } else c->typed_operands[first + i] = value.id;
        }
        if (i == 0u) sum = value;
        else if ((pack || dot) && !binary(s, offset, pack ? IR_BIT_OR : IR_ADD, sum, value, &sum)) return false;
    }
    if (pack || dot) { out->value = sum; return true; }
    return sema_append(s, (IrInstruction){.opcode = IR_COMPOSITE, .type = out->constant.type,
        .source_offset = offset, .operands.composite = {first, 4u, 1u, 0u}}, &out->value);
}
