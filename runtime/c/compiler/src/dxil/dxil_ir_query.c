/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_ir.h"

#include <string.h>


static const DxilIrType *type_at(const DxilIrModule *module,
    DxilIrTypeId id) {
    if (id == DXIL_IR_INVALID_ID || id >= module->type_count) {
        return NULL;
    }
    return &module->types[id];
}

bool dxil_ir_is_compute_only_module(const DxilIrModule *module) {
    uint32_t i = 0;
    if (module->entry_point_count == 0) {
        return false;
    }
    for (i = 0; i < module->entry_point_count; i++) {
        if (module->entry_points[i].stage != DXIL_IR_STAGE_COMPUTE) {
            return false;
        }
    }
    return true;
}

static const DxilIrExpr *expr_at(const DxilIrFunction *function,
    DxilIrExprId id) {
    if (id >= function->expr_count) {
        return NULL;
    }
    return &function->exprs[id];
}

static const DxilIrStmt *stmt_at(const DxilIrFunction *function,
    DxilIrStmtId id) {
    if (id >= function->stmt_count) {
        return NULL;
    }
    return &function->stmts[id];
}

bool dxil_ir_reference_root_local(const DxilIrFunction *function,
    DxilIrExprId expr_id, uint32_t *out_local) {
    DxilIrExprId current = expr_id;
    uint32_t steps = 0;
    while (steps <= function->expr_count) {
        const DxilIrExpr *expr = expr_at(function, current);
        if (expr == NULL) {
            return false;
        }
        switch (expr->kind) {
            case DXIL_IR_EXPR_LOCAL_REF:
                *out_local = expr->index;
                return true;
            case DXIL_IR_EXPR_MEMBER:
                current = expr->base;
                break;
            case DXIL_IR_EXPR_INDEX:
                current = expr->base;
                break;
            case DXIL_IR_EXPR_LOAD:
                current = expr->inner;
                break;
            default:
                return false;
        }
        steps += 1;
    }
    return false;
}

static bool reference_roots_at_parameter(const DxilIrFunction *function,
    DxilIrExprId expr_id, uint32_t param_index) {
    DxilIrExprId current = expr_id;
    uint32_t steps = 0;
    while (steps <= function->expr_count) {
        const DxilIrExpr *expr = expr_at(function, current);
        if (expr == NULL) {
            return false;
        }
        switch (expr->kind) {
            case DXIL_IR_EXPR_PARAM_REF:
                return expr->index == param_index;
            case DXIL_IR_EXPR_MEMBER:
                current = expr->base;
                break;
            case DXIL_IR_EXPR_INDEX:
                current = expr->base;
                break;
            case DXIL_IR_EXPR_LOAD:
                current = expr->inner;
                break;
            default:
                return false;
        }
        steps += 1;
    }
    return false;
}

bool dxil_ir_parameter_is_assigned(const DxilIrFunction *function,
    uint32_t param_index) {
    uint32_t i = 0;
    for (i = 0; i < function->stmt_count; i++) {
        const DxilIrStmt *stmt = &function->stmts[i];
        if (stmt->kind == DXIL_IR_STMT_ASSIGN
            && reference_roots_at_parameter(function,
                stmt->assign_lhs, param_index)) {
            return true;
        }
    }
    return false;
}

bool dxil_ir_resolve_const_local_initializer(
    const DxilIrFunction *function, uint32_t local_idx,
    DxilIrExprId *out_expr) {
    uint32_t i = 0;
    for (i = 0; i < function->stmt_count; i++) {
        const DxilIrStmt *stmt = &function->stmts[i];
        if (stmt->kind == DXIL_IR_STMT_LOCAL_DECL
            && stmt->local == local_idx && stmt->is_const
            && stmt->has_initializer) {
            *out_expr = stmt->initializer;
            return true;
        }
    }
    return false;
}

DxilIrExprId dxil_ir_resolve_value_alias(
    const DxilIrFunction *function, DxilIrExprId expr_id) {
    DxilIrExprId current = expr_id;
    uint32_t steps = 0;
    while (steps <= function->expr_count) {
        const DxilIrExpr *expr = expr_at(function, current);
        DxilIrExprId resolved = 0;
        bool found = false;
        if (expr == NULL) {
            return current;
        }
        switch (expr->kind) {
            case DXIL_IR_EXPR_LOAD:
                current = expr->inner;
                break;
            case DXIL_IR_EXPR_CONSTRUCT:
                if (expr->args.len != 1) {
                    return current;
                }
                current = function->expr_args[expr->args.start];
                break;
            case DXIL_IR_EXPR_LOCAL_REF:
                found = dxil_ir_resolve_const_local_initializer(
                    function, expr->index, &resolved);
                if (!found) {
                    return current;
                }
                current = resolved;
                break;
            default:
                return current;
        }
        steps += 1;
    }
    return current;
}

DxilIrTypeId dxil_ir_resolve_indexable_type(
    const DxilIrModule *module, DxilIrTypeId ty) {
    DxilIrTypeId current = ty;
    unsigned steps = 0;
    while (steps <= module->type_count) {
        const DxilIrType *type = type_at(module, current);
        if (type == NULL || type->kind != DXIL_IR_TYPE_REF) {
            return current;
        }
        current = type->elem;
        steps += 1;
    }
    return current;
}

bool dxil_ir_classify_builtin_component(const DxilIrFunction *function,
    DxilIrExprId expr_id, DxilIrBuiltin builtin, uint8_t *out_component) {
    DxilIrExprId resolved = dxil_ir_resolve_value_alias(function,
        expr_id);
    const DxilIrExpr *expr = expr_at(function, resolved);
    const DxilIrExpr *base = NULL;
    DxilIrExprId base_resolved = 0;
    if (expr == NULL || expr->kind != DXIL_IR_EXPR_MEMBER) {
        return false;
    }
    base_resolved = dxil_ir_resolve_value_alias(function, expr->base);
    base = expr_at(function, base_resolved);
    if (base == NULL || base->kind != DXIL_IR_EXPR_PARAM_REF) {
        return false;
    }
    if (base->index >= function->param_count) {
        return false;
    }
    {
        const DxilIrParam *param = &function->params[base->index];
        if (!param->has_io || param->io.builtin != builtin) {
            return false;
        }
    }
    if (expr->field_name == NULL) {
        return false;
    }
    if (strcmp(expr->field_name, "x") == 0) {
        *out_component = 0;
        return true;
    }
    if (strcmp(expr->field_name, "y") == 0) {
        *out_component = 1;
        return true;
    }
    if (strcmp(expr->field_name, "z") == 0) {
        *out_component = 2;
        return true;
    }
    return false;
}

bool dxil_ir_match_int_literal(const DxilIrFunction *function,
    DxilIrExprId expr_id, uint64_t *out_value) {
    DxilIrExprId resolved = dxil_ir_resolve_value_alias(function,
        expr_id);
    const DxilIrExpr *expr = expr_at(function, resolved);
    if (expr == NULL || expr->kind != DXIL_IR_EXPR_INT_LIT) {
        return false;
    }
    *out_value = expr->uint_value;
    return true;
}

bool dxil_ir_matches_int_literal(const DxilIrFunction *function,
    DxilIrExprId expr_id, uint64_t expected) {
    uint64_t value = 0;
    return dxil_ir_match_int_literal(function, expr_id, &value)
        && value == expected;
}

bool dxil_ir_type_has_io_struct_field(const DxilIrModule *module,
    DxilIrTypeId ty) {
    const DxilIrType *type = type_at(module, ty);
    const DxilIrStructDef *def = NULL;
    uint32_t i = 0;
    if (type == NULL || type->kind != DXIL_IR_TYPE_STRUCT) {
        return false;
    }
    if (type->struct_id >= module->struct_count) {
        return false;
    }
    def = &module->structs[type->struct_id];
    for (i = 0; i < def->field_count; i++) {
        if (def->fields[i].has_io) {
            return true;
        }
    }
    return false;
}

bool dxil_ir_expr_is_texture_1d(const DxilIrModule *module,
    const DxilIrFunction *function, DxilIrExprId expr_id) {
    const DxilIrExpr *expr = expr_at(function, expr_id);
    const DxilIrType *type = NULL;
    if (expr == NULL) {
        return false;
    }
    type = type_at(module, expr->ty);
    return type != NULL && type->kind == DXIL_IR_TYPE_TEXTURE_1D;
}

bool dxil_ir_resolve_runtime_array_element_stride(
    const DxilIrModule *module, const DxilIrFunction *function,
    DxilIrExprId base_id, uint64_t *out_stride) {
    const DxilIrExpr *base = expr_at(function, base_id);
    DxilIrTypeId base_ty = DXIL_IR_INVALID_ID;
    const DxilIrType *array = NULL;
    uint32_t elem_size = 0;
    uint32_t elem_align = 0;
    if (base == NULL) {
        return false;
    }
    base_ty = dxil_ir_resolve_indexable_type(module, base->ty);
    array = type_at(module, base_ty);
    if (array == NULL || array->kind != DXIL_IR_TYPE_ARRAY) {
        return false;
    }
    if (array->has_len) {
        return false;
    }
    elem_size = dxil_ir_type_size(module, array->elem);
    elem_align = dxil_ir_type_alignment(module, array->elem);
    *out_stride = dxil_ir_round_up(elem_size, elem_align);
    return true;
}

static bool walk_chain_to_global(const DxilIrFunction *function,
    DxilIrExprId expr_id, bool allow_index, uint32_t *out_global) {
    DxilIrExprId current = expr_id;
    uint32_t steps = 0;
    while (steps <= function->expr_count) {
        const DxilIrExpr *expr = expr_at(function, current);
        if (expr == NULL) {
            return false;
        }
        switch (expr->kind) {
            case DXIL_IR_EXPR_GLOBAL_REF:
                *out_global = expr->index;
                return true;
            case DXIL_IR_EXPR_INDEX:
                if (!allow_index) {
                    return false;
                }
                current = expr->base;
                break;
            case DXIL_IR_EXPR_MEMBER:
                current = expr->base;
                break;
            case DXIL_IR_EXPR_LOAD:
                current = expr->inner;
                break;
            default:
                return false;
        }
        steps += 1;
    }
    return false;
}

bool dxil_ir_find_global_base(const DxilIrFunction *function,
    DxilIrExprId expr_id, uint32_t *out_global) {
    return walk_chain_to_global(function, expr_id, true, out_global);
}

bool dxil_ir_resolve_member_global(const DxilIrFunction *function,
    DxilIrExprId expr_id, uint32_t *out_global) {
    return walk_chain_to_global(function, expr_id, false, out_global);
}

bool dxil_ir_is_local_ref(const DxilIrFunction *function,
    DxilIrExprId expr_id, uint32_t local_idx) {
    DxilIrExprId resolved = dxil_ir_resolve_value_alias(function,
        expr_id);
    const DxilIrExpr *expr = expr_at(function, resolved);
    return expr != NULL && expr->kind == DXIL_IR_EXPR_LOCAL_REF
        && expr->index == local_idx;
}

static bool stmt_writes_local_depth(const DxilIrFunction *function,
    DxilIrStmtId stmt_id, uint32_t local_idx, unsigned depth) {
    const DxilIrStmt *stmt = NULL;
    uint32_t i = 0;
    if (depth > 512) {
        return false;
    }
    stmt = stmt_at(function, stmt_id);
    if (stmt == NULL) {
        return false;
    }
    switch (stmt->kind) {
        case DXIL_IR_STMT_BLOCK:
            for (i = 0; i < stmt->range.len; i++) {
                if (stmt_writes_local_depth(function,
                        function->stmt_children[stmt->range.start + i],
                        local_idx, depth + 1)) {
                    return true;
                }
            }
            return false;
        case DXIL_IR_STMT_ASSIGN:
            return dxil_ir_is_local_ref(function, stmt->assign_lhs,
                local_idx);
        case DXIL_IR_STMT_IF:
            if (stmt_writes_local_depth(function, stmt->then_block,
                    local_idx, depth + 1)) {
                return true;
            }
            return stmt->has_else_block
                && stmt_writes_local_depth(function,
                    stmt->else_block, local_idx, depth + 1);
        case DXIL_IR_STMT_LOOP: {
            if (stmt->has_init
                && stmt_writes_local_depth(function, stmt->init,
                    local_idx, depth + 1)) {
                return true;
            }
            if (stmt->has_continuing
                && stmt_writes_local_depth(function,
                    stmt->continuing, local_idx, depth + 1)) {
                return true;
            }
            return stmt_writes_local_depth(function, stmt->body,
                local_idx, depth + 1);
        }
        case DXIL_IR_STMT_SWITCH:
            for (i = 0; i < stmt->cases.len; i++) {
                const DxilIrSwitchCase *switch_case =
                    &function->switch_cases[stmt->cases.start + i];
                if (stmt_writes_local_depth(function,
                        switch_case->body, local_idx, depth + 1)) {
                    return true;
                }
            }
            return false;
        default:
            return false;
    }
}

bool dxil_ir_stmt_writes_local(const DxilIrFunction *function,
    DxilIrStmtId stmt_id, uint32_t local_idx) {
    return stmt_writes_local_depth(function, stmt_id, local_idx, 0);
}

static bool expr_contains_depth(const DxilIrFunction *function,
    DxilIrExprId expr_id, DxilIrExprId target, unsigned depth) {
    const DxilIrExpr *expr = NULL;
    uint32_t i = 0;
    if (depth > 512) {
        return false;
    }
    if (expr_id == target) {
        return true;
    }
    expr = expr_at(function, expr_id);
    if (expr == NULL) {
        return false;
    }
    switch (expr->kind) {
        case DXIL_IR_EXPR_LOAD:
        case DXIL_IR_EXPR_ADDRESS_OF:
        case DXIL_IR_EXPR_DEREF:
            return expr_contains_depth(function, expr->inner, target,
                depth + 1);
        case DXIL_IR_EXPR_UNARY:
            return expr_contains_depth(function, expr->operand, target,
                depth + 1);
        case DXIL_IR_EXPR_BINARY:
            return expr_contains_depth(function, expr->lhs, target,
                    depth + 1)
                || expr_contains_depth(function, expr->rhs, target,
                    depth + 1);
        case DXIL_IR_EXPR_CALL:
        case DXIL_IR_EXPR_CONSTRUCT:
            for (i = 0; i < expr->args.len; i++) {
                if (expr_contains_depth(function,
                        function->expr_args[expr->args.start + i],
                        target, depth + 1)) {
                    return true;
                }
            }
            return false;
        case DXIL_IR_EXPR_MEMBER:
            return expr_contains_depth(function, expr->base, target,
                depth + 1);
        case DXIL_IR_EXPR_INDEX:
            return expr_contains_depth(function, expr->base, target,
                    depth + 1)
                || expr_contains_depth(function, expr->index_expr,
                    target, depth + 1);
        default:
            return false;
    }
}

bool dxil_ir_expr_contains_expr(const DxilIrFunction *function,
    DxilIrExprId expr_id, DxilIrExprId target_expr_id) {
    return expr_contains_depth(function, expr_id, target_expr_id, 0);
}

static bool stmt_contains_depth(const DxilIrFunction *function,
    DxilIrStmtId stmt_id, DxilIrExprId target, unsigned depth) {
    const DxilIrStmt *stmt = NULL;
    uint32_t i = 0;
    if (depth > 512) {
        return false;
    }
    stmt = stmt_at(function, stmt_id);
    if (stmt == NULL) {
        return false;
    }
    switch (stmt->kind) {
        case DXIL_IR_STMT_BLOCK:
            for (i = 0; i < stmt->range.len; i++) {
                if (stmt_contains_depth(function,
                        function->stmt_children[stmt->range.start + i],
                        target, depth + 1)) {
                    return true;
                }
            }
            return false;
        case DXIL_IR_STMT_LOCAL_DECL:
            return stmt->has_initializer
                && expr_contains_depth(function, stmt->initializer,
                    target, depth + 1);
        case DXIL_IR_STMT_EXPR:
            return expr_contains_depth(function, stmt->expr, target,
                depth + 1);
        case DXIL_IR_STMT_ASSIGN:
            return expr_contains_depth(function, stmt->assign_lhs,
                    target, depth + 1)
                || expr_contains_depth(function, stmt->assign_rhs,
                    target, depth + 1);
        case DXIL_IR_STMT_RETURN:
            return stmt->has_return_value
                && expr_contains_depth(function, stmt->return_value,
                    target, depth + 1);
        case DXIL_IR_STMT_IF:
            if (expr_contains_depth(function, stmt->cond, target,
                    depth + 1)) {
                return true;
            }
            if (stmt_contains_depth(function, stmt->then_block, target,
                    depth + 1)) {
                return true;
            }
            return stmt->has_else_block
                && stmt_contains_depth(function, stmt->else_block,
                    target, depth + 1);
        case DXIL_IR_STMT_LOOP: {
            if (stmt->has_init
                && stmt_contains_depth(function, stmt->init, target,
                    depth + 1)) {
                return true;
            }
            if (stmt->has_cond
                && expr_contains_depth(function, stmt->cond, target,
                    depth + 1)) {
                return true;
            }
            if (stmt->has_continuing
                && stmt_contains_depth(function, stmt->continuing,
                    target, depth + 1)) {
                return true;
            }
            return stmt_contains_depth(function, stmt->body, target,
                depth + 1);
        }
        case DXIL_IR_STMT_SWITCH: {
            if (expr_contains_depth(function, stmt->switch_expr,
                    target, depth + 1)) {
                return true;
            }
            for (i = 0; i < stmt->cases.len; i++) {
                const DxilIrSwitchCase *switch_case =
                    &function->switch_cases[stmt->cases.start + i];
                if (stmt_contains_depth(function, switch_case->body,
                        target, depth + 1)) {
                    return true;
                }
            }
            return false;
        }
        default:
            return false;
    }
}

bool dxil_ir_stmt_contains_expr(const DxilIrFunction *function,
    DxilIrStmtId stmt_id, DxilIrExprId target_expr_id) {
    return stmt_contains_depth(function, stmt_id, target_expr_id, 0);
}

bool dxil_ir_runtime_array_write_guard(const DxilIrFunction *function,
    DxilIrExprId lhs_expr_id, DxilIrExprId *out_unclamped_index,
    DxilIrExprId *out_array_length) {
    DxilIrExprId current = lhs_expr_id;
    uint32_t steps = 0;
    while (steps <= function->expr_count) {
        const DxilIrExpr *expr = expr_at(function, current);
        const DxilIrExpr *clamp = NULL;
        const DxilIrExpr *maximum = NULL;
        const DxilIrExpr *length_expr = NULL;
        DxilIrExprId clamp_id = 0;
        if (expr == NULL) {
            return false;
        }
        if (expr->kind == DXIL_IR_EXPR_MEMBER) {
            current = expr->base;
            steps += 1;
            continue;
        }
        if (expr->kind != DXIL_IR_EXPR_INDEX) {
            return false;
        }
        clamp_id = expr->index_expr;
        clamp = expr_at(function, clamp_id);
        if (clamp == NULL || clamp->kind != DXIL_IR_EXPR_CALL
            || !clamp->robustness_generated
            || clamp->call_kind != DXIL_IR_CALL_BUILTIN
            || clamp->name == NULL
            || strcmp(clamp->name, "min") != 0
            || clamp->args.len != 2) {
            current = expr->base;
            steps += 1;
            continue;
        }
        maximum = expr_at(function,
            function->expr_args[clamp->args.start + 1]);
        if (maximum == NULL
            || maximum->kind != DXIL_IR_EXPR_BINARY
            || maximum->binary_op != DXIL_IR_BINARY_SUB
            || !dxil_ir_matches_int_literal(function, maximum->rhs,
                1)) {
            return false;
        }
        length_expr = expr_at(function, maximum->lhs);
        if (length_expr == NULL
            || length_expr->kind != DXIL_IR_EXPR_CALL
            || length_expr->call_kind != DXIL_IR_CALL_BUILTIN
            || length_expr->name == NULL
            || strcmp(length_expr->name, "arrayLength") != 0
            || length_expr->args.len != 1) {
            return false;
        }
        *out_unclamped_index =
            function->expr_args[clamp->args.start];
        *out_array_length = maximum->lhs;
        return true;
    }
    return false;
}
