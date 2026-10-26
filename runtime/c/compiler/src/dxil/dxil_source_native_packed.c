/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_source_native_packed.h"

/* Target-view expansion of retained ir_packed_integer.Lowering. The canonical
 * compiler has already admitted operand types, ranges and constant semantics. */
typedef struct PackedProjection {
    DxilSourceBridge *bridge;
    DxilSourceFunction *function;
    uint32_t unsigned_type, signed_type, unsigned_vector, signed_vector;
} PackedProjection;

static uint32_t vector_type(DxilSourceBridge *bridge, uint32_t element) {
    uint32_t result = DXIL_IR_INVALID_ID;
    const DxilIrType type = {.kind = DXIL_IR_TYPE_VECTOR, .elem = element, .len = 4u};
    DxilStatus status = dxil_ir_intern_type(bridge->module, &type, &result);
    if (status != DXIL_OK) (void)dxil_source_native_fail(bridge, 0u, status,
        "packed native DXIL type projection capacity exceeded");
    return result;
}

static uint32_t append(PackedProjection *projection, DxilIrExpr expression) {
    expression.category = DXIL_IR_EXPR_VALUE;
    return dxil_source_native_push_expr(projection->bridge, projection->function, expression);
}

static uint32_t literal(PackedProjection *projection, uint32_t type, uint64_t value) {
    if (type == projection->signed_type && value > INT64_MAX) {
        uint32_t magnitude = append(projection, (DxilIrExpr){.ty = type,
            .kind = DXIL_IR_EXPR_INT_LIT, .uint_value = (~value) + 1u});
        return append(projection, (DxilIrExpr){.ty = type, .kind = DXIL_IR_EXPR_UNARY,
            .unary_op = DXIL_IR_UNARY_NEG, .operand = magnitude});
    }
    return append(projection, (DxilIrExpr){.ty = type,
        .kind = DXIL_IR_EXPR_INT_LIT, .uint_value = value});
}

static uint32_t arguments(PackedProjection *projection, uint32_t type,
    const char *name, const uint32_t *arguments, uint32_t count) {
    DxilIrFunction *view = projection->function->view;
    if (projection->bridge->status != DXIL_OK) return DXIL_IR_INVALID_ID;
    if (count > DXIL_IR_MAX_EXPR_ARGS - view->expr_arg_count) {
        (void)dxil_source_native_fail(projection->bridge, 0u, DXIL_OUTPUT_TOO_LARGE,
            "packed native DXIL argument projection capacity exceeded");
        return DXIL_IR_INVALID_ID;
    }
    DxilIrRange range = {view->expr_arg_count, count};
    for (uint32_t i = 0u; i < count; ++i) view->expr_args[view->expr_arg_count++] = arguments[i];
    return append(projection, (DxilIrExpr){.ty = type,
        .kind = name == NULL ? DXIL_IR_EXPR_CONSTRUCT : DXIL_IR_EXPR_CALL,
        .construct_ty = type,
        .name = name == NULL ? NULL : dxil_source_native_text(projection->bridge, name),
        .call_kind = DXIL_IR_CALL_BUILTIN,
        .args = range});
}

static uint32_t splat(PackedProjection *projection, uint32_t type, uint32_t value) {
    return arguments(projection, type, NULL, &value, 1u);
}

static uint32_t binary(PackedProjection *projection, uint32_t type,
    DxilIrBinaryOp operation, uint32_t left, uint32_t right) {
    return append(projection, (DxilIrExpr){.ty = type, .kind = DXIL_IR_EXPR_BINARY,
        .binary_op = operation, .lhs = left, .rhs = right});
}

static uint32_t offsets(PackedProjection *projection, bool reverse) {
    uint32_t values[4];
    for (uint32_t i = 0u; i < 4u; ++i) {
        values[i] = literal(projection, projection->unsigned_type,
            (reverse ? 3u - i : i) * 8u);
    }
    return arguments(projection, projection->unsigned_vector, NULL, values, 4u);
}

static uint32_t unpack(PackedProjection *projection, uint32_t argument, bool sign) {
    uint32_t value = splat(projection, projection->unsigned_vector, argument);
    uint32_t shifts = offsets(projection, sign);
    uint32_t shifted = binary(projection, projection->unsigned_vector,
        sign ? DXIL_IR_BINARY_SHIFT_LEFT : DXIL_IR_BINARY_SHIFT_RIGHT, value, shifts);
    if (sign) {
        uint32_t signed_value = arguments(projection, projection->signed_vector,
            "bitcast", &shifted, 1u);
        uint32_t shift = literal(projection, projection->unsigned_type, 24u);
        uint32_t splatted = splat(projection, projection->unsigned_vector, shift);
        return binary(projection, projection->signed_vector,
            DXIL_IR_BINARY_SHIFT_RIGHT, signed_value, splatted);
    }
    uint32_t mask = literal(projection, projection->unsigned_type, 255u);
    uint32_t splatted = splat(projection, projection->unsigned_vector, mask);
    return binary(projection, projection->unsigned_vector,
        DXIL_IR_BINARY_BIT_AND, shifted, splatted);
}

uint32_t dxil_source_native_packed(DxilSourceBridge *bridge,
    DxilSourceFunction *function, NumericBuiltinKind kind, const DxilIrExpr *call) {
    PackedProjection projection = {.bridge = bridge, .function = function,
        .unsigned_type = dxil_source_native_type(bridge, IR_U32),
        .signed_type = dxil_source_native_type(bridge, IR_I32)};
    if (bridge->status != DXIL_OK) return DXIL_IR_INVALID_ID;
    projection.unsigned_vector = vector_type(bridge, projection.unsigned_type);
    if (bridge->status != DXIL_OK) return DXIL_IR_INVALID_ID;
    projection.signed_vector = vector_type(bridge, projection.signed_type);
    if (bridge->status != DXIL_OK) return DXIL_IR_INVALID_ID;
    if (call->args.len != (packed_dot(kind) ? 2u : 1u) ||
        call->args.start > function->view->expr_arg_count ||
        call->args.len > function->view->expr_arg_count - call->args.start) {
        (void)dxil_source_native_fail(bridge, 0u, DXIL_INVALID_IR,
            "packed native DXIL argument range is invalid");
        return DXIL_IR_INVALID_ID;
    }
    const bool sign = packed_signed(kind);
    uint32_t value = function->view->expr_args[call->args.start];
    if (packed_pack(kind)) {
        uint32_t vector = sign ? projection.signed_vector : projection.unsigned_vector;
        if (kind == BUILTIN_PACK4X_I8_CLAMP || kind == BUILTIN_PACK4X_U8_CLAMP) {
            uint32_t lower = literal(&projection, sign ? projection.signed_type : projection.unsigned_type,
                sign ? UINT64_MAX - 127u : 0u);
            uint32_t upper = literal(&projection, sign ? projection.signed_type : projection.unsigned_type,
                sign ? 127u : 255u);
            uint32_t lower_vector = splat(&projection, vector, lower);
            uint32_t upper_vector = splat(&projection, vector, upper);
            uint32_t args[] = {value, lower_vector, upper_vector};
            value = arguments(&projection, vector, "clamp", args, 3u);
        }
        if (sign) value = arguments(&projection, projection.unsigned_vector, "bitcast", &value, 1u);
        uint32_t mask = literal(&projection, projection.unsigned_type, 255u);
        uint32_t mask_vector = splat(&projection, projection.unsigned_vector, mask);
        uint32_t masked = binary(&projection, projection.unsigned_vector,
            DXIL_IR_BINARY_BIT_AND, value, mask_vector);
        uint32_t shifts = offsets(&projection, false);
        uint32_t shifted = binary(&projection, projection.unsigned_vector,
            DXIL_IR_BINARY_SHIFT_LEFT, masked, shifts);
        uint32_t one = literal(&projection, projection.unsigned_type, 1u);
        uint32_t ones = splat(&projection, projection.unsigned_vector, one);
        uint32_t args[] = {shifted, ones};
        return arguments(&projection, call->ty, "dot", args, 2u);
    }
    uint32_t left = unpack(&projection, value, sign);
    if (!packed_dot(kind)) return left;
    uint32_t right = unpack(&projection, function->view->expr_args[call->args.start + 1u], sign);
    uint32_t args[] = {left, right};
    return arguments(&projection, call->ty, "dot", args, 2u);
}
