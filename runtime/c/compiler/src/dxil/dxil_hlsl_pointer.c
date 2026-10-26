/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_hlsl_internal.h"
#include <string.h>

/* Caller-built IR has no canonical source workspace. Reuse its read-only
 * alias/literal query; conservatively snapshot any other index expression.
 * Extra snapshots of constant arithmetic are harmless and avoid a second
 * integer evaluation engine. All alias walks are explicitly bounded. */
static bool contains_index(const DxilIrFunction *function, uint32_t expression, uint32_t target) {
    for (uint32_t n = 0u; n < function->expr_count; ++n) {
        if (expression >= function->expr_count) return false;
        if (expression == target) return true;
        const DxilIrExpr *node = &function->exprs[expression];
        switch (node->kind) {
            case DXIL_IR_EXPR_LOAD: case DXIL_IR_EXPR_ADDRESS_OF: case DXIL_IR_EXPR_DEREF:
                expression = node->inner; break;
            case DXIL_IR_EXPR_MEMBER: case DXIL_IR_EXPR_INDEX: expression = node->base; break;
            case DXIL_IR_EXPR_BOOL_LIT: case DXIL_IR_EXPR_INT_LIT: case DXIL_IR_EXPR_FLOAT_LIT:
            case DXIL_IR_EXPR_PARAM_REF: case DXIL_IR_EXPR_LOCAL_REF: case DXIL_IR_EXPR_GLOBAL_REF:
            case DXIL_IR_EXPR_UNARY: case DXIL_IR_EXPR_BINARY: case DXIL_IR_EXPR_CALL:
            case DXIL_IR_EXPR_CONSTRUCT: return false;
        }
    }
    return false;
}

bool dxil_hlsl_pointer_index_captured(const DxilHlslEmitter *emitter,
    const DxilIrFunction *function, DxilIrExprId expression) {
    if (expression >= function->expr_count || function->exprs[expression].kind != DXIL_IR_EXPR_INDEX) return false;
    uint64_t literal;
    if (dxil_ir_match_int_literal(function, function->exprs[expression].index_expr, &literal)) return false;
    for (uint32_t i = 0u; i < function->stmt_count; ++i) {
        const DxilIrStmt *statement = &function->stmts[i];
        if (statement->kind != DXIL_IR_STMT_LOCAL_DECL || statement->local >= function->local_count ||
            !statement->is_const || !statement->has_initializer) continue;
        DxilIrType type;
        if (dxil_ir_type_get(emitter->module, function->locals[statement->local].ty, &type) != DXIL_OK ||
            type.kind != DXIL_IR_TYPE_REF) continue;
        if (contains_index(function, statement->initializer, expression)) return true;
    }
    return false;
}

static bool runtime_index(const DxilIrFunction *function, uint32_t expression,
    uint32_t *raw, uint32_t *length) {
    if (expression >= function->expr_count || function->exprs[expression].kind != DXIL_IR_EXPR_INDEX) return false;
    uint32_t clamp = function->exprs[expression].index_expr;
    if (clamp >= function->expr_count) return false;
    const DxilIrExpr *node = &function->exprs[clamp];
    return node->kind == DXIL_IR_EXPR_CALL && node->robustness_generated && node->name != NULL &&
        strcmp(node->name, "min") == 0 && dxil_ir_runtime_array_write_guard(function, expression, raw, length);
}
static DxilStatus named(DxilHlslEmitter *emitter, const char *prefix, uint32_t id) {
    return dxil_hlsl_write_temporary(emitter, prefix, id);
}
DxilStatus dxil_hlsl_emit_captured_index(DxilHlslEmitter *emitter,
    const DxilIrFunction *function, DxilIrExprId expression, bool runtime_raw) {
    uint32_t raw, length;
    const bool runtime = runtime_raw && runtime_index(function, expression, &raw, &length);
    return named(emitter, runtime ? "doe_pointer_raw_" : "doe_pointer_index_", expression);
}
bool dxil_hlsl_runtime_guard(const DxilHlslEmitter *emitter,
    const DxilIrFunction *function, DxilIrExprId expression,
    DxilIrExprId *captured, DxilIrExprId *raw, DxilIrExprId *length) {
    for (uint32_t depth = 0u; depth < function->expr_count; ++depth) {
        if (expression >= function->expr_count) return false;
        const DxilIrExpr *node = &function->exprs[expression];
        if (runtime_index(function, expression, raw, length)) {
            *captured = dxil_hlsl_pointer_index_captured(emitter, function, expression) ? expression : DXIL_IR_INVALID_ID;
            return true;
        }
        if (node->kind == DXIL_IR_EXPR_MEMBER || node->kind == DXIL_IR_EXPR_INDEX) expression = node->base;
        else if (node->kind == DXIL_IR_EXPR_ADDRESS_OF || node->kind == DXIL_IR_EXPR_DEREF || node->kind == DXIL_IR_EXPR_LOAD)
            expression = node->inner;
        else if (node->kind == DXIL_IR_EXPR_LOCAL_REF && node->index < function->local_count) {
            DxilIrType type;
            if (dxil_ir_type_get(emitter->module, function->locals[node->index].ty, &type) != DXIL_OK ||
                type.kind != DXIL_IR_TYPE_REF || !dxil_ir_resolve_const_local_initializer(function, node->index, &expression)) return false;
        } else return false;
    }
    return false;
}
static DxilStatus snapshot(DxilHlslEmitter *emitter, const DxilIrFunction *function,
    uint32_t expression, const char *prefix, uint32_t owner) {
    DxilStatus status = dxil_hlsl_write_indent(emitter);
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, "const ");
    if (status == DXIL_OK) status = dxil_hlsl_emit_type_only(emitter, function->exprs[expression].ty);
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, " ");
    if (status == DXIL_OK) status = named(emitter, prefix, owner);
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, " = ");
    if (status == DXIL_OK) status = dxil_hlsl_emit_expr(emitter, function, expression);
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, ";\n");
    return status;
}
static DxilStatus runtime_snapshot(DxilHlslEmitter *emitter, const DxilIrFunction *function,
    uint32_t index, uint32_t raw, uint32_t length) {
    DxilStatus status = snapshot(emitter, function, raw, "doe_pointer_raw_", index);
    if (status == DXIL_OK) status = snapshot(emitter, function, length, "doe_pointer_length_", index);
    if (status == DXIL_OK) status = dxil_hlsl_write_indent(emitter);
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, "const bool ");
    if (status == DXIL_OK) status = named(emitter, "doe_pointer_valid_", index);
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, " = ");
    if (status == DXIL_OK) status = named(emitter, "doe_pointer_raw_", index);
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, " < ");
    if (status == DXIL_OK) status = named(emitter, "doe_pointer_length_", index);
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, ";\n");
    return status;
}

DxilStatus dxil_hlsl_emit_pointer_captures(DxilHlslEmitter *emitter,
    const DxilIrFunction *function, DxilIrExprId expression) {
    uint32_t indices[DXIL_IR_MAX_EXPRS], count = 0u;
    bool root = false;
    for (uint32_t n = 0u; n < function->expr_count; ++n) {
        if (expression >= function->expr_count) return DXIL_INVALID_IR;
        const DxilIrExpr *node = &function->exprs[expression];
        switch (node->kind) {
            case DXIL_IR_EXPR_LOAD: case DXIL_IR_EXPR_ADDRESS_OF: case DXIL_IR_EXPR_DEREF:
                expression = node->inner; break;
            case DXIL_IR_EXPR_MEMBER: expression = node->base; break;
            case DXIL_IR_EXPR_INDEX:
                if (count == DXIL_IR_MAX_EXPRS || node->index_expr >= function->expr_count) return DXIL_INVALID_IR;
                indices[count++] = expression;
                expression = node->base;
                break;
            case DXIL_IR_EXPR_PARAM_REF: case DXIL_IR_EXPR_LOCAL_REF: case DXIL_IR_EXPR_GLOBAL_REF:
                root = true; break;
            case DXIL_IR_EXPR_BOOL_LIT: case DXIL_IR_EXPR_INT_LIT: case DXIL_IR_EXPR_FLOAT_LIT:
            case DXIL_IR_EXPR_UNARY: case DXIL_IR_EXPR_BINARY: case DXIL_IR_EXPR_CALL:
            case DXIL_IR_EXPR_CONSTRUCT: return DXIL_INVALID_IR;
        }
        if (root) break;
    }
    if (!root) return DXIL_INVALID_IR;
    for (uint32_t n = count; n != 0u; --n) {
        uint32_t id = indices[n - 1u], index = function->exprs[id].index_expr;
        uint64_t literal;
        if (dxil_ir_match_int_literal(function, index, &literal)) continue;
        uint32_t raw, length;
        bool runtime = runtime_index(function, id, &raw, &length);
        DxilStatus status = runtime ? runtime_snapshot(emitter, function, id, raw, length) : DXIL_OK;
        if (status != DXIL_OK) return status;
        bool saved = emitter->pointer_override;
        uint32_t saved_raw = emitter->pointer_override_raw, saved_length = emitter->pointer_override_length;
        uint32_t saved_owner = emitter->pointer_override_owner;
        if (runtime) {
            emitter->pointer_override = true;
            emitter->pointer_override_raw = raw; emitter->pointer_override_length = length;
            emitter->pointer_override_owner = id;
        }
        status = snapshot(emitter, function, index, "doe_pointer_index_", id);
        emitter->pointer_override = saved;
        emitter->pointer_override_raw = saved_raw; emitter->pointer_override_length = saved_length;
        emitter->pointer_override_owner = saved_owner;
        if (status != DXIL_OK) return status;
    }
    return DXIL_OK;
}
