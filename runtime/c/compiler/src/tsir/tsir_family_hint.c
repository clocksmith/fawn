/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_frontend.h"

#include <stdlib.h>
#include <string.h>

/* The three retained family-hint predicates share expression edges, not
 * language semantics. An explicit DFS stack and completed-node memo remove
 * the old 512-level truncation and exponential revisits of shared DAGs. */
typedef enum HintQueryKind {
    HINT_LOCAL,
    HINT_INDEX,
    HINT_INDIRECT
} HintQueryKind;

typedef struct HintFrame {
    uint32_t expression;
    uint32_t edge;
    uint32_t alias_depth;
} HintFrame;

typedef struct HintQuery {
    const TsirIrFunction *function;
    HintQueryKind kind;
    uint32_t local;
    unsigned char *states; /* 0 unseen, 1 active, 2 false, 3 true */
    HintFrame *stack;
    size_t slots;
} HintQuery;

static void query_close(HintQuery *query) {
    free(query->states);
    free(query->stack);
    memset(query, 0, sizeof(*query));
}

static TsirStatus query_open(HintQuery *query, const TsirIrFunction *function,
    HintQueryKind kind, uint32_t local) {
    size_t levels = kind == HINT_INDIRECT ? 9u : 1u;
    memset(query, 0, sizeof(*query));
    query->function = function;
    query->kind = kind;
    query->local = local;
    if (function->expr_count > UINT32_MAX ||
        function->expr_count > SIZE_MAX / levels / sizeof(HintFrame)) {
        return TSIR_ERR_OVERFLOW;
    }
    query->slots = function->expr_count * levels;
    if (query->slots == 0) {
        return TSIR_OK;
    }
    query->states = calloc(query->slots, sizeof(*query->states));
    query->stack = malloc(query->slots * sizeof(*query->stack));
    if (query->states == NULL || query->stack == NULL) {
        query_close(query);
        return TSIR_ERR_NOMEM;
    }
    return TSIR_OK;
}

static TsirStatus query_edge(const HintQuery *query, const HintFrame *frame,
    HintFrame *child, bool *present, bool *matched) {
    const TsirIrFunction *function = query->function;
    const TsirIrExpr *node = &function->exprs[frame->expression];
    *present = false;
    *matched = false;
    *child = (HintFrame){0, 0, frame->alias_depth};
    switch (node->kind) {
        case TSIR_IR_EXPR_LOCAL_REF:
            if (query->kind == HINT_LOCAL) {
                *matched = node->ref_index == query->local;
            } else if (query->kind == HINT_INDIRECT && frame->alias_depth < 8u && frame->edge == 0) {
                for (size_t i = 0; i < function->stmt_count; i++) {
                    const TsirIrStmt *statement = &function->stmts[i];
                    if (statement->kind != TSIR_IR_STMT_LOCAL_DECL || statement->local != node->ref_index) {
                        continue;
                    }
                    if (statement->has_initializer) {
                        child->expression = statement->initializer;
                        child->alias_depth += 1;
                        *present = true;
                    }
                    break;
                }
            }
            break;
        case TSIR_IR_EXPR_INDEX:
            if (query->kind != HINT_LOCAL) {
                *matched = true;
            } else if (frame->edge < 2u) {
                child->expression = frame->edge == 0 ? node->index_base : node->index_index;
                *present = true;
            }
            break;
        case TSIR_IR_EXPR_LOAD:
            child->expression = node->inner;
            *present = frame->edge == 0;
            break;
        case TSIR_IR_EXPR_BINARY:
            child->expression = frame->edge == 0 ? node->binary_lhs : node->binary_rhs;
            *present = frame->edge < 2u;
            break;
        case TSIR_IR_EXPR_UNARY:
            child->expression = node->unary_operand;
            *present = frame->edge == 0;
            break;
        case TSIR_IR_EXPR_MEMBER:
            child->expression = node->member_base;
            *present = frame->edge == 0;
            break;
        case TSIR_IR_EXPR_CALL:
            if (node->args_start > function->expr_arg_count ||
                node->args_len > function->expr_arg_count - node->args_start) {
                return TSIR_ERR_INVALID;
            }
            if (frame->edge < node->args_len) {
                child->expression = function->expr_args[node->args_start + frame->edge];
                *present = true;
            }
            break;
        case TSIR_IR_EXPR_BOOL_LIT:
        case TSIR_IR_EXPR_INT_LIT:
        case TSIR_IR_EXPR_FLOAT_LIT:
        case TSIR_IR_EXPR_PARAM_REF:
        case TSIR_IR_EXPR_GLOBAL_REF:
        case TSIR_IR_EXPR_ADDRESS_OF:
        case TSIR_IR_EXPR_DEREF:
        case TSIR_IR_EXPR_CONSTRUCT:
            break;
        default:
            return TSIR_ERR_INVALID;
    }
    if (*present && child->expression >= function->expr_count) {
        return TSIR_ERR_INVALID;
    }
    return TSIR_OK;
}

static size_t frame_slot(const HintQuery *query, const HintFrame *frame) {
    return (size_t)frame->alias_depth * query->function->expr_count + frame->expression;
}

static TsirStatus query_run(HintQuery *query, uint32_t expression, bool *out) {
    size_t depth = 1;
    if (expression >= query->function->expr_count) {
        return TSIR_ERR_INVALID;
    }
    if (query->states[expression] >= 2u) {
        *out = query->states[expression] == 3u;
        return TSIR_OK;
    }
    query->stack[0] = (HintFrame){expression, 0, 0};
    query->states[expression] = 1;
    while (depth != 0) {
        HintFrame *frame = &query->stack[depth - 1];
        size_t slot = frame_slot(query, frame);
        HintFrame child;
        bool present;
        bool matched;
        TsirStatus status = query_edge(query, frame, &child, &present, &matched);
        if (status != TSIR_OK) {
            return status;
        }
        if (matched || !present) {
            query->states[slot] = matched ? 3u : 2u;
            depth -= 1;
            continue;
        }
        size_t child_slot = frame_slot(query, &child);
        if (query->states[child_slot] == 1u) {
            return TSIR_ERR_INVALID;
        }
        if (query->states[child_slot] == 3u) {
            query->states[slot] = 3;
            depth -= 1;
        } else if (query->states[child_slot] == 2u) {
            frame->edge += 1;
        } else {
            if (depth == query->slots) {
                return TSIR_ERR_INVALID;
            }
            query->states[child_slot] = 1;
            query->stack[depth++] = child;
        }
    }
    *out = query->states[expression] == 3u;
    return TSIR_OK;
}

static bool local_named(const TsirIrFunction *function, const char *name, size_t length, uint32_t *out) {
    if (name == NULL) {
        return false;
    }
    for (size_t i = 0; i < function->local_count; i++) {
        if (function->locals[i].name != NULL && tsir_text_equal(function->locals[i].name, function->locals[i].name_length, name, length)) {
            *out = (uint32_t)i;
            return true;
        }
    }
    return false;
}

static TsirStatus covering_axes(const TsirIrFunction *function,
    const TsirIterationAxis *axes, bool *out) {
    HintQuery queries[3] = {0};
    uint32_t outer;
    uint32_t inner;
    TsirStatus status = TSIR_OK;
    *out = false;
    if (!local_named(function, axes[0].name, axes[0].name_length, &outer) ||
        !local_named(function, axes[1].name, axes[1].name_length, &inner)) {
        return TSIR_OK;
    }
    status = query_open(&queries[0], function, HINT_INDEX, 0);
    if (status == TSIR_OK) status = query_open(&queries[1], function, HINT_LOCAL, outer);
    if (status == TSIR_OK) status = query_open(&queries[2], function, HINT_LOCAL, inner);
    for (size_t i = 0; status == TSIR_OK && i < function->expr_count; i++) {
        const TsirIrExpr *node = &function->exprs[i];
        bool matched = false;
        uint32_t global;
        if (node->kind != TSIR_IR_EXPR_LOAD) continue;
        status = query_run(&queries[0], node->inner, &matched);
        if (status != TSIR_OK || !matched || !tsir_ir_find_global_base(function, node->inner, &global)) continue;
        status = query_run(&queries[1], node->inner, &matched);
        if (status != TSIR_OK || !matched) continue;
        status = query_run(&queries[2], node->inner, &matched);
        if (status == TSIR_OK && matched) {
            *out = true;
            break;
        }
    }
    for (size_t i = 0; i < 3u; i++) query_close(&queries[i]);
    return status;
}

static TsirStatus indirect_access(const TsirIrFunction *function, bool *out) {
    HintQuery query;
    TsirStatus status = query_open(&query, function, HINT_INDIRECT, 0);
    *out = false;
    for (size_t i = 0; status == TSIR_OK && i < function->expr_count; i++) {
        if (function->exprs[i].kind != TSIR_IR_EXPR_INDEX) continue;
        status = query_run(&query, function->exprs[i].index_index, out);
        if (*out) break;
    }
    query_close(&query);
    return status;
}

static bool nontrivial_bound(const char *text, size_t length) {
    return text != NULL && (tsir_text_starts_with(text, length, "dispatch.") ||
        tsir_text_starts_with(text, length, "uniform:") || tsir_text_starts_with(text, length, "override:") ||
        tsir_text_starts_with(text, length, "override@id:") || tsir_text_starts_with(text, length, "const:"));
}

TsirStatus tsir_family_hint_infer_ir(const TsirIrFunction *function,
    const TsirIterationAxis *axes, size_t axis_count,
    const TsirReductionRegion *reductions, size_t reduction_count,
    TsirKernelFamilyHint *out) {
    TsirKernelFamilyHint hint = TSIR_HINT_UNKNOWN;
    TsirStatus status = TSIR_OK;
    bool matched = false;
    if (function == NULL || out == NULL ||
        (axis_count != 0 && axes == NULL) || (reduction_count != 0 && reductions == NULL) ||
        (function->expr_count != 0 && function->exprs == NULL) ||
        (function->local_count != 0 && function->locals == NULL) ||
        (function->stmt_count != 0 && function->stmts == NULL) ||
        (function->expr_arg_count != 0 && function->expr_args == NULL)) {
        return TSIR_ERR_INVALID;
    }
    if (function->local_count > UINT32_MAX) return TSIR_ERR_OVERFLOW;
    if (reduction_count != 0) {
        hint = TSIR_HINT_REDUCTION;
        if (axis_count == 2 && reduction_count == 1 && reductions[0].axis == 1 &&
            reductions[0].op == TSIR_REDUCTION_SUM && nontrivial_bound(axes[0].upper_bound, axes[0].upper_bound_length)) {
            status = covering_axes(function, axes, &matched);
            if (matched) hint = TSIR_HINT_FUSED_GEMV;
        }
    } else if (axis_count != 0) {
        status = indirect_access(function, &matched);
        hint = matched ? TSIR_HINT_GATHER : TSIR_HINT_ELEMENTWISE;
    }
    if (status == TSIR_OK) *out = hint;
    return status;
}
