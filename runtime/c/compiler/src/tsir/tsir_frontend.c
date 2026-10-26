/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_frontend.h"

#include "tsir_frontend_expr.h"
#include "tsir_frontend_internal.h"
#include "tsir_frontend_passes.h"

#include <stdlib.h>
#include <string.h>

char *tsir_frontend_dup_str(const char *text) {
    size_t len = 0;
    char *copy = NULL;
    if (text == NULL) {
        return NULL;
    }
    len = strlen(text);
    copy = (char *)malloc(len + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, len + 1);
    return copy;
}

TsirScalarKind tsir_ir_scalar_to_kind(TsirIrScalar scalar) {
    switch (scalar) {
        case TSIR_IR_F32:
            return TSIR_SCALAR_F32;
        case TSIR_IR_F16:
            return TSIR_SCALAR_F16;
        case TSIR_IR_I32:
            return TSIR_SCALAR_I32;
        case TSIR_IR_U32:
            return TSIR_SCALAR_U32;
        case TSIR_IR_ABSTRACT_INT:
            return TSIR_SCALAR_I32;
        case TSIR_IR_ABSTRACT_FLOAT:
            return TSIR_SCALAR_F32;
        case TSIR_IR_BOOL:
            return TSIR_SCALAR_U32;
        case TSIR_IR_VOID:
            return TSIR_SCALAR_U32;
        default:
            return TSIR_SCALAR_U32;
    }
}

TsirKernelFamilyHint tsir_family_hint_classify(size_t axis_count,
    size_t reduction_count, bool fused_gemv_evidence,
    bool indirect_access) {
    if (reduction_count > 0) {
        if (fused_gemv_evidence) {
            return TSIR_HINT_FUSED_GEMV;
        }
        return TSIR_HINT_REDUCTION;
    }
    if (axis_count > 0) {
        if (indirect_access) {
            return TSIR_HINT_GATHER;
        }
        return TSIR_HINT_ELEMENTWISE;
    }
    return TSIR_HINT_UNKNOWN;
}

/* ---- lists ---- */

static bool grow_array(void **items, size_t *cap, size_t elem_size,
    size_t count) {
    size_t new_cap = 0;
    void *grown = NULL;
    if (count < *cap) {
        return true;
    }
    new_cap = (*cap == 0) ? 8 : (*cap * 2);
    grown = realloc(*items, new_cap * elem_size);
    if (grown == NULL) {
        return false;
    }
    *items = grown;
    *cap = new_cap;
    return true;
}

bool tsir_frontend_axis_append(TsirAxisList *list,
    TsirIterationAxis axis) {
    if (!grow_array((void **)&list->items, &list->cap,
        sizeof(TsirIterationAxis), list->count)) {
        return false;
    }
    list->items[list->count] = axis;
    list->count += 1;
    return true;
}

bool tsir_frontend_binding_append(TsirBindingList *list,
    TsirBufferBinding binding, uint32_t global_index) {
    if (!grow_array((void **)&list->items, &list->cap,
        sizeof(TsirBufferBinding), list->count)) {
        return false;
    }
    if (!grow_array((void **)&list->global_indices,
        &list->global_cap, sizeof(uint32_t), list->global_count)) {
        return false;
    }
    list->items[list->count] = binding;
    list->count += 1;
    list->global_indices[list->global_count] = global_index;
    list->global_count += 1;
    return true;
}

bool tsir_frontend_reduction_append(TsirReductionList *list,
    TsirReductionRegion reduction) {
    if (!grow_array((void **)&list->items, &list->cap,
        sizeof(TsirReductionRegion), list->count)) {
        return false;
    }
    list->items[list->count] = reduction;
    list->count += 1;
    return true;
}

bool tsir_frontend_collective_append(TsirCollectiveList *list,
    TsirCollectiveSemanticNode node) {
    if (!grow_array((void **)&list->items, &list->cap,
        sizeof(TsirCollectiveSemanticNode), list->count)) {
        return false;
    }
    list->items[list->count] = node;
    list->count += 1;
    return true;
}

bool tsir_frontend_rejection_append(TsirRejectionList *list,
    TsirRejectionReason reason, const char *node_path,
    const char *detail) {
    char *path_copy = NULL;
    char *detail_copy = NULL;
    TsirRejectionEntry entry;
    if (node_path == NULL || detail == NULL) {
        return false;
    }
    path_copy = tsir_frontend_dup_str(node_path);
    detail_copy = tsir_frontend_dup_str(detail);
    if (path_copy == NULL || detail_copy == NULL) {
        free(path_copy);
        free(detail_copy);
        return false;
    }
    if (!grow_array((void **)&list->items, &list->cap,
        sizeof(TsirRejectionEntry), list->count)) {
        free(path_copy);
        free(detail_copy);
        return false;
    }
    entry.reason = reason;
    entry.node_path = path_copy;
    entry.detail = detail_copy;
    list->items[list->count] = entry;
    list->count += 1;
    return true;
}

void tsir_frontend_axis_free(TsirAxisList *list) {
    size_t i = 0;
    if (list == NULL) {
        return;
    }
    for (i = 0; i < list->count; i++) {
        free((void *)list->items[i].name);
        free((void *)list->items[i].lower_bound);
        free((void *)list->items[i].upper_bound);
        free((void *)list->items[i].step);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

void tsir_frontend_binding_free(TsirBindingList *list) {
    size_t i = 0;
    if (list == NULL) {
        return;
    }
    for (i = 0; i < list->count; i++) {
        free((void *)list->items[i].name);
        free((void *)list->items[i].logical_shape);
    }
    free(list->items);
    free(list->global_indices);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
    list->global_indices = NULL;
    list->global_count = 0;
    list->global_cap = 0;
}

void tsir_frontend_reduction_free(TsirReductionList *list) {
    if (list == NULL) {
        return;
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

void tsir_frontend_collective_free(TsirCollectiveList *list) {
    if (list == NULL) {
        return;
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

void tsir_frontend_rejection_free(TsirRejectionList *list) {
    size_t i = 0;
    if (list == NULL) {
        return;
    }
    for (i = 0; i < list->count; i++) {
        free((void *)list->items[i].node_path);
        free((void *)list->items[i].detail);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

/* ---- family hint IR walk ---- */

static bool find_local_by_name(const TsirIrFunction *func,
    const char *name, uint32_t *out) {
    size_t i = 0;
    for (i = 0; i < func->local_count; i++) {
        if (strcmp(func->locals[i].name, name) == 0) {
            *out = (uint32_t)i;
            return true;
        }
    }
    return false;
}

static const TsirIrExpr *fexpr(const TsirIrFunction *func,
    uint32_t id) {
    if (id >= func->expr_count) {
        return NULL;
    }
    return &func->exprs[id];
}

static bool tree_contains_local(const TsirIrFunction *func,
    uint32_t id, uint32_t local_index, uint32_t depth) {
    const TsirIrExpr *node = fexpr(func, id);
    uint32_t i = 0;
    if (node == NULL || depth > 512) {
        return false;
    }
    switch (node->kind) {
        case TSIR_IR_EXPR_LOCAL_REF:
            return node->ref_index == local_index;
        case TSIR_IR_EXPR_LOAD:
            return tree_contains_local(func, node->inner,
                local_index, depth + 1);
        case TSIR_IR_EXPR_BINARY:
            return tree_contains_local(func, node->binary_lhs,
                    local_index, depth + 1) ||
                tree_contains_local(func, node->binary_rhs,
                    local_index, depth + 1);
        case TSIR_IR_EXPR_UNARY:
            return tree_contains_local(func, node->unary_operand,
                local_index, depth + 1);
        case TSIR_IR_EXPR_MEMBER:
            return tree_contains_local(func, node->member_base,
                local_index, depth + 1);
        case TSIR_IR_EXPR_INDEX:
            return tree_contains_local(func, node->index_base,
                    local_index, depth + 1) ||
                tree_contains_local(func, node->index_index,
                    local_index, depth + 1);
        case TSIR_IR_EXPR_CALL:
            for (i = 0; i < node->args_len; i++) {
                if (node->args_start + i >= func->expr_arg_count) {
                    return false;
                }
                if (tree_contains_local(func,
                    func->expr_args[node->args_start + i],
                    local_index, depth + 1)) {
                    return true;
                }
            }
            return false;
        case TSIR_IR_EXPR_BOOL_LIT:
        case TSIR_IR_EXPR_INT_LIT:
        case TSIR_IR_EXPR_FLOAT_LIT:
        case TSIR_IR_EXPR_PARAM_REF:
        case TSIR_IR_EXPR_GLOBAL_REF:
        case TSIR_IR_EXPR_ADDRESS_OF:
        case TSIR_IR_EXPR_DEREF:
        case TSIR_IR_EXPR_CONSTRUCT:
        default:
            return false;
    }
}

static bool tree_contains_index(const TsirIrFunction *func,
    uint32_t id, uint32_t depth) {
    const TsirIrExpr *node = fexpr(func, id);
    uint32_t i = 0;
    if (node == NULL || depth > 512) {
        return false;
    }
    switch (node->kind) {
        case TSIR_IR_EXPR_INDEX:
            return true;
        case TSIR_IR_EXPR_LOAD:
            return tree_contains_index(func, node->inner,
                depth + 1);
        case TSIR_IR_EXPR_BINARY:
            return tree_contains_index(func, node->binary_lhs,
                    depth + 1) ||
                tree_contains_index(func, node->binary_rhs,
                    depth + 1);
        case TSIR_IR_EXPR_UNARY:
            return tree_contains_index(func, node->unary_operand,
                depth + 1);
        case TSIR_IR_EXPR_MEMBER:
            return tree_contains_index(func, node->member_base,
                depth + 1);
        case TSIR_IR_EXPR_CALL:
            for (i = 0; i < node->args_len; i++) {
                if (node->args_start + i >= func->expr_arg_count) {
                    return false;
                }
                if (tree_contains_index(func,
                    func->expr_args[node->args_start + i],
                    depth + 1)) {
                    return true;
                }
            }
            return false;
        case TSIR_IR_EXPR_BOOL_LIT:
        case TSIR_IR_EXPR_INT_LIT:
        case TSIR_IR_EXPR_FLOAT_LIT:
        case TSIR_IR_EXPR_PARAM_REF:
        case TSIR_IR_EXPR_GLOBAL_REF:
        case TSIR_IR_EXPR_ADDRESS_OF:
        case TSIR_IR_EXPR_DEREF:
        case TSIR_IR_EXPR_CONSTRUCT:
        case TSIR_IR_EXPR_LOCAL_REF:
        default:
            return false;
    }
}

static bool local_init_contains_buffer_index(
    const TsirIrFunction *func, uint32_t local_index,
    uint32_t depth);

static bool index_field_contains(const TsirIrFunction *func,
    uint32_t id, uint32_t depth) {
    const TsirIrExpr *node = fexpr(func, id);
    uint32_t i = 0;
    if (node == NULL || depth > 512) {
        return false;
    }
    switch (node->kind) {
        case TSIR_IR_EXPR_INDEX:
            return true;
        case TSIR_IR_EXPR_LOCAL_REF:
            return local_init_contains_buffer_index(func,
                node->ref_index, depth + 1);
        case TSIR_IR_EXPR_LOAD:
            return index_field_contains(func, node->inner, depth);
        case TSIR_IR_EXPR_BINARY:
            return index_field_contains(func, node->binary_lhs,
                    depth) ||
                index_field_contains(func, node->binary_rhs, depth);
        case TSIR_IR_EXPR_UNARY:
            return index_field_contains(func, node->unary_operand,
                depth);
        case TSIR_IR_EXPR_MEMBER:
            return index_field_contains(func, node->member_base,
                depth);
        case TSIR_IR_EXPR_CALL:
            for (i = 0; i < node->args_len; i++) {
                if (node->args_start + i >= func->expr_arg_count) {
                    return false;
                }
                if (index_field_contains(func,
                    func->expr_args[node->args_start + i], depth)) {
                    return true;
                }
            }
            return false;
        case TSIR_IR_EXPR_BOOL_LIT:
        case TSIR_IR_EXPR_INT_LIT:
        case TSIR_IR_EXPR_FLOAT_LIT:
        case TSIR_IR_EXPR_PARAM_REF:
        case TSIR_IR_EXPR_GLOBAL_REF:
        case TSIR_IR_EXPR_ADDRESS_OF:
        case TSIR_IR_EXPR_DEREF:
        case TSIR_IR_EXPR_CONSTRUCT:
        default:
            return false;
    }
}

static bool local_init_contains_buffer_index(
    const TsirIrFunction *func, uint32_t local_index,
    uint32_t depth) {
    size_t i = 0;
    if (depth > 8) {
        return false;
    }
    for (i = 0; i < func->stmt_count; i++) {
        const TsirIrStmt *stmt = &func->stmts[i];
        if (stmt->kind != TSIR_IR_STMT_LOCAL_DECL) {
            continue;
        }
        if (stmt->local != local_index) {
            continue;
        }
        if (!stmt->has_initializer) {
            return false;
        }
        return index_field_contains(func, stmt->initializer, depth);
    }
    return false;
}

static bool has_indexed_covering(const TsirIrFunction *func,
    const char *outer_name, const char *inner_name) {
    uint32_t outer_local = 0;
    uint32_t inner_local = 0;
    uint32_t global_base = 0;
    size_t i = 0;
    if (!find_local_by_name(func, outer_name, &outer_local)) {
        return false;
    }
    if (!find_local_by_name(func, inner_name, &inner_local)) {
        return false;
    }
    for (i = 0; i < func->expr_count; i++) {
        const TsirIrExpr *expr = &func->exprs[i];
        if (expr->kind != TSIR_IR_EXPR_LOAD) {
            continue;
        }
        if (!tree_contains_index(func, expr->inner, 0)) {
            continue;
        }
        if (!tsir_ir_find_global_base(func, expr->inner,
            &global_base)) {
            continue;
        }
        if (tree_contains_local(func, expr->inner, outer_local, 0) &&
            tree_contains_local(func, expr->inner, inner_local, 0)) {
            return true;
        }
    }
    return false;
}

static bool has_indirect_access(const TsirIrFunction *func) {
    size_t i = 0;
    for (i = 0; i < func->expr_count; i++) {
        const TsirIrExpr *expr = &func->exprs[i];
        if (expr->kind != TSIR_IR_EXPR_INDEX) {
            continue;
        }
        if (index_field_contains(func, expr->index_index, 0)) {
            return true;
        }
    }
    return false;
}

static bool nontrivial_bound(const char *s) {
    return strncmp(s, "dispatch.", 9) == 0 ||
        strncmp(s, "uniform:", 8) == 0 ||
        strncmp(s, "override:", 9) == 0 ||
        strncmp(s, "override@id:", 12) == 0 ||
        strncmp(s, "const:", 6) == 0;
}

TsirKernelFamilyHint tsir_family_hint_infer_ir(
    const TsirIrFunction *func, const TsirIterationAxis *axes,
    size_t axis_count, const TsirReductionRegion *reductions,
    size_t reduction_count) {
    if (func == NULL) {
        return TSIR_HINT_UNKNOWN;
    }
    if (reduction_count > 0) {
        bool fused = false;
        if (axis_count == 2 && reduction_count == 1 &&
            reductions[0].axis == 1 &&
            reductions[0].op == TSIR_REDUCTION_SUM &&
            axes != NULL && nontrivial_bound(axes[0].upper_bound) &&
            has_indexed_covering(func, axes[0].name,
                axes[1].name)) {
            fused = true;
        }
        return fused ? TSIR_HINT_FUSED_GEMV : TSIR_HINT_REDUCTION;
    }
    if (axis_count > 0) {
        return has_indirect_access(func) ? TSIR_HINT_GATHER
            : TSIR_HINT_ELEMENTWISE;
    }
    return TSIR_HINT_UNKNOWN;
}

void tsir_frontend_free_function_contents(TsirSemanticFunction *func) {
    size_t i = 0;
    free((void *)func->name);
    for (i = 0; i < func->axis_count; i++) {
        free((void *)func->axes[i].name);
        free((void *)func->axes[i].lower_bound);
        free((void *)func->axes[i].upper_bound);
        free((void *)func->axes[i].step);
    }
    free((void *)func->axes);
    for (i = 0; i < func->binding_count; i++) {
        free((void *)func->bindings[i].name);
        free((void *)func->bindings[i].logical_shape);
    }
    free((void *)func->bindings);
    free((void *)func->reductions);
    free((void *)func->collectives);
    free((void *)func->body.binding_roles);
    free((void *)func->body.axis_roles);
    free((void *)func->body.rms_norm.epsilon.path);
}

void tsir_frontend_free_semantic(TsirSemantic *semantic) {
    size_t i = 0;
    size_t r = 0;
    if (semantic == NULL) {
        return;
    }
    for (i = 0; i < semantic->function_count; i++) {
        tsir_frontend_free_function_contents(
            (TsirSemanticFunction *)&semantic->functions[i]);
    }
    free((void *)semantic->functions);
    for (r = 0; r < semantic->rejection_count; r++) {
        free((void *)semantic->rejections[r].node_path);
        free((void *)semantic->rejections[r].detail);
    }
    free((void *)semantic->rejections);
    free((void *)semantic->frontend_version);
    memset(semantic, 0, sizeof(*semantic));
}

