/* SPDX-License-Identifier: MIT */
#include "tsir_source_internal.h"
#include <stdlib.h>
#include <string.h>

/* Projection of retained ir_packed_integer.Lowering. Numeric admission and
 * arithmetic remain owned by the C compiler, not by this TSIR view. */
typedef struct PackedProjection {
    TsirSourceBridge *bridge;
    TsirSourceFunction *function;
    uint32_t unsigned_vector, signed_vector;
} PackedProjection;

static uint32_t vector_type(TsirSourceBridge *b, uint32_t element) {
    for (size_t i = 0; i < b->module.type_count; ++i)
        if (b->types[i].kind == TSIR_IR_TYPE_VECTOR &&
            b->types[i].elem == element && b->types[i].len == 4u) return (uint32_t)i;
    size_t count = b->module.type_count;
    if (count >= UINT32_MAX || count >= SIZE_MAX / sizeof(*b->types)) {
        b->status = TSIR_ERR_OVERFLOW;
        return TSIR_IR_NONE;
    }
    TsirIrType *types = realloc(b->types, (count + 1u) * sizeof(*types));
    if (!types) { b->status = TSIR_ERR_NOMEM; return TSIR_IR_NONE; }
    b->types = types;
    b->module.types = types;
    types[count] = (TsirIrType){.kind = TSIR_IR_TYPE_VECTOR, .elem = element, .len = 4u};
    b->module.type_count = count + 1u;
    return (uint32_t)count;
}
static uint32_t append(PackedProjection *p, TsirIrExpr e) {
    if (p->bridge->status != TSIR_OK) return TSIR_IR_NONE;
    return tsir_source_push_expr(p->bridge, p->function, e);
}
static uint32_t literal(PackedProjection *p, uint32_t ty, uint64_t value) {
    if (ty == IR_I32 && value > INT64_MAX) {
        uint32_t magnitude = append(p, (TsirIrExpr){.ty = ty, .kind = TSIR_IR_EXPR_INT_LIT,
            .int_value = (~value) + 1u});
        return append(p, (TsirIrExpr){.ty = ty, .kind = TSIR_IR_EXPR_UNARY,
            .unary_op = TSIR_IR_UN_NEG, .unary_operand = magnitude});
    }
    return append(p, (TsirIrExpr){.ty = ty, .kind = TSIR_IR_EXPR_INT_LIT, .int_value = value});
}
static uint32_t arguments(PackedProjection *p, uint32_t ty, const char *name,
    const uint32_t *args, size_t count) {
    TsirSourceFunction *f = p->function;
    if (p->bridge->status != TSIR_OK) return TSIR_IR_NONE;
    if (count > f->expr_cap - f->view.expr_arg_count) {
        (void)tsir_source_fail(p->bridge, 0u, "packed TSIR argument capacity exceeded");
        return TSIR_IR_NONE;
    }
    uint32_t start = (uint32_t)f->view.expr_arg_count;
    for (size_t i = 0; i < count; ++i) f->args[f->view.expr_arg_count++] = args[i];
    return append(p, (TsirIrExpr){.ty = ty,
        .kind = name ? TSIR_IR_EXPR_CALL : TSIR_IR_EXPR_CONSTRUCT,
        .call_name = name, .call_is_builtin = name != NULL,
        .args_start = start, .args_len = (uint32_t)count});
}
static uint32_t splat(PackedProjection *p, uint32_t ty, uint32_t value) {
    return arguments(p, ty, NULL, &value, 1u);
}
static uint32_t binary(PackedProjection *p, uint32_t ty, TsirIrBinaryOp op,
    uint32_t left, uint32_t right) {
    return append(p, (TsirIrExpr){.ty = ty, .kind = TSIR_IR_EXPR_BINARY,
        .binary_op = op, .binary_lhs = left, .binary_rhs = right});
}
static uint32_t offsets(PackedProjection *p, bool reverse) {
    uint32_t values[4];
    for (uint32_t i = 0; i < 4u; ++i)
        values[i] = literal(p, IR_U32, (reverse ? 3u - i : i) * 8u);
    return arguments(p, p->unsigned_vector, NULL, values, 4u);
}
static uint32_t unpack(PackedProjection *p, uint32_t arg, bool sign) {
    uint32_t value = splat(p, p->unsigned_vector, arg);
    uint32_t shifts = offsets(p, sign);
    uint32_t shifted = binary(p, p->unsigned_vector,
        sign ? TSIR_IR_BIN_SHIFT_LEFT : TSIR_IR_BIN_SHIFT_RIGHT, value, shifts);
    if (sign) {
        uint32_t signed_value = arguments(p, p->signed_vector, "bitcast", &shifted, 1u);
        uint32_t shift = literal(p, IR_U32, 24u);
        uint32_t splatted_shift = splat(p, p->unsigned_vector, shift);
        return binary(p, p->signed_vector, TSIR_IR_BIN_SHIFT_RIGHT, signed_value, splatted_shift);
    }
    uint32_t mask = literal(p, IR_U32, 255u);
    uint32_t splatted_mask = splat(p, p->unsigned_vector, mask);
    return binary(p, p->unsigned_vector, TSIR_IR_BIN_BIT_AND, shifted, splatted_mask);
}
uint32_t tsir_source_packed(TsirSourceBridge *b, TsirSourceFunction *f,
    NumericBuiltinKind kind, const TsirIrExpr *call) {
    PackedProjection p = {.bridge = b, .function = f};
    p.unsigned_vector = vector_type(b, IR_U32);
    if (b->status != TSIR_OK) return TSIR_IR_NONE;
    p.signed_vector = vector_type(b, IR_I32);
    if (b->status != TSIR_OK) return TSIR_IR_NONE;
    const bool sign = packed_signed(kind);
    uint32_t value = f->args[call->args_start];
    if (packed_pack(kind)) {
        const uint32_t vector = sign ? p.signed_vector : p.unsigned_vector;
        if (kind == BUILTIN_PACK4X_I8_CLAMP || kind == BUILTIN_PACK4X_U8_CLAMP) {
            uint32_t lower = literal(&p, sign ? IR_I32 : IR_U32, sign ? UINT64_MAX - 127u : 0u);
            uint32_t upper = literal(&p, sign ? IR_I32 : IR_U32, sign ? 127u : 255u);
            uint32_t lower_vector = splat(&p, vector, lower);
            uint32_t upper_vector = splat(&p, vector, upper);
            uint32_t args[] = {value, lower_vector, upper_vector};
            value = arguments(&p, vector, "clamp", args, 3u);
        }
        if (sign) value = arguments(&p, p.unsigned_vector, "bitcast", &value, 1u);
        uint32_t mask = literal(&p, IR_U32, 255u);
        uint32_t mask_vector = splat(&p, p.unsigned_vector, mask);
        uint32_t masked = binary(&p, p.unsigned_vector, TSIR_IR_BIN_BIT_AND, value, mask_vector);
        uint32_t shifts = offsets(&p, false);
        uint32_t shifted = binary(&p, p.unsigned_vector, TSIR_IR_BIN_SHIFT_LEFT, masked, shifts);
        uint32_t one = literal(&p, IR_U32, 1u);
        uint32_t ones = splat(&p, p.unsigned_vector, one);
        uint32_t args[] = {shifted, ones};
        return arguments(&p, call->ty, "dot", args, 2u);
    }
    uint32_t left = unpack(&p, value, sign);
    if (!packed_dot(kind)) return left;
    uint32_t right = unpack(&p, f->args[call->args_start + 1u], sign);
    uint32_t args[] = {left, right};
    return arguments(&p, call->ty, "dot", args, 2u);
}
