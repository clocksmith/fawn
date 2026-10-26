/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_frontend.h"

#include "tsir_frontend_expr.h"
#include "tsir_frontend_internal.h"
#include "tsir_frontend_passes.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static char *join2(const char *a, const char *b) {
    size_t la = strlen(a);
    size_t lb = strlen(b);
    char *out = (char *)malloc(la + lb + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, a, la);
    memcpy(out + la, b, lb + 1);
    return out;
}

/* ---- axes walker ---- */

typedef struct AxisCtx {
    const TsirWgslModule *module;
    const TsirIrFunction *func;
    TsirAxisList *axes;
    TsirStatus err;
} AxisCtx;

static const TsirIrStmt *astmt(AxisCtx *ctx, uint32_t id) {
    if (id >= ctx->func->stmt_count) {
        return NULL;
    }
    return &ctx->func->stmts[id];
}

static void axes_walk_stmt(AxisCtx *ctx, uint32_t id);

static void axes_walk_block_children(AxisCtx *ctx,
    const TsirIrStmt *block_stmt) {
    uint32_t i = 0;
    for (i = 0; i < block_stmt->range_len; i++) {
        uint32_t child_id = 0;
        const TsirIrStmt *child = NULL;
        if (block_stmt->range_start + i >=
            ctx->func->stmt_child_count) {
            return;
        }
        child_id =
            ctx->func->stmt_children[block_stmt->range_start + i];
        child = astmt(ctx, child_id);
        if (child == NULL) {
            return;
        }
        if (child->kind == TSIR_IR_STMT_LOCAL_DECL) {
            const char *letter = NULL;
            if (tsir_expr_dispatch_axis_letter(ctx->func,
                child->has_initializer, child->initializer,
                &letter) &&
                child->local < ctx->func->local_count) {
                TsirIterationAxis axis;
                char *upper = NULL;
                char *name_copy = NULL;
                int rc = 0;
                rc = tsir_expr_scan_dispatch_guard(ctx->module,
                    ctx->func, block_stmt->range_start,
                    block_stmt->range_len, i, child->local, &upper);
                if (rc == -1) {
                    ctx->err = TSIR_ERR_NOMEM;
                    return;
                }
                if (rc == 0) {
                    upper = join2("dispatch.", letter);
                    if (upper == NULL) {
                        ctx->err = TSIR_ERR_NOMEM;
                        return;
                    }
                }
                name_copy = tsir_frontend_dup_str(
                    ctx->func->locals[child->local].name);
                if (name_copy == NULL) {
                    free(upper);
                    ctx->err = TSIR_ERR_NOMEM;
                    return;
                }
                memset(&axis, 0, sizeof(axis));
                axis.name = name_copy;
                axis.lower_bound = tsir_frontend_dup_str("0");
                axis.upper_bound = upper;
                axis.step = tsir_frontend_dup_str("1");
                if (axis.lower_bound == NULL ||
                    axis.step == NULL) {
                    free((void *)axis.name);
                    free((void *)axis.lower_bound);
                    free((void *)axis.upper_bound);
                    free((void *)axis.step);
                    ctx->err = TSIR_ERR_NOMEM;
                    return;
                }
                if (!tsir_frontend_axis_append(ctx->axes, axis)) {
                    free((void *)axis.name);
                    free((void *)axis.lower_bound);
                    free((void *)axis.upper_bound);
                    free((void *)axis.step);
                    ctx->err = TSIR_ERR_NOMEM;
                    return;
                }
            }
            continue;
        }
        axes_walk_stmt(ctx, child_id);
        if (ctx->err != TSIR_OK) {
            return;
        }
    }
}

static void axes_walk_stmt(AxisCtx *ctx, uint32_t id) {
    const TsirIrStmt *stmt = astmt(ctx, id);
    if (stmt == NULL || ctx->err != TSIR_OK) {
        return;
    }
    switch (stmt->kind) {
        case TSIR_IR_STMT_BLOCK:
            axes_walk_block_children(ctx, stmt);
            return;
        case TSIR_IR_STMT_IF:
            axes_walk_stmt(ctx, stmt->if_then);
            if (stmt->has_if_else) {
                axes_walk_stmt(ctx, stmt->if_else);
            }
            return;
        case TSIR_IR_STMT_LOOP: {
            bool is_for = (stmt->loop_kind == TSIR_IR_LOOP_FOR);
            if (is_for && stmt->has_loop_init) {
                const TsirIrStmt *init =
                    astmt(ctx, stmt->loop_init);
                if (init != NULL &&
                    init->kind == TSIR_IR_STMT_LOCAL_DECL) {
                    uint32_t local = init->local;
                    bool decreasing =
                        tsir_expr_detect_step_sign(ctx->func,
                            stmt->has_loop_continuing,
                            stmt->loop_continuing,
                            local) == TSIR_STEP_NEGATIVE;
                    if (!decreasing && local < ctx->func->local_count) {
                        TsirIterationAxis axis;
                        char *lower = NULL;
                        char *upper = NULL;
                        char *step = NULL;
                        char *name_copy = NULL;
                        uint64_t literal = 0;
                        int rc = 0;
                        rc = tsir_expr_extract_init_bound(ctx->module,
                            ctx->func, init->has_initializer,
                            init->initializer, &lower);
                        if (rc == -1) {
                            ctx->err = TSIR_ERR_NOMEM;
                            return;
                        }
                        if (rc == 0) {
                            lower = tsir_frontend_dup_str("0");
                        }
                        if (tsir_expr_literal_upper_bound(ctx->func,
                            stmt->has_loop_cond, stmt->loop_cond,
                            local, &literal) == 1) {
                            char tmp[32];
                            snprintf(tmp, sizeof(tmp), "%llu",
                                (unsigned long long)literal);
                            upper = tsir_frontend_dup_str(tmp);
                        } else {
                            rc = tsir_expr_symbolic_upper_bound(
                                ctx->module, ctx->func,
                                stmt->has_loop_cond, stmt->loop_cond,
                                local, &upper);
                            if (rc == -1) {
                                free(lower);
                                ctx->err = TSIR_ERR_NOMEM;
                                return;
                            }
                            if (rc == 0) {
                                upper = tsir_frontend_dup_str("upper_bound");
                            }
                        }
                        rc = tsir_expr_extract_step(ctx->module,
                            ctx->func, stmt->has_loop_continuing,
                            stmt->loop_continuing, local, &step);
                        if (rc == -1) {
                            free(lower);
                            free(upper);
                            ctx->err = TSIR_ERR_NOMEM;
                            return;
                        }
                        if (rc == 0) {
                            step = tsir_frontend_dup_str("1");
                        }
                        name_copy = tsir_frontend_dup_str(
                            ctx->func->locals[local].name);
                        if (lower == NULL || upper == NULL ||
                            step == NULL || name_copy == NULL) {
                            free(lower);
                            free(upper);
                            free(step);
                            free(name_copy);
                            ctx->err = TSIR_ERR_NOMEM;
                            return;
                        }
                        memset(&axis, 0, sizeof(axis));
                        axis.name = name_copy;
                        axis.lower_bound = lower;
                        axis.upper_bound = upper;
                        axis.step = step;
                        if (!tsir_frontend_axis_append(ctx->axes,
                            axis)) {
                            free(name_copy);
                            free(lower);
                            free(upper);
                            free(step);
                            ctx->err = TSIR_ERR_NOMEM;
                            return;
                        }
                    }
                }
            }
            axes_walk_stmt(ctx, stmt->loop_body);
            if (stmt->has_loop_continuing) {
                axes_walk_stmt(ctx, stmt->loop_continuing);
            }
            return;
        }
        case TSIR_IR_STMT_LOCAL_DECL:
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


TsirStatus tsir_frontend_recover_axes(const TsirWgslModule *module,
    const TsirIrFunction *func, TsirAxisList *axes) {
    AxisCtx ctx;
    if (module == NULL || func == NULL || axes == NULL) {
        return TSIR_ERR_INVALID;
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.module = module;
    ctx.func = func;
    ctx.axes = axes;
    ctx.err = TSIR_OK;
    axes_walk_stmt(&ctx, func->root_stmt);
    return ctx.err;
}
