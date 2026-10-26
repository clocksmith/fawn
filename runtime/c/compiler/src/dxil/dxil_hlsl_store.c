/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_hlsl_internal.h"
#include <string.h>

#define TRY(expression) do { DxilStatus status_ = (expression); if (status_ != DXIL_OK) return status_; } while (0)
static DxilStatus named(DxilHlslEmitter *e, const char *prefix, uint32_t owner) {
    return dxil_hlsl_write_temporary(e, prefix, owner);
}
static DxilStatus expression_name(DxilHlslEmitter *e, uint32_t expression) {
    TRY(named(e, "doe_store_expr_", e->store_owner));
    TRY(dxil_hlsl_write_text(e, "_"));
    return dxil_hlsl_write_u32(e, expression);
}
static DxilStatus snapshot(DxilHlslEmitter *e, const DxilIrFunction *f, uint32_t expression) {
    if (expression >= f->expr_count) return DXIL_INVALID_IR;
    for (uint32_t i = 0u; i < e->store_expression_count; ++i)
        if (e->store_expressions[i] == expression) return DXIL_OK;
    if (e->store_expression_count == DXIL_IR_MAX_EXPRS) return DXIL_OUTPUT_TOO_LARGE;
    TRY(dxil_hlsl_write_indent(e));
    TRY(dxil_hlsl_write_text(e, "const "));
    TRY(dxil_hlsl_emit_type_only(e, f->exprs[expression].ty));
    TRY(dxil_hlsl_write_text(e, " "));
    TRY(expression_name(e, expression));
    TRY(dxil_hlsl_write_text(e, " = "));
    TRY(dxil_hlsl_emit_expr(e, f, expression));
    TRY(dxil_hlsl_write_text(e, ";\n"));
    e->store_expressions[e->store_expression_count++] = expression;
    return DXIL_OK;
}
static DxilStatus rhs_snapshot(DxilHlslEmitter *e, const DxilIrFunction *f, uint32_t expression) {
    if (expression >= f->expr_count) return DXIL_INVALID_IR;
    char name[DXIL_HLSL_TEMPORARY_NAME_CAP];
    TRY(dxil_hlsl_temporary_name(e, "doe_store_rhs_", e->store_owner, name));
    TRY(dxil_hlsl_write_indent(e)); TRY(dxil_hlsl_write_text(e, "const "));
    TRY(dxil_hlsl_emit_typed_name(e, f->exprs[expression].ty, name));
    TRY(dxil_hlsl_write_text(e, " = "));
    /* RHS is a new evaluation, even if caller-built graph identities are
     * shared with the LHS. Only the LHS is bound to our reference snapshots. */
    e->store_override = false;
    DxilStatus status = dxil_hlsl_emit_expr(e, f, expression);
    e->store_override = true;
    if (status != DXIL_OK) return status;
    return dxil_hlsl_write_text(e, ";\n");
}
static DxilStatus path_snapshots(DxilHlslEmitter *e, const DxilIrFunction *f, uint32_t expression) {
    uint32_t path[DXIL_IR_MAX_EXPRS], count = 0u;
    bool root = false;
    for (uint32_t depth = 0u; depth < f->expr_count; ++depth) {
        if (expression >= f->expr_count) return DXIL_INVALID_IR;
        const DxilIrExpr *node = &f->exprs[expression];
        if (node->kind == DXIL_IR_EXPR_INDEX) {
            if (count == DXIL_IR_MAX_EXPRS) return DXIL_INVALID_IR;
            path[count++] = expression; expression = node->base;
        } else if (node->kind == DXIL_IR_EXPR_MEMBER) expression = node->base;
        else if (node->kind == DXIL_IR_EXPR_LOAD || node->kind == DXIL_IR_EXPR_ADDRESS_OF || node->kind == DXIL_IR_EXPR_DEREF)
            expression = node->inner;
        else if (node->kind == DXIL_IR_EXPR_LOCAL_REF) {
            if (node->index >= f->local_count) return DXIL_INVALID_IR;
            DxilIrType type;
            if (dxil_ir_type_get(e->module, f->locals[node->index].ty, &type) != DXIL_OK) return DXIL_INVALID_IR;
            if (type.kind != DXIL_IR_TYPE_REF) { root = true; break; }
            if (!dxil_ir_resolve_const_local_initializer(f, node->index, &expression)) return DXIL_INVALID_IR;
        } else if (node->kind == DXIL_IR_EXPR_GLOBAL_REF || node->kind == DXIL_IR_EXPR_PARAM_REF) {
            root = true; break;
        } else return DXIL_INVALID_IR;
    }
    if (!root) return DXIL_INVALID_IR;
    for (uint32_t n = count; n != 0u; --n) {
        uint32_t id = path[n - 1u];
        if (!dxil_hlsl_pointer_index_captured(e, f, id)) TRY(snapshot(e, f, f->exprs[id].index_expr));
    }
    return DXIL_OK;
}
static DxilStatus guard_name(DxilHlslEmitter *e, uint32_t captured) {
    return named(e, captured == DXIL_IR_INVALID_ID ? "doe_store_valid_" : "doe_pointer_valid_",
        captured == DXIL_IR_INVALID_ID ? e->store_owner : captured);
}
static DxilStatus body(DxilHlslEmitter *e, const DxilIrFunction *f, uint32_t statement,
    uint32_t captured, uint32_t raw, uint32_t length) {
    const DxilIrStmt *node = &f->stmts[statement];
    TRY(dxil_hlsl_write_indent(e)); TRY(dxil_hlsl_write_text(e, "{\n")); e->indent += 4u;
    if (captured == DXIL_IR_INVALID_ID) {
        TRY(snapshot(e, f, raw)); TRY(snapshot(e, f, length));
        TRY(dxil_hlsl_write_indent(e)); TRY(dxil_hlsl_write_text(e, "const bool "));
        TRY(guard_name(e, captured)); TRY(dxil_hlsl_write_text(e, " = "));
        TRY(dxil_hlsl_emit_expr(e, f, raw)); TRY(dxil_hlsl_write_text(e, " < "));
        TRY(dxil_hlsl_emit_expr(e, f, length)); TRY(dxil_hlsl_write_text(e, ";\n"));
    }
    TRY(path_snapshots(e, f, node->assign_lhs));
    const bool compound = node->assign_op != DXIL_IR_ASSIGN_ASSIGN;
    if (compound) {
        /* Match shared validate.c: form reference, load old value, then RHS.
         * The zero fallback avoids a physical OOB read even though its final
         * store is discarded. No RHS effects are hidden inside the guard. */
        TRY(dxil_hlsl_write_indent(e));
        TRY(dxil_hlsl_emit_type_only(e, f->exprs[node->assign_lhs].ty));
        TRY(dxil_hlsl_write_text(e, " ")); TRY(named(e, "doe_store_old_", statement));
        TRY(dxil_hlsl_write_text(e, " = ("));
        TRY(dxil_hlsl_emit_type_only(e, f->exprs[node->assign_lhs].ty));
        TRY(dxil_hlsl_write_text(e, ")0;\n"));
        TRY(dxil_hlsl_write_indent(e)); TRY(dxil_hlsl_write_text(e, "if ("));
        TRY(guard_name(e, captured)); TRY(dxil_hlsl_write_text(e, ") "));
        TRY(named(e, "doe_store_old_", statement)); TRY(dxil_hlsl_write_text(e, " = "));
        TRY(dxil_hlsl_emit_expr(e, f, node->assign_lhs)); TRY(dxil_hlsl_write_text(e, ";\n"));
    }
    TRY(rhs_snapshot(e, f, node->assign_rhs));
    TRY(dxil_hlsl_write_indent(e)); TRY(dxil_hlsl_write_text(e, "if ("));
    TRY(guard_name(e, captured)); TRY(dxil_hlsl_write_text(e, ") {\n")); e->indent += 4u;
    TRY(dxil_hlsl_write_indent(e)); TRY(dxil_hlsl_emit_expr(e, f, node->assign_lhs));
    TRY(dxil_hlsl_write_text(e, " = "));
    if (compound) {
        const char *assignment = dxil_hlsl_assignment_text(node->assign_op);
        size_t size = strlen(assignment);
        if (size < 2u || size > 3u) return DXIL_INVALID_IR;
        char operation[3]; memcpy(operation, assignment, size - 1u); operation[size - 1u] = '\0';
        TRY(named(e, "doe_store_old_", statement)); TRY(dxil_hlsl_write_text(e, " "));
        TRY(dxil_hlsl_write_text(e, operation)); TRY(dxil_hlsl_write_text(e, " "));
    }
    TRY(named(e, "doe_store_rhs_", statement)); TRY(dxil_hlsl_write_text(e, ";\n"));
    e->indent -= 4u; TRY(dxil_hlsl_write_indent(e)); TRY(dxil_hlsl_write_text(e, "}\n"));
    e->indent -= 4u; TRY(dxil_hlsl_write_indent(e)); return dxil_hlsl_write_text(e, "}\n");
}
DxilStatus dxil_hlsl_emit_guarded_store(DxilHlslEmitter *e, const DxilIrFunction *f,
    DxilIrStmtId statement, DxilIrExprId captured, DxilIrExprId raw, DxilIrExprId length) {
    if (e->store_override || statement >= f->stmt_count) return DXIL_INVALID_IR;
    size_t indent = e->indent;
    e->store_override = true; e->store_owner = statement; e->store_expression_count = 0u;
    DxilStatus status = body(e, f, statement, captured, raw, length);
    e->store_override = false; e->store_expression_count = 0u; e->indent = indent;
    return status;
}
#undef TRY
