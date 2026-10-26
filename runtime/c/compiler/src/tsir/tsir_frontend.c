/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_frontend.h"

#include "tsir_frontend_expr.h"
#include "tsir_frontend_internal.h"
#include "tsir_frontend_passes.h"

#include <stdlib.h>
#include <string.h>

char *tsir_frontend_dup_str(const char *text) {
    return tsir_text_duplicate(text, 0);
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

bool tsir_frontend_rejection_append_span(TsirRejectionList *list,
    TsirRejectionReason reason, const char *node_path, size_t node_path_length,
    const char *detail, size_t detail_length) {
    char *path_copy = NULL;
    char *detail_copy = NULL;
    TsirRejectionEntry entry = {0};
    if (node_path == NULL || detail == NULL) {
        return false;
    }
    path_copy = tsir_text_duplicate(node_path, node_path_length);
    detail_copy = tsir_text_duplicate(detail, detail_length);
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
    entry.node_path_length = node_path_length;
    entry.detail = detail_copy;
    entry.detail_length = detail_length;
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


bool tsir_frontend_rejection_append(TsirRejectionList *list,
    TsirRejectionReason reason, const char *node_path, const char *detail) {
    return tsir_frontend_rejection_append_span(list, reason, node_path, 0, detail, 0);
}
