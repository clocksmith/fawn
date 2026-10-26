/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_hlsl_numeric.h"
#include <string.h>

#define TRY(call) do { const DxilStatus status_ = (call); if (status_ != DXIL_OK) return status_; } while (0)

static bool arguments_valid(const DxilIrFunction *function, DxilIrRange args, uint32_t count) {
    if (function->expr_count > DXIL_IR_MAX_EXPRS || function->expr_arg_count > DXIL_IR_MAX_EXPR_ARGS ||
        args.len != count || args.start > function->expr_arg_count ||
        args.len > function->expr_arg_count - args.start) return false;
    for (uint32_t i = 0u; i < args.len; ++i)
        if (function->expr_args[args.start + i] >= function->expr_count) return false;
    return true;
}
static DxilStatus argument(DxilHlslEmitter *emitter, const DxilIrFunction *function,
    DxilIrRange args, uint32_t index) {
    return dxil_hlsl_emit_expr(emitter, function, function->expr_args[args.start + index]);
}
static DxilStatus dot_shape(const DxilIrModule *module, const DxilIrFunction *function,
    DxilIrTypeId result_ty, DxilIrRange args, bool *integer, bool *sign, uint32_t *lanes) {
    DxilIrType scalar;
    *integer = false;
    if (dxil_ir_type_get(module, result_ty, &scalar) != DXIL_OK) return DXIL_INVALID_IR;
    if (scalar.kind != DXIL_IR_TYPE_SCALAR ||
        (scalar.scalar != DXIL_IR_SCALAR_I32 && scalar.scalar != DXIL_IR_SCALAR_U32)) return DXIL_OK;
    *integer = true;
    if (!arguments_valid(function, args, 2u)) return DXIL_INVALID_IR;
    DxilIrType first, second;
    if (dxil_ir_type_get(module, function->exprs[function->expr_args[args.start]].ty, &first) != DXIL_OK ||
        dxil_ir_type_get(module, function->exprs[function->expr_args[args.start + 1u]].ty, &second) != DXIL_OK ||
        first.kind != DXIL_IR_TYPE_VECTOR || second.kind != DXIL_IR_TYPE_VECTOR ||
        first.len < 2u || first.len > 4u || second.len != first.len ||
        first.elem != result_ty || second.elem != result_ty) return DXIL_INVALID_IR;
    *sign = scalar.scalar == DXIL_IR_SCALAR_I32;
    *lanes = first.len;
    return DXIL_OK;
}
static DxilStatus dot_name(DxilHlslEmitter *emitter, bool sign, uint32_t lanes) {
    TRY(dxil_hlsl_write_text(emitter, sign ? "__doe_integer_dot_i" : "__doe_integer_dot_u"));
    return dxil_hlsl_write_u32(emitter, lanes);
}

DxilStatus dxil_hlsl_emit_numeric_builtin(DxilHlslEmitter *emitter,
    const DxilIrFunction *function, DxilIrTypeId result_ty, const char *name,
    DxilIrRange args, bool *handled) {
    if (handled == NULL) return DXIL_INVALID_IR;
    *handled = false;
    if (emitter == NULL || emitter->module == NULL || function == NULL || name == NULL)
        return DXIL_INVALID_IR;
    if (strcmp(name, "dot") == 0) {
        bool integer, sign = false;
        uint32_t lanes = 0u;
        TRY(dot_shape(emitter->module, function, result_ty, args, &integer, &sign, &lanes));
        if (!integer) return DXIL_OK;
        *handled = true;
        TRY(dot_name(emitter, sign, lanes));
        TRY(dxil_hlsl_write_text(emitter, "("));
        TRY(argument(emitter, function, args, 0u));
        TRY(dxil_hlsl_write_text(emitter, ", "));
        TRY(argument(emitter, function, args, 1u));
        return dxil_hlsl_write_text(emitter, ")");
    }
    const bool extract = strcmp(name, "extractBits") == 0;
    const bool insert = strcmp(name, "insertBits") == 0;
    const bool degrees = strcmp(name, "degrees") == 0;
    const bool radians = strcmp(name, "radians") == 0;
    if (!extract && !insert && !degrees && !radians) return DXIL_OK;
    *handled = true;
    if (!arguments_valid(function, args, extract ? 3u : insert ? 4u : 1u)) return DXIL_INVALID_IR;
    if (degrees || radians) {
        TRY(dxil_hlsl_write_text(emitter, "("));
        TRY(argument(emitter, function, args, 0u));
        return dxil_hlsl_write_text(emitter, degrees ? " * 57.29577951308232)" : " * 0.017453292519943295)");
    }
    /* Preserve the retained expression surface, including its bit-range edge
     * behavior; these are serializers, not a second WGSL numeric evaluator. */
    if (extract) {
        TRY(dxil_hlsl_write_text(emitter, "(("));
        TRY(argument(emitter, function, args, 0u));
        TRY(dxil_hlsl_write_text(emitter, " >> "));
        TRY(argument(emitter, function, args, 1u));
        TRY(dxil_hlsl_write_text(emitter, ") & ((1u << "));
        TRY(argument(emitter, function, args, 2u));
        return dxil_hlsl_write_text(emitter, ") - 1u))");
    }
    TRY(dxil_hlsl_write_text(emitter, "(("));
    TRY(argument(emitter, function, args, 0u));
    TRY(dxil_hlsl_write_text(emitter, " & ~(((1u << "));
    TRY(argument(emitter, function, args, 3u));
    TRY(dxil_hlsl_write_text(emitter, ") - 1u) << "));
    TRY(argument(emitter, function, args, 2u));
    TRY(dxil_hlsl_write_text(emitter, ")) | (("));
    TRY(argument(emitter, function, args, 1u));
    TRY(dxil_hlsl_write_text(emitter, " & ((1u << "));
    TRY(argument(emitter, function, args, 3u));
    TRY(dxil_hlsl_write_text(emitter, ") - 1u)) << "));
    TRY(argument(emitter, function, args, 2u));
    return dxil_hlsl_write_text(emitter, "))");
}

DxilStatus dxil_hlsl_emit_numeric_helpers(DxilHlslEmitter *emitter) {
    if (emitter == NULL || emitter->module == NULL) return DXIL_INVALID_IR;
    const DxilIrModule *module = emitter->module;
    if (module->function_count > DXIL_IR_MAX_FUNCTIONS) return DXIL_INVALID_IR;
    bool used[2][3] = {{false}};
    for (uint32_t f = 0u; f < module->function_count; ++f) {
        const DxilIrFunction *function = &module->functions[f];
        if (function->expr_count > DXIL_IR_MAX_EXPRS || function->expr_arg_count > DXIL_IR_MAX_EXPR_ARGS)
            return DXIL_INVALID_IR;
        for (uint32_t i = 0u; i < function->expr_count; ++i) {
            const DxilIrExpr *expr = &function->exprs[i];
            if (expr->kind != DXIL_IR_EXPR_CALL || expr->call_kind != DXIL_IR_CALL_BUILTIN ||
                expr->name == NULL || strcmp(expr->name, "dot") != 0) continue;
            bool integer, sign = false;
            uint32_t lanes = 0u;
            TRY(dot_shape(module, function, expr->ty, expr->args, &integer, &sign, &lanes));
            if (integer) used[sign ? 1u : 0u][lanes - 2u] = true;
        }
    }
    for (uint32_t sign = 0u; sign < 2u; ++sign) for (uint32_t width = 0u; width < 3u; ++width) {
        if (!used[sign][width]) continue;
        const char *scalar = sign != 0u ? "int" : "uint";
        uint32_t lanes = width + 2u;
        TRY(dxil_hlsl_write_text(emitter, scalar));
        TRY(dxil_hlsl_write_text(emitter, " "));
        TRY(dot_name(emitter, sign != 0u, lanes));
        TRY(dxil_hlsl_write_text(emitter, "("));
        TRY(dxil_hlsl_write_text(emitter, scalar));
        TRY(dxil_hlsl_write_u32(emitter, lanes));
        TRY(dxil_hlsl_write_text(emitter, " a, "));
        TRY(dxil_hlsl_write_text(emitter, scalar));
        TRY(dxil_hlsl_write_u32(emitter, lanes));
        TRY(dxil_hlsl_write_text(emitter, " b) { uint sum = "));
        for (uint32_t lane = 0u; lane < lanes; ++lane) {
            char component[2] = {"xyzw"[lane], '\0'};
            if (lane != 0u) TRY(dxil_hlsl_write_text(emitter, " + "));
            TRY(dxil_hlsl_write_text(emitter, "uint(a."));
            TRY(dxil_hlsl_write_text(emitter, component));
            TRY(dxil_hlsl_write_text(emitter, ") * uint(b."));
            TRY(dxil_hlsl_write_text(emitter, component));
            TRY(dxil_hlsl_write_text(emitter, ")"));
        }
        TRY(dxil_hlsl_write_text(emitter, sign != 0u ? "; return asint(sum); }\n" : "; return sum; }\n"));
    }
    return DXIL_OK;
}
