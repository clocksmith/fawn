/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_frontend.h"

#include "tsir_frontend_expr.h"
#include "tsir_frontend_internal.h"
#include "tsir_frontend_passes.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ---- reductions walker ---- */

typedef struct ReductionCtx {
    const TsirWgslModule *module;
    const TsirIrFunction *func;
    uint32_t func_index;
    TsirRejectionList *rejections;
    TsirReductionList *reductions;
    const uint32_t *global_indices;
    size_t global_index_count;
    uint32_t axis_counter;
    TsirStatus err;
} ReductionCtx;

static const TsirIrStmt *rstmt2(ReductionCtx *ctx, uint32_t id) {
    if (id >= ctx->func->stmt_count) {
        return NULL;
    }
    return &ctx->func->stmts[id];
}

static const TsirIrExpr *rexpr(ReductionCtx *ctx, uint32_t id) {
    if (id >= ctx->func->expr_count) {
        return NULL;
    }
    return &ctx->func->exprs[id];
}

static bool contains_alias_load(ReductionCtx *ctx, uint32_t id,
    const uint32_t *aliases, size_t alias_count) {
    const TsirIrExpr *node = rexpr(ctx, id);
    uint32_t i = 0;
    if (node == NULL) {
        return false;
    }
    switch (node->kind) {
        case TSIR_IR_EXPR_LOAD: {
            const TsirIrExpr *inner = rexpr(ctx, node->inner);
            size_t a = 0;
            if (inner != NULL &&
                inner->kind == TSIR_IR_EXPR_LOCAL_REF) {
                for (a = 0; a < alias_count; a++) {
                    if (aliases[a] == inner->ref_index) {
                        return true;
                    }
                }
            }
            return contains_alias_load(ctx, node->inner, aliases,
                alias_count);
        }
        case TSIR_IR_EXPR_UNARY:
            return contains_alias_load(ctx, node->unary_operand,
                aliases, alias_count);
        case TSIR_IR_EXPR_BINARY:
            return contains_alias_load(ctx, node->binary_lhs,
                    aliases, alias_count) ||
                contains_alias_load(ctx, node->binary_rhs, aliases,
                    alias_count);
        case TSIR_IR_EXPR_INDEX:
            return contains_alias_load(ctx, node->index_base,
                    aliases, alias_count) ||
                contains_alias_load(ctx, node->index_index,
                    aliases, alias_count);
        case TSIR_IR_EXPR_MEMBER:
            return contains_alias_load(ctx, node->member_base,
                aliases, alias_count);
        case TSIR_IR_EXPR_CONSTRUCT:
        case TSIR_IR_EXPR_CALL:
            for (i = 0; i < node->args_len; i++) {
                if (node->args_start + i >=
                    ctx->func->expr_arg_count) {
                    return false;
                }
                if (contains_alias_load(ctx,
                    ctx->func->expr_args[node->args_start + i],
                    aliases, alias_count)) {
                    return true;
                }
            }
            return false;
        case TSIR_IR_EXPR_BOOL_LIT:
        case TSIR_IR_EXPR_INT_LIT:
        case TSIR_IR_EXPR_FLOAT_LIT:
        case TSIR_IR_EXPR_PARAM_REF:
        case TSIR_IR_EXPR_LOCAL_REF:
        case TSIR_IR_EXPR_GLOBAL_REF:
        case TSIR_IR_EXPR_ADDRESS_OF:
        case TSIR_IR_EXPR_DEREF:
        default:
            return false;
    }
}

static bool map_global_to_binding(ReductionCtx *ctx,
    uint32_t global_index, uint32_t *out) {
    size_t i = 0;
    for (i = 0; i < ctx->global_index_count; i++) {
        if (ctx->global_indices[i] == global_index) {
            *out = (uint32_t)i;
            return true;
        }
    }
    return false;
}

static bool resolve_target_binding(ReductionCtx *ctx,
    uint32_t range_start, uint32_t range_len, uint32_t loop_index,
    uint32_t acc_local, uint32_t *out) {
    uint32_t aliases[8];
    size_t alias_count = 1;
    uint32_t k = 0;
    aliases[0] = acc_local;
    for (k = loop_index + 1; k < range_len; k++) {
        uint32_t stmt_id = 0;
        const TsirIrStmt *stmt = NULL;
        if (range_start + k >= ctx->func->stmt_child_count) {
            return false;
        }
        stmt_id = ctx->func->stmt_children[range_start + k];
        stmt = rstmt2(ctx, stmt_id);
        if (stmt == NULL) {
            return false;
        }
        if (stmt->kind == TSIR_IR_STMT_LOCAL_DECL) {
            if (!stmt->has_initializer) {
                continue;
            }
            if (!contains_alias_load(ctx, stmt->initializer,
                aliases, alias_count)) {
                continue;
            }
            if (alias_count < 8) {
                aliases[alias_count] = stmt->local;
                alias_count += 1;
            }
        } else if (stmt->kind == TSIR_IR_STMT_ASSIGN) {
            uint32_t global_index = 0;
            if (!contains_alias_load(ctx, stmt->assign_rhs,
                aliases, alias_count)) {
                continue;
            }
            if (!tsir_ir_find_global_base(ctx->func,
                stmt->assign_lhs, &global_index)) {
                continue;
            }
            if (map_global_to_binding(ctx, global_index, out)) {
                return true;
            }
        }
    }
    return false;
}

static bool detect_reduction_op(ReductionCtx *ctx,
    const TsirIrStmt *assign, TsirReductionOp *out,
    uint32_t *acc_out) {
    const TsirIrExpr *lhs = rexpr(ctx, assign->assign_lhs);
    if (lhs == NULL || lhs->kind != TSIR_IR_EXPR_LOCAL_REF) {
        return false;
    }
    *acc_out = lhs->ref_index;
    if (assign->assign_op == TSIR_IR_ASSIGN_ADD) {
        *out = TSIR_REDUCTION_SUM;
        return true;
    }
    if (assign->assign_op == TSIR_IR_ASSIGN_MUL) {
        *out = TSIR_REDUCTION_PRODUCT;
        return true;
    }
    if (assign->assign_op != TSIR_IR_ASSIGN) {
        return false;
    }
    {
        const TsirIrExpr *rhs = rexpr(ctx, assign->assign_rhs);
        if (rhs == NULL) {
            return false;
        }
        if (rhs->kind == TSIR_IR_EXPR_BINARY) {
            const TsirIrExpr *binary_lhs =
                rexpr(ctx, rhs->binary_lhs);
            const TsirIrExpr *load_inner = NULL;
            if (binary_lhs == NULL ||
                binary_lhs->kind != TSIR_IR_EXPR_LOAD) {
                return false;
            }
            load_inner = rexpr(ctx, binary_lhs->inner);
            if (load_inner == NULL ||
                load_inner->kind != TSIR_IR_EXPR_LOCAL_REF ||
                load_inner->ref_index != *acc_out) {
                return false;
            }
            if (rhs->binary_op == TSIR_IR_BIN_ADD) {
                *out = TSIR_REDUCTION_SUM;
                return true;
            }
            if (rhs->binary_op == TSIR_IR_BIN_MUL) {
                *out = TSIR_REDUCTION_PRODUCT;
                return true;
            }
            return false;
        }
        if (rhs->kind == TSIR_IR_EXPR_CALL) {
            bool is_max = false;
            bool is_min = false;
            uint32_t i = 0;
            if (!rhs->call_is_builtin || rhs->call_name == NULL) {
                return false;
            }
            is_max = tsir_text_equal(rhs->call_name, rhs->call_name_length, "max", 0);
            is_min = tsir_text_equal(rhs->call_name, rhs->call_name_length, "min", 0);
            if ((!is_max && !is_min) || rhs->args_len < 2) {
                return false;
            }
            for (i = 0; i < rhs->args_len; i++) {
                const TsirIrExpr *arg = NULL;
                const TsirIrExpr *inner = NULL;
                if (rhs->args_start + i >=
                    ctx->func->expr_arg_count) {
                    return false;
                }
                arg = rexpr(ctx,
                    ctx->func->expr_args[rhs->args_start + i]);
                if (arg == NULL ||
                    arg->kind != TSIR_IR_EXPR_LOAD) {
                    continue;
                }
                inner = rexpr(ctx, arg->inner);
                if (inner == NULL ||
                    inner->kind != TSIR_IR_EXPR_LOCAL_REF ||
                    inner->ref_index != *acc_out) {
                    continue;
                }
                *out = is_max ? TSIR_REDUCTION_MAX
                    : TSIR_REDUCTION_MIN;
                return true;
            }
            return false;
        }
        return false;
    }
}

static void scan_direct_body(ReductionCtx *ctx, uint32_t body_id,
    uint32_t my_axis, uint32_t parent_start, uint32_t parent_len,
    uint32_t position_in_parent) {
    const TsirIrStmt *body = rstmt2(ctx, body_id);
    uint32_t j = 0;
    if (body == NULL || body->kind != TSIR_IR_STMT_BLOCK) {
        return;
    }
    for (j = 0; j < body->range_len; j++) {
        uint32_t stmt_id = 0;
        const TsirIrStmt *stmt = NULL;
        TsirReductionOp op = TSIR_REDUCTION_SUM;
        uint32_t acc_local = 0;
        if (body->range_start + j >= ctx->func->stmt_child_count) {
            return;
        }
        stmt_id = ctx->func->stmt_children[body->range_start + j];
        stmt = rstmt2(ctx, stmt_id);
        if (stmt == NULL || stmt->kind != TSIR_IR_STMT_ASSIGN) {
            continue;
        }
        if (!detect_reduction_op(ctx, stmt, &op, &acc_local)) {
            continue;
        }
        {
            TsirReductionRegion region;
            uint32_t target = 0;
            bool resolved = resolve_target_binding(ctx,
                parent_start, parent_len, position_in_parent,
                acc_local, &target);
            bool kind_ok = false;
            TsirScalarKind kind = TSIR_SCALAR_F32;
            char path[128];
            memset(&region, 0, sizeof(region));
            region.axis = my_axis;
            region.op = op;
            region.contract.associativity =
                TSIR_ASSOC_STRICT_ORDERED;
            region.contract.nan_inf = TSIR_NANINF_PROPAGATE;
            if (!resolved) {
                snprintf(path, sizeof(path),
                    "functions[%u].reductions[%zu]", ctx->func_index,
                    ctx->reductions->count);
                if (!tsir_frontend_rejection_append(ctx->rejections,
                    TSIR_REJECT_DEPENDENCE_UNANALYZABLE, path,
                    "reduction accumulator has no post-loop "
                    "writeback to a bound global")) {
                    ctx->err = TSIR_ERR_NOMEM;
                    return;
                }
                target = 0;
            }
            if (acc_local < ctx->func->local_count) {
                uint32_t cursor = 0;
                const TsirIrType *t = NULL;
                if (tsir_ir_unwrap_ref(ctx->module,
                    ctx->func->locals[acc_local].ty, &cursor) &&
                    cursor < ctx->module->type_count) {
                    t = &ctx->module->types[cursor];
                    if (t->kind == TSIR_IR_TYPE_SCALAR) {
                        kind = tsir_ir_scalar_to_kind(t->scalar);
                        kind_ok = true;
                    }
                }
            }
            region.contract.accumulation = kind;
            if (!kind_ok) {
                snprintf(path, sizeof(path),
                    "functions[%u].reductions[%zu]", ctx->func_index,
                    ctx->reductions->count);
                if (!tsir_frontend_rejection_append(ctx->rejections,
                    TSIR_REJECT_DEPENDENCE_UNANALYZABLE, path,
                    "reduction accumulator type is not representable "
                    "as a single-scalar accumulation")) {
                    ctx->err = TSIR_ERR_NOMEM;
                    return;
                }
            }
            region.target_binding = target;
            if (!tsir_frontend_reduction_append(ctx->reductions,
                region)) {
                ctx->err = TSIR_ERR_NOMEM;
                return;
            }
        }
        return;
    }
}

static void reductions_walk_stmt(ReductionCtx *ctx, uint32_t id,
    bool has_parent, uint32_t parent_start, uint32_t parent_len,
    uint32_t position_in_parent) {
    const TsirIrStmt *stmt = rstmt2(ctx, id);
    uint32_t i = 0;
    if (stmt == NULL || ctx->err != TSIR_OK) {
        return;
    }
    switch (stmt->kind) {
        case TSIR_IR_STMT_BLOCK:
            for (i = 0; i < stmt->range_len; i++) {
                if (stmt->range_start + i >=
                    ctx->func->stmt_child_count) {
                    return;
                }
                reductions_walk_stmt(ctx,
                    ctx->func->stmt_children[stmt->range_start + i],
                    true, stmt->range_start, stmt->range_len, i);
                if (ctx->err != TSIR_OK) {
                    return;
                }
            }
            return;
        case TSIR_IR_STMT_IF:
            reductions_walk_stmt(ctx, stmt->if_then, false, 0, 0,
                0);
            if (stmt->has_if_else) {
                reductions_walk_stmt(ctx, stmt->if_else, false, 0,
                    0, 0);
            }
            return;
        case TSIR_IR_STMT_LOCAL_DECL: {
            const char *letter = NULL;
            if (tsir_expr_dispatch_axis_letter(ctx->func,
                stmt->has_initializer, stmt->initializer,
                &letter, NULL)) {
                ctx->axis_counter += 1;
            }
            return;
        }
        case TSIR_IR_STMT_LOOP: {
            bool recognized = false;
            uint32_t my_axis = 0;
            if (stmt->loop_kind == TSIR_IR_LOOP_FOR &&
                stmt->has_loop_init) {
                const TsirIrStmt *init =
                    rstmt2(ctx, stmt->loop_init);
                bool decreasing = false;
                if (init != NULL &&
                    init->kind == TSIR_IR_STMT_LOCAL_DECL) {
                    decreasing = tsir_expr_detect_step_sign(
                        ctx->func, stmt->has_loop_continuing,
                        stmt->loop_continuing,
                        init->local) == TSIR_STEP_NEGATIVE;
                }
                if (!decreasing) {
                    recognized = true;
                    my_axis = ctx->axis_counter;
                    ctx->axis_counter += 1;
                    if (has_parent) {
                        scan_direct_body(ctx, stmt->loop_body,
                            my_axis, parent_start, parent_len,
                            position_in_parent);
                        if (ctx->err != TSIR_OK) {
                            return;
                        }
                    }
                }
            }
            (void)recognized;
            reductions_walk_stmt(ctx, stmt->loop_body, false, 0, 0,
                0);
            if (stmt->has_loop_continuing) {
                reductions_walk_stmt(ctx, stmt->loop_continuing,
                    false, 0, 0, 0);
            }
            return;
        }
        case TSIR_IR_STMT_EXPR:
        case TSIR_IR_STMT_ASSIGN:
        case TSIR_IR_STMT_RETURN:
        case TSIR_IR_STMT_SWITCH:
        case TSIR_IR_STMT_BREAK:
        case TSIR_IR_STMT_CONTINUE:
        case TSIR_IR_STMT_DISCARD:
        default:
            return;
    }
}


TsirStatus tsir_frontend_recover_reductions(
    const TsirWgslModule *module, const TsirIrFunction *func,
    uint32_t func_index, TsirReductionList *reductions,
    TsirRejectionList *rejections, const uint32_t *global_indices,
    size_t global_index_count) {
    ReductionCtx ctx;
    if (module == NULL || func == NULL || reductions == NULL ||
        rejections == NULL) {
        return TSIR_ERR_INVALID;
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.module = module;
    ctx.func = func;
    ctx.func_index = func_index;
    ctx.rejections = rejections;
    ctx.reductions = reductions;
    ctx.global_indices = global_indices;
    ctx.global_index_count = global_index_count;
    ctx.err = TSIR_OK;
    reductions_walk_stmt(&ctx, func->root_stmt, false, 0, 0, 0);
    return ctx.err;
}
