/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_frontend.h"

#include "tsir_frontend_expr.h"
#include "tsir_frontend_internal.h"
#include "tsir_frontend_passes.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ---- binding extraction ---- */

static const char *type_tag_name(TsirIrTypeKind kind) {
    switch (kind) {
        case TSIR_IR_TYPE_SCALAR:
            return "scalar";
        case TSIR_IR_TYPE_VECTOR:
            return "vector";
        case TSIR_IR_TYPE_MATRIX:
            return "matrix";
        case TSIR_IR_TYPE_ARRAY:
            return "array";
        case TSIR_IR_TYPE_ATOMIC:
            return "atomic";
        case TSIR_IR_TYPE_STRUCT:
            return "struct_";
        case TSIR_IR_TYPE_REF:
            return "ref";
        default:
            return "other";
    }
}

static const char *addr_space_name(TsirIrAddrSpace space) {
    switch (space) {
        case TSIR_IR_SPACE_FUNCTION:
            return "function";
        case TSIR_IR_SPACE_PRIVATE:
            return "private";
        case TSIR_IR_SPACE_WORKGROUP:
            return "workgroup";
        case TSIR_IR_SPACE_UNIFORM:
            return "uniform";
        case TSIR_IR_SPACE_STORAGE:
            return "storage";
        default:
            return "handle";
    }
}

/* Returns 1 with *shape_out malloc'd (or NULL with len 0) and
 * *elem_out set, 0 when the type is unlowerable, -1 on NOMEM. */
static int extract_elem_and_shape(const TsirWgslModule *module,
    const TsirIrGlobal *global, uint64_t **shape_out,
    size_t *shape_len_out, TsirScalarKind *elem_out) {
    uint32_t cursor = 0;
    const TsirIrType *t = NULL;
    *shape_out = NULL;
    *shape_len_out = 0;
    if (!tsir_ir_unwrap_ref(module, global->ty, &cursor)) {
        return 0;
    }
    if (cursor >= module->type_count) {
        return 0;
    }
    t = &module->types[cursor];
    switch (t->kind) {
        case TSIR_IR_TYPE_SCALAR:
            *elem_out = tsir_ir_scalar_to_kind(t->scalar);
            return 1;
        case TSIR_IR_TYPE_ARRAY: {
            uint64_t *nested = NULL;
            size_t nested_len = 0;
            TsirScalarKind nested_elem = TSIR_SCALAR_F32;
            TsirIrGlobal synthetic;
            int rc = 0;
            uint64_t *shape = NULL;
            size_t i = 0;
            memset(&synthetic, 0, sizeof(synthetic));
            synthetic.name = "";
            synthetic.ty = t->elem;
            synthetic.class = TSIR_IR_GLOBAL_VAR;
            rc = extract_elem_and_shape(module, &synthetic, &nested,
                &nested_len, &nested_elem);
            if (rc != 1) {
                free(nested);
                return rc;
            }
            shape = (uint64_t *)malloc(
                (nested_len + 1) * sizeof(uint64_t));
            if (shape == NULL) {
                free(nested);
                return -1;
            }
            shape[0] = t->has_len ? t->array_len : 0;
            for (i = 0; i < nested_len; i++) {
                shape[i + 1] = nested[i];
            }
            free(nested);
            *shape_out = shape;
            *shape_len_out = nested_len + 1;
            *elem_out = nested_elem;
            return 1;
        }
        case TSIR_IR_TYPE_VECTOR: {
            uint64_t *nested = NULL;
            size_t nested_len = 0;
            TsirScalarKind nested_elem = TSIR_SCALAR_F32;
            TsirIrGlobal synthetic;
            int rc = 0;
            uint64_t *shape = NULL;
            memset(&synthetic, 0, sizeof(synthetic));
            synthetic.name = "";
            synthetic.ty = t->elem;
            synthetic.class = TSIR_IR_GLOBAL_VAR;
            rc = extract_elem_and_shape(module, &synthetic, &nested,
                &nested_len, &nested_elem);
            if (rc != 1) {
                free(nested);
                return rc;
            }
            if (nested_len != 0) {
                free(nested);
                return 0;
            }
            free(nested);
            shape = (uint64_t *)malloc(sizeof(uint64_t));
            if (shape == NULL) {
                return -1;
            }
            shape[0] = t->len;
            *shape_out = shape;
            *shape_len_out = 1;
            *elem_out = nested_elem;
            return 1;
        }
        case TSIR_IR_TYPE_MATRIX: {
            uint64_t *nested = NULL;
            size_t nested_len = 0;
            TsirScalarKind nested_elem = TSIR_SCALAR_F32;
            TsirIrGlobal synthetic;
            int rc = 0;
            uint64_t *shape = NULL;
            memset(&synthetic, 0, sizeof(synthetic));
            synthetic.name = "";
            synthetic.ty = t->elem;
            synthetic.class = TSIR_IR_GLOBAL_VAR;
            rc = extract_elem_and_shape(module, &synthetic, &nested,
                &nested_len, &nested_elem);
            if (rc != 1) {
                free(nested);
                return rc;
            }
            if (nested_len != 0) {
                free(nested);
                return 0;
            }
            free(nested);
            shape = (uint64_t *)malloc(2 * sizeof(uint64_t));
            if (shape == NULL) {
                return -1;
            }
            shape[0] = t->rows;
            shape[1] = t->columns;
            *shape_out = shape;
            *shape_len_out = 2;
            *elem_out = nested_elem;
            return 1;
        }
        case TSIR_IR_TYPE_STRUCT: {
            uint32_t byte_size = 0;
            uint64_t *shape = NULL;
            if (!global->has_addr_space ||
                global->addr_space != TSIR_IR_SPACE_UNIFORM) {
                return 0;
            }
            if (!tsir_ir_type_size(module, cursor, &byte_size)) {
                return 0;
            }
            if (byte_size == 0 || byte_size % 4 != 0) {
                return 0;
            }
            shape = (uint64_t *)malloc(sizeof(uint64_t));
            if (shape == NULL) {
                return -1;
            }
            shape[0] = byte_size / 4;
            *shape_out = shape;
            *shape_len_out = 1;
            *elem_out = TSIR_SCALAR_U32;
            return 1;
        }
        default:
            return 0;
    }
}

TsirStatus tsir_frontend_extract_bindings(const TsirWgslModule *module,
    const TsirIrFunction *func, uint32_t func_index,
    TsirBindingList *out, TsirRejectionList *rejections) {
    bool *referenced = NULL;
    size_t gi = 0;
    size_t ei = 0;
    TsirStatus status = TSIR_OK;
    if (module->global_count > 0) {
        referenced = (bool *)calloc(module->global_count,
            sizeof(bool));
        if (referenced == NULL) {
            return TSIR_ERR_NOMEM;
        }
    }
    for (ei = 0; ei < func->expr_count; ei++) {
        const TsirIrExpr *expr = &func->exprs[ei];
        if (expr->kind == TSIR_IR_EXPR_GLOBAL_REF &&
            expr->ref_index < module->global_count) {
            referenced[expr->ref_index] = true;
        }
    }
    for (gi = 0; gi < module->global_count; gi++) {
        const TsirIrGlobal *global = &module->globals[gi];
        uint64_t *shape = NULL;
        size_t shape_len = 0;
        TsirScalarKind elem = TSIR_SCALAR_F32;
        int rc = 0;
        TsirBufferBinding binding;
        char *name_copy = NULL;
        if (!global->has_binding) {
            continue;
        }
        if (!referenced[gi]) {
            continue;
        }
        rc = extract_elem_and_shape(module, global, &shape,
            &shape_len, &elem);
        if (rc == -1) {
            status = TSIR_ERR_NOMEM;
            break;
        }
        if (rc == 0) {
            char path[256];
            char detail[256];
            uint32_t cursor = 0;
            const TsirIrType *t = NULL;
            const char *tag = "other";
            free(shape);
            if (tsir_ir_unwrap_ref(module, global->ty, &cursor) &&
                cursor < module->type_count) {
                t = &module->types[cursor];
                tag = type_tag_name(t->kind);
            }
            snprintf(path, sizeof(path),
                "functions[%u].bindings[%u:%u]", func_index,
                global->group, global->binding);
            if (t != NULL && t->kind == TSIR_IR_TYPE_STRUCT) {
                snprintf(detail, sizeof(detail), "%s_%s",
                    global->has_addr_space
                        ? addr_space_name(global->addr_space)
                        : "none",
                    tag);
            } else {
                snprintf(detail, sizeof(detail), "%s", tag);
            }
            if (!tsir_frontend_rejection_append(rejections,
                TSIR_REJECT_BINDING_TYPE_UNLOWERABLE, path,
                detail)) {
                status = TSIR_ERR_NOMEM;
            }
            if (status != TSIR_OK) {
                break;
            }
            continue;
        }
        name_copy = tsir_frontend_dup_str(global->name);
        if (name_copy == NULL) {
            free(shape);
            status = TSIR_ERR_NOMEM;
            break;
        }
        memset(&binding, 0, sizeof(binding));
        binding.name = name_copy;
        binding.group = global->group;
        binding.binding = global->binding;
        binding.logical_shape = shape;
        binding.logical_shape_len = shape_len;
        binding.elem = elem;
        binding.read_write = global->has_access &&
            global->access == TSIR_IR_ACCESS_READ_WRITE;
        if (!tsir_frontend_binding_append(out, binding,
            (uint32_t)gi)) {
            free(name_copy);
            free(shape);
            status = TSIR_ERR_NOMEM;
            break;
        }
    }
    free(referenced);
    return status;
}

