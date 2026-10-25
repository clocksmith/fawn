/* SPDX-License-Identifier: MIT */
#include "sema.h"

static bool literal(Source *source, uint32_t offset, TypeId type, uint32_t bits, IrValue *out) {
    const uint32_t width = type_vector_width(source->compiler, type);
    ConstantValue lanes[4];
    for (uint32_t i = 0u; i < width; ++i) lanes[i] = (ConstantValue){.type = IR_F32, .bits = bits};
    ExpressionValue value = {.known = true, .value.source_offset = offset};
    if (!sema_finish_lanes(source, type, lanes, width, &value.constant) ||
        !sema_ensure_value(source, &value, true)) return false;
    *out = value.value;
    return true;
}
static bool binary(Source *source, uint32_t offset, IrOpcode opcode,
                   IrValue left, IrValue right, IrValue *out) {
    return sema_append(source, (IrInstruction){.opcode = opcode, .type = left.type,
        .source_offset = offset, .operands.scalar = {left.id, right.id, left.type, right.type}}, out);
}

/* Lower through ordinary saved-value arithmetic. GLSL/MSL smoothstep intrinsics
 * do not guarantee WGSL's descending-edge case, so every target consumes this
 * same typed formula. No source expression or effect is evaluated a second time. */
bool sema_interpolation(Source *source, uint32_t offset, NumericBuiltinKind kind,
                        ExpressionValue *arguments, bool emit, ExpressionValue *out) {
    if (!emit) return true;
    const uint32_t count = kind == BUILTIN_SATURATE ? 1u : 3u;
    for (uint32_t i = 0u; i < count; ++i)
        if (!sema_ensure_value(source, &arguments[i], true)) return false;
    const TypeId type = out->constant.type;
    IrValue value = arguments[0].value, zero, one;
    if (!literal(source, offset, type, 0u, &zero) ||
        !literal(source, offset, type, UINT32_C(0x3f800000), &one)) return false;
    if (kind == BUILTIN_SMOOTHSTEP) {
        IrValue numerator, denominator;
        if (!binary(source, offset, IR_SUBTRACT, arguments[2].value, arguments[0].value, &numerator) ||
            !binary(source, offset, IR_SUBTRACT, arguments[1].value, arguments[0].value, &denominator) ||
            !binary(source, offset, IR_DIVIDE, numerator, denominator, &value)) return false;
    }
    if (!sema_append(source, (IrInstruction){.opcode = IR_BUILTIN, .type = type,
            .source_offset = offset, .operands.scalar = {value.id, zero.id, one.id, BUILTIN_CLAMP}},
            &value)) return false;
    if (kind == BUILTIN_SATURATE) { out->value = value; return true; }
    IrValue two, three, square, doubled, remainder;
    return literal(source, offset, type, UINT32_C(0x40000000), &two) &&
        literal(source, offset, type, UINT32_C(0x40400000), &three) &&
        binary(source, offset, IR_MULTIPLY, value, value, &square) &&
        binary(source, offset, IR_MULTIPLY, two, value, &doubled) &&
        binary(source, offset, IR_SUBTRACT, three, doubled, &remainder) &&
        binary(source, offset, IR_MULTIPLY, square, remainder, &out->value);
}
