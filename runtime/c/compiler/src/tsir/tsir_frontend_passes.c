/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_frontend_passes.h"

#include "tsir_frontend_expr.h"

#include <stdio.h>
#include <stdlib.h>
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

static bool name_equals_ci(const char *a, size_t length, const char *b) {
    size_t i = 0;
    size_t count = tsir_text_length(a, length);
    if (a == NULL || b == NULL || count != strlen(b)) {
        return false;
    }
    for (i = 0; i < count; i++) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return false;
        }
    }
    return true;
}

/* ---- collectives ---- */

static const TsirAlgorithmExactInvariant collective_invariants[2] = {
    TSIR_INVARIANT_REDUCTION_ORDER,
    TSIR_INVARIANT_ASSOCIATIVITY_GROUPING
};

typedef struct CollectiveCtx {
    const TsirWgslModule *module;
    const TsirIrFunction *func;
    uint32_t func_index;
    TsirCollectiveList *out;
    TsirRejectionList *rejections;
    uint32_t axis_counter;
    uint32_t *axis_stack;
    size_t stack_count;
    size_t stack_cap;
    TsirStatus err;
} CollectiveCtx;

static const TsirIrExpr *cexpr(CollectiveCtx *ctx, uint32_t id) {
    if (id >= ctx->func->expr_count) {
        return NULL;
    }
    return &ctx->func->exprs[id];
}

static const TsirIrStmt *cstmt(CollectiveCtx *ctx, uint32_t id) {
    if (id >= ctx->func->stmt_count) {
        return NULL;
    }
    return &ctx->func->stmts[id];
}

static bool collective_kind_for_name(const char *name, size_t length,
    TsirCollectiveKind *out) {
    static const struct {
        const char *name;
        TsirCollectiveKind kind;
    } table[] = {
        { "subgroupAdd", TSIR_COLLECTIVE_SUBGROUP_ADD },
        { "subgroupMin", TSIR_COLLECTIVE_SUBGROUP_MIN },
        { "subgroupMax", TSIR_COLLECTIVE_SUBGROUP_MAX },
        { "subgroupMul", TSIR_COLLECTIVE_SUBGROUP_MUL },
        { "subgroupBroadcast", TSIR_COLLECTIVE_SUBGROUP_BROADCAST },
        { "subgroupShuffle", TSIR_COLLECTIVE_SUBGROUP_SHUFFLE },
        { "subgroupBallot", TSIR_COLLECTIVE_SUBGROUP_BALLOT },
        { "subgroupInclusiveAdd",
            TSIR_COLLECTIVE_SUBGROUP_INCLUSIVE_SCAN },
        { "subgroupInclusiveMul",
            TSIR_COLLECTIVE_SUBGROUP_INCLUSIVE_SCAN },
        { "subgroupExclusiveAdd",
            TSIR_COLLECTIVE_SUBGROUP_EXCLUSIVE_SCAN },
        { "subgroupExclusiveMul",
            TSIR_COLLECTIVE_SUBGROUP_EXCLUSIVE_SCAN },
        { "workgroupBarrier", TSIR_COLLECTIVE_WORKGROUP_BARRIER }
    };
    size_t i = 0;
    if (name == NULL) {
        return false;
    }
    for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (tsir_text_equal(name, length, table[i].name, 0)) {
            *out = table[i].kind;
            return true;
        }
    }
    return false;
}

static void collective_walk_expr(CollectiveCtx *ctx, uint32_t id);

static void collective_emit(CollectiveCtx *ctx,
    const TsirIrExpr *expr, TsirCollectiveKind kind) {
    TsirCollectiveSemanticNode node;
    memset(&node, 0, sizeof(node));
    node.kind = kind;
    node.axis = (ctx->stack_count > 0)
        ? (int32_t)ctx->axis_stack[ctx->stack_count - 1]
        : -1;
    node.exactness.class = TSIR_EXACTNESS_ALGORITHM_EXACT;
    node.exactness.invariants = collective_invariants;
    node.exactness.invariant_count = 2;
    node.dtype = TSIR_SCALAR_U32;
    if (kind != TSIR_COLLECTIVE_WORKGROUP_BARRIER) {
        uint32_t cursor = 0;
        const TsirIrType *t = NULL;
        bool scalar_ok = false;
        if (ctx->module != NULL &&
            tsir_ir_unwrap_ref(ctx->module, expr->ty, &cursor) &&
            cursor < ctx->module->type_count) {
            t = &ctx->module->types[cursor];
            scalar_ok = (t->kind == TSIR_IR_TYPE_SCALAR);
            if (scalar_ok) {
                node.dtype = tsir_ir_scalar_to_kind(t->scalar);
            }
        }
        if (!scalar_ok) {
            char path[128];
            snprintf(path, sizeof(path),
                "functions[%u].collectives[%zu]", ctx->func_index,
                ctx->out->count);
            if (!tsir_frontend_rejection_append(ctx->rejections,
                TSIR_REJECT_COLLECTIVE_NOT_REPRESENTABLE, path,
                "collective return type is not representable as a "
                "single-scalar dtype")) {
                ctx->err = TSIR_ERR_NOMEM;
                return;
            }
        }
    }
    if (!tsir_frontend_collective_append(ctx->out, node)) {
        ctx->err = TSIR_ERR_NOMEM;
    }
}

static void collective_walk_expr(CollectiveCtx *ctx, uint32_t id) {
    const TsirIrExpr *expr = cexpr(ctx, id);
    uint32_t i = 0;
    if (expr == NULL || ctx->err != TSIR_OK) {
        return;
    }
    switch (expr->kind) {
        case TSIR_IR_EXPR_CALL: {
            TsirCollectiveKind kind;
            if (expr->call_is_builtin && expr->call_name != NULL &&
                collective_kind_for_name(expr->call_name, expr->call_name_length, &kind)) {
                collective_emit(ctx, expr, kind);
            }
            for (i = 0; i < expr->args_len; i++) {
                if (expr->args_start + i >=
                    ctx->func->expr_arg_count) {
                    return;
                }
                collective_walk_expr(ctx,
                    ctx->func->expr_args[expr->args_start + i]);
                if (ctx->err != TSIR_OK) {
                    return;
                }
            }
            return;
        }
        case TSIR_IR_EXPR_LOAD:
            collective_walk_expr(ctx, expr->inner);
            return;
        case TSIR_IR_EXPR_UNARY:
            collective_walk_expr(ctx, expr->unary_operand);
            return;
        case TSIR_IR_EXPR_BINARY:
            collective_walk_expr(ctx, expr->binary_lhs);
            collective_walk_expr(ctx, expr->binary_rhs);
            return;
        case TSIR_IR_EXPR_CONSTRUCT:
            for (i = 0; i < expr->args_len; i++) {
                if (expr->args_start + i >=
                    ctx->func->expr_arg_count) {
                    return;
                }
                collective_walk_expr(ctx,
                    ctx->func->expr_args[expr->args_start + i]);
                if (ctx->err != TSIR_OK) {
                    return;
                }
            }
            return;
        case TSIR_IR_EXPR_MEMBER:
            collective_walk_expr(ctx, expr->member_base);
            return;
        case TSIR_IR_EXPR_INDEX:
            collective_walk_expr(ctx, expr->index_base);
            collective_walk_expr(ctx, expr->index_index);
            return;
        case TSIR_IR_EXPR_BOOL_LIT:
        case TSIR_IR_EXPR_INT_LIT:
        case TSIR_IR_EXPR_FLOAT_LIT:
        case TSIR_IR_EXPR_PARAM_REF:
        case TSIR_IR_EXPR_LOCAL_REF:
        case TSIR_IR_EXPR_GLOBAL_REF:
        case TSIR_IR_EXPR_ADDRESS_OF:
        case TSIR_IR_EXPR_DEREF:
        default:
            return;
    }
}

static void collective_walk_stmt(CollectiveCtx *ctx, uint32_t id) {
    const TsirIrStmt *stmt = cstmt(ctx, id);
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
                collective_walk_stmt(ctx,
                    ctx->func->stmt_children[stmt->range_start + i]);
                if (ctx->err != TSIR_OK) {
                    return;
                }
            }
            return;
        case TSIR_IR_STMT_LOCAL_DECL: {
            const char *letter = NULL;
            if (tsir_expr_dispatch_axis_letter(ctx->func,
                stmt->has_initializer, stmt->initializer, &letter, NULL)) {
                ctx->axis_counter += 1;
            }
            if (stmt->has_initializer) {
                collective_walk_expr(ctx, stmt->initializer);
            }
            return;
        }
        case TSIR_IR_STMT_EXPR:
            collective_walk_expr(ctx, stmt->expr);
            return;
        case TSIR_IR_STMT_ASSIGN:
            collective_walk_expr(ctx, stmt->assign_lhs);
            collective_walk_expr(ctx, stmt->assign_rhs);
            return;
        case TSIR_IR_STMT_RETURN:
            if (stmt->has_return_expr) {
                collective_walk_expr(ctx, stmt->return_expr);
            }
            return;
        case TSIR_IR_STMT_IF:
            collective_walk_expr(ctx, stmt->if_cond);
            collective_walk_stmt(ctx, stmt->if_then);
            if (stmt->has_if_else) {
                collective_walk_stmt(ctx, stmt->if_else);
            }
            return;
        case TSIR_IR_STMT_LOOP: {
            bool recognized = false;
            uint32_t my_axis = 0;
            if (stmt->loop_kind == TSIR_IR_LOOP_FOR &&
                stmt->has_loop_init) {
                const TsirIrStmt *init =
                    cstmt(ctx, stmt->loop_init);
                recognized = true;
                if (init != NULL &&
                    init->kind == TSIR_IR_STMT_LOCAL_DECL &&
                    tsir_expr_detect_step_sign(ctx->func,
                        stmt->has_loop_continuing,
                        stmt->loop_continuing,
                        init->local) == TSIR_STEP_NEGATIVE) {
                    recognized = false;
                }
            }
            if (recognized) {
                my_axis = ctx->axis_counter;
                ctx->axis_counter += 1;
                if (ctx->stack_count >= ctx->stack_cap) {
                    size_t cap = ctx->stack_cap == 0
                        ? 8 : ctx->stack_cap * 2;
                    uint32_t *grown = (uint32_t *)realloc(
                        ctx->axis_stack, cap * sizeof(uint32_t));
                    if (grown == NULL) {
                        ctx->err = TSIR_ERR_NOMEM;
                        return;
                    }
                    ctx->axis_stack = grown;
                    ctx->stack_cap = cap;
                }
                ctx->axis_stack[ctx->stack_count] = my_axis;
                ctx->stack_count += 1;
            }
            if (stmt->has_loop_init) {
                collective_walk_stmt(ctx, stmt->loop_init);
            }
            if (stmt->has_loop_cond) {
                collective_walk_expr(ctx, stmt->loop_cond);
            }
            collective_walk_stmt(ctx, stmt->loop_body);
            if (stmt->has_loop_continuing) {
                collective_walk_stmt(ctx, stmt->loop_continuing);
            }
            if (recognized) {
                ctx->stack_count -= 1;
            }
            return;
        }
        case TSIR_IR_STMT_SWITCH:
            collective_walk_expr(ctx, stmt->expr);
            return;
        case TSIR_IR_STMT_BREAK:
        case TSIR_IR_STMT_CONTINUE:
        case TSIR_IR_STMT_DISCARD:
        default:
            return;
    }
}

TsirStatus tsir_frontend_collect_collectives(
    const TsirWgslModule *module, const TsirIrFunction *func,
    uint32_t func_index, TsirCollectiveList *out,
    TsirRejectionList *rejections) {
    CollectiveCtx ctx;
    if (func == NULL || out == NULL || rejections == NULL) {
        return TSIR_ERR_INVALID;
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.module = module;
    ctx.func = func;
    ctx.func_index = func_index;
    ctx.out = out;
    ctx.rejections = rejections;
    ctx.err = TSIR_OK;
    collective_walk_stmt(&ctx, func->root_stmt);
    free(ctx.axis_stack);
    return ctx.err;
}

/* ---- rejections ---- */

typedef struct RejectionCtx {
    const TsirIrFunction *func;
    uint32_t func_index;
    TsirRejectionList *rejections;
    TsirStatus err;
} RejectionCtx;

static const TsirIrStmt *rstmt(RejectionCtx *ctx, uint32_t id) {
    if (id >= ctx->func->stmt_count) {
        return NULL;
    }
    return &ctx->func->stmts[id];
}

static void rejection_walk_stmt(RejectionCtx *ctx, uint32_t id,
    const char *prefix) {
    const TsirIrStmt *stmt = rstmt(ctx, id);
    uint32_t i = 0;
    if (stmt == NULL || ctx->err != TSIR_OK) {
        return;
    }
    switch (stmt->kind) {
        case TSIR_IR_STMT_BLOCK:
            for (i = 0; i < stmt->range_len; i++) {
                char index_text[32];
                char *segment = NULL;
                char *child_path = NULL;
                if (stmt->range_start + i >=
                    ctx->func->stmt_child_count) {
                    return;
                }
                snprintf(index_text, sizeof(index_text),
                    ".body[%u]", i);
                segment = join2(prefix, index_text);
                if (segment == NULL) {
                    ctx->err = TSIR_ERR_NOMEM;
                    return;
                }
                child_path = segment;
                rejection_walk_stmt(ctx,
                    ctx->func->stmt_children[stmt->range_start + i],
                    child_path);
                free(child_path);
                if (ctx->err != TSIR_OK) {
                    return;
                }
            }
            return;
        case TSIR_IR_STMT_IF: {
            char *then_path = join2(prefix, ".then");
            if (then_path == NULL) {
                ctx->err = TSIR_ERR_NOMEM;
                return;
            }
            rejection_walk_stmt(ctx, stmt->if_then, then_path);
            free(then_path);
            if (ctx->err != TSIR_OK) {
                return;
            }
            if (stmt->has_if_else) {
                char *else_path = join2(prefix, ".else");
                if (else_path == NULL) {
                    ctx->err = TSIR_ERR_NOMEM;
                    return;
                }
                rejection_walk_stmt(ctx, stmt->if_else, else_path);
                free(else_path);
            }
            return;
        }
        case TSIR_IR_STMT_LOOP:
            if (stmt->loop_kind != TSIR_IR_LOOP_FOR) {
                const char *detail =
                    (stmt->loop_kind == TSIR_IR_LOOP_WHILE)
                    ? "while loop" : "unstructured loop";
                if (!tsir_frontend_rejection_append(ctx->rejections,
                    TSIR_REJECT_DEPENDENCE_UNANALYZABLE, prefix,
                    detail)) {
                    ctx->err = TSIR_ERR_NOMEM;
                    return;
                }
            } else if (stmt->has_loop_init) {
                const TsirIrStmt *init =
                    rstmt(ctx, stmt->loop_init);
                if (init != NULL &&
                    init->kind == TSIR_IR_STMT_LOCAL_DECL &&
                    tsir_expr_detect_step_sign(ctx->func,
                        stmt->has_loop_continuing,
                        stmt->loop_continuing,
                        init->local) == TSIR_STEP_NEGATIVE) {
                    if (!tsir_frontend_rejection_append(
                        ctx->rejections,
                        TSIR_REJECT_SOURCE_NOT_AFFINE, prefix,
                        "decreasing for-loop does not fit half-open "
                        "iteration model")) {
                        ctx->err = TSIR_ERR_NOMEM;
                        return;
                    }
                }
            }
            rejection_walk_stmt(ctx, stmt->loop_body, prefix);
            if (ctx->err != TSIR_OK) {
                return;
            }
            if (stmt->has_loop_continuing) {
                rejection_walk_stmt(ctx, stmt->loop_continuing,
                    prefix);
            }
            return;
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

TsirStatus tsir_frontend_recover_rejections(
    const TsirIrFunction *func, uint32_t func_index,
    TsirRejectionList *rejections) {
    RejectionCtx ctx;
    char root[64];
    if (func == NULL || rejections == NULL) {
        return TSIR_ERR_INVALID;
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.func = func;
    ctx.func_index = func_index;
    ctx.rejections = rejections;
    ctx.err = TSIR_OK;
    snprintf(root, sizeof(root), "functions[%u]", func_index);
    rejection_walk_stmt(&ctx, func->root_stmt, root);
    return ctx.err;
}

/* ---- body inference ---- */

static bool make_roles(TsirSemanticBody *out,
    const TsirSemanticBodyBinding *bindings, size_t binding_count,
    const TsirSemanticBodyAxis *axes, size_t axis_count) {
    TsirSemanticBodyBinding *bcopy = NULL;
    TsirSemanticBodyAxis *acopy = NULL;
    if (binding_count > 0) {
        bcopy = (TsirSemanticBodyBinding *)malloc(
            binding_count * sizeof(TsirSemanticBodyBinding));
        if (bcopy == NULL) {
            return false;
        }
        memcpy(bcopy, bindings,
            binding_count * sizeof(TsirSemanticBodyBinding));
    }
    if (axis_count > 0) {
        acopy = (TsirSemanticBodyAxis *)malloc(
            axis_count * sizeof(TsirSemanticBodyAxis));
        if (acopy == NULL) {
            free(bcopy);
            return false;
        }
        memcpy(acopy, axes, axis_count * sizeof(TsirSemanticBodyAxis));
    }
    out->binding_roles = bcopy;
    out->binding_role_count = binding_count;
    out->axis_roles = acopy;
    out->axis_role_count = axis_count;
    return true;
}

/* Returns 1 when the epsilon resolved (path allocated in *path_out,
 * binding/offset filled), 0 when it did not resolve (empty body),
 * -1 on allocation failure. */
static int infer_rms_epsilon(const TsirWgslModule *module,
    const TsirBufferBinding *bindings, size_t binding_count,
    const char *hidden_bound, size_t hidden_length, char **path_out,
    size_t *path_length_out, uint32_t *binding_out, uint32_t *offset_out) {
    TsirBuffer path;
    const char *rest;
    const char *dot;
    size_t binding_length;
    size_t field_length;
    size_t prefix_length = 0;
    size_t hidden_count = tsir_text_length(hidden_bound, hidden_length);
    bool found = false;
    tsir_buf_init(&path);
    if (tsir_text_starts_with(hidden_bound, hidden_length, "uniform:")) {
        for (size_t i = 0; i < hidden_count; i++) {
            if (hidden_bound[i] == '.') prefix_length = i + 1;
        }
    }
    if (prefix_length != 0) {
        tsir_buf_putn(&path, hidden_bound, prefix_length);
        tsir_buf_puts(&path, "eps");
    } else {
        tsir_buf_puts(&path, "uniform:u.eps");
    }
    if (path.err != TSIR_OK) {
        tsir_buf_free(&path);
        return -1;
    }
    rest = path.data + 8;
    dot = memchr(rest, '.', path.len - 8);
    if (dot == NULL || dot == rest || (size_t)(dot - path.data) + 1 >= path.len) {
        tsir_buf_free(&path);
        return 0;
    }
    binding_length = (size_t)(dot - rest);
    field_length = path.len - (size_t)(dot + 1 - path.data);
    for (size_t i = 0; i < binding_count; i++) {
        if (tsir_text_equal(bindings[i].name, bindings[i].name_length,
                rest, binding_length)) {
            *binding_out = (uint32_t)i;
            found = true;
            break;
        }
    }
    if (found && module != NULL) {
        for (size_t g = 0; g < module->global_count; g++) {
            const TsirIrGlobal *global = &module->globals[g];
            uint32_t cursor = 0;
            const TsirIrType *type;
            const TsirIrStructDef *definition;
            if (!tsir_text_equal(global->name, global->name_length, rest, binding_length)) continue;
            if (!tsir_ir_unwrap_ref(module, global->ty, &cursor) || cursor >= module->type_count) break;
            type = &module->types[cursor];
            if (type->kind != TSIR_IR_TYPE_STRUCT || type->struct_id >= module->struct_count) break;
            definition = &module->structs[type->struct_id];
            for (size_t f = 0; f < definition->field_count; f++) {
                const TsirIrStructField *field = &definition->fields[f];
                if (!tsir_text_equal(field->name, field->name_length, dot + 1, field_length)) continue;
                if (!tsir_ir_type_is_scalar(module, field->ty, TSIR_IR_F32) ||
                    !tsir_ir_struct_field_offset(module, definition, (uint32_t)f, offset_out)) break;
                *path_out = path.data;
                *path_length_out = path.len;
                return 1;
            }
            break;
        }
    }
    tsir_buf_free(&path);
    return 0;
}

TsirStatus tsir_frontend_infer_body(const TsirWgslModule *module,
    TsirKernelFamilyHint hint, const TsirIterationAxis *axes,
    size_t axis_count, const TsirBufferBinding *bindings,
    size_t binding_count, const TsirReductionRegion *reductions,
    size_t reduction_count, TsirSemanticBody *out) {
    if (out == NULL) {
        return TSIR_ERR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    out->op = TSIR_BODY_UNKNOWN;
    if (hint == TSIR_HINT_FUSED_GEMV && axis_count >= 2 &&
        binding_count >= 3 && reduction_count >= 1) {
        static const TsirSemanticBodyBinding roles[3] = {
            { 0, TSIR_ROLE_MATRIX },
            { 1, TSIR_ROLE_VECTOR },
            { 2, TSIR_ROLE_OUTPUT }
        };
        TsirSemanticBodyAxis axis_roles[2];
        axis_roles[0].axis_index = 0;
        axis_roles[0].role = TSIR_AXIS_OUTPUT;
        axis_roles[1].axis_index = reductions[0].axis;
        axis_roles[1].role = TSIR_AXIS_REDUCTION;
        out->op = TSIR_BODY_FUSED_GEMV;
        if (!make_roles(out, roles, 3, axis_roles, 2)) {
            return TSIR_ERR_NOMEM;
        }
        return TSIR_OK;
    }
    if (hint == TSIR_HINT_GATHER && axis_count >= 2 &&
        binding_count >= 3) {
        static const TsirSemanticBodyBinding roles[3] = {
            { 0, TSIR_ROLE_INDICES },
            { 1, TSIR_ROLE_TABLE },
            { 2, TSIR_ROLE_OUTPUT }
        };
        static const TsirSemanticBodyAxis axis_roles[2] = {
            { 0, TSIR_AXIS_TOKEN },
            { 1, TSIR_AXIS_HIDDEN }
        };
        out->op = TSIR_BODY_GATHER;
        if (!make_roles(out, roles, 3, axis_roles, 2)) {
            return TSIR_ERR_NOMEM;
        }
        return TSIR_OK;
    }
    if (axis_count >= 2 && binding_count >= 3 &&
        reduction_count == 1 && reductions[0].op == TSIR_REDUCTION_SUM &&
        reductions[0].axis < axis_count &&
        tsir_text_equal(axes[0].upper_bound, axes[0].upper_bound_length,
            axes[reductions[0].axis].upper_bound, axes[reductions[0].axis].upper_bound_length) &&
        name_equals_ci(bindings[0].name, bindings[0].name_length, "input") &&
        (name_equals_ci(bindings[1].name, bindings[1].name_length, "weight") ||
            name_equals_ci(bindings[1].name, bindings[1].name_length, "scale")) &&
        name_equals_ci(bindings[2].name, bindings[2].name_length, "output")) {
        char *path = NULL;
        size_t path_length = 0;
        uint32_t binding_index = 0;
        uint32_t offset = 0;
        int rc = infer_rms_epsilon(module, bindings, binding_count,
            axes[0].upper_bound, axes[0].upper_bound_length, &path,
            &path_length, &binding_index, &offset);
        if (rc == -1) {
            return TSIR_ERR_NOMEM;
        }
        if (rc == 0) {
            return TSIR_OK;
        }
        {
            static const TsirSemanticBodyBinding roles[3] = {
                { 0, TSIR_ROLE_INPUT },
                { 1, TSIR_ROLE_SCALE },
                { 2, TSIR_ROLE_OUTPUT }
            };
            TsirSemanticBodyAxis axis_roles[2];
            axis_roles[0].axis_index = 0;
            axis_roles[0].role = TSIR_AXIS_HIDDEN;
            axis_roles[1].axis_index = reductions[0].axis;
            axis_roles[1].role = TSIR_AXIS_REDUCTION;
            out->op = TSIR_BODY_RMS_NORM;
            if (!make_roles(out, roles, 3, axis_roles, 2)) {
                free(path);
                return TSIR_ERR_NOMEM;
            }
            out->has_rms_norm = true;
            out->rms_norm.formula =
                TSIR_RMSNORM_SUM_SQUARES_MEAN_EPSILON_RSQRT_SCALE;
            out->rms_norm.epsilon.source =
                TSIR_EPSILON_UNIFORM_FIELD;
            out->rms_norm.epsilon.path = path;
            out->rms_norm.epsilon.path_length = path_length;
            out->rms_norm.epsilon.binding_index.has = true;
            out->rms_norm.epsilon.binding_index.value = binding_index;
            out->rms_norm.epsilon.byte_offset.has = true;
            out->rms_norm.epsilon.byte_offset.value = offset;
            out->rms_norm.epsilon.has_literal_f64 = false;
            out->rms_norm.hidden_extent_axis = 0;
            out->rms_norm.reduction_target =
                TSIR_RMSNORM_INTERMEDIATE_SCALAR;
        }
        return TSIR_OK;
    }
    return TSIR_OK;
}
