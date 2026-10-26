/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_wgsl_ir.h"

static const TsirIrType *get_type(const TsirWgslModule *module,
    uint32_t ty) {
    if (module == NULL || ty >= module->type_count) {
        return NULL;
    }
    return &module->types[ty];
}

static const TsirIrExpr *get_expr(const TsirIrFunction *func,
    uint32_t id) {
    if (func == NULL || id >= func->expr_count) {
        return NULL;
    }
    return &func->exprs[id];
}

static uint32_t round_up_u32(uint32_t value, uint32_t alignment,
    bool *ok) {
    uint32_t remainder = 0;
    if (alignment <= 1) {
        return value;
    }
    remainder = value % alignment;
    if (remainder == 0) {
        return value;
    }
    if (value > UINT32_MAX - (alignment - remainder)) {
        *ok = false;
        return 0;
    }
    return value + (alignment - remainder);
}

static uint32_t lane_align(uint32_t count) {
    if (count == 2) {
        return 2;
    }
    if (count == 3 || count == 4) {
        return 4;
    }
    return 1;
}

bool tsir_ir_unwrap_ref(const TsirWgslModule *module, uint32_t ty,
    uint32_t *out) {
    const TsirIrType *t = get_type(module, ty);
    if (t == NULL || out == NULL) {
        return false;
    }
    if (t->kind == TSIR_IR_TYPE_REF) {
        *out = t->elem;
    } else {
        *out = ty;
    }
    return true;
}

bool tsir_ir_type_is_scalar(const TsirWgslModule *module, uint32_t ty,
    TsirIrScalar scalar) {
    const TsirIrType *t = get_type(module, ty);
    if (t == NULL) {
        return false;
    }
    return t->kind == TSIR_IR_TYPE_SCALAR && t->scalar == scalar;
}

bool tsir_ir_type_alignment(const TsirWgslModule *module, uint32_t ty,
    uint32_t *out) {
    const TsirIrType *t = get_type(module, ty);
    uint32_t inner = 0;
    uint32_t max_align = 1;
    size_t i = 0;
    if (t == NULL || out == NULL) {
        return false;
    }
    switch (t->kind) {
        case TSIR_IR_TYPE_SCALAR:
            if (t->scalar == TSIR_IR_F16) {
                *out = 2;
            } else {
                *out = 4;
            }
            return true;
        case TSIR_IR_TYPE_VECTOR:
            if (!tsir_ir_type_alignment(module, t->elem, &inner)) {
                return false;
            }
            *out = inner * lane_align(t->len);
            return true;
        case TSIR_IR_TYPE_MATRIX:
            if (!tsir_ir_type_alignment(module, t->elem, &inner)) {
                return false;
            }
            *out = inner * lane_align(t->rows);
            return true;
        case TSIR_IR_TYPE_ARRAY:
        case TSIR_IR_TYPE_ATOMIC:
            return tsir_ir_type_alignment(module, t->elem, out);
        case TSIR_IR_TYPE_STRUCT: {
            const TsirIrStructDef *def = NULL;
            if (t->struct_id >= module->struct_count) {
                return false;
            }
            def = &module->structs[t->struct_id];
            for (i = 0; i < def->field_count; i++) {
                if (!tsir_ir_type_alignment(module,
                    def->fields[i].ty, &inner)) {
                    return false;
                }
                if (inner > max_align) {
                    max_align = inner;
                }
            }
            *out = max_align;
            return true;
        }
        default:
            *out = 4;
            return true;
    }
}

bool tsir_ir_type_size(const TsirWgslModule *module, uint32_t ty,
    uint32_t *out) {
    const TsirIrType *t = get_type(module, ty);
    uint32_t elem_size = 0;
    uint32_t elem_align = 0;
    uint32_t stride = 0;
    uint32_t offset = 0;
    uint32_t max_align = 1;
    bool ok = true;
    size_t i = 0;
    if (t == NULL || out == NULL) {
        return false;
    }
    switch (t->kind) {
        case TSIR_IR_TYPE_SCALAR:
            *out = (t->scalar == TSIR_IR_F16) ? 2 : 4;
            return true;
        case TSIR_IR_TYPE_VECTOR:
            if (!tsir_ir_type_size(module, t->elem, &elem_size)) {
                return false;
            }
            if (elem_size > UINT32_MAX / (t->len == 0 ? 1 : t->len)) {
                return false;
            }
            *out = elem_size * t->len;
            return true;
        case TSIR_IR_TYPE_MATRIX: {
            uint32_t col_size = 0;
            uint32_t col_align = 0;
            if (!tsir_ir_type_size(module, t->elem, &elem_size)) {
                return false;
            }
            if (!tsir_ir_type_alignment(module, t->elem,
                &elem_align)) {
                return false;
            }
            col_size = elem_size * t->rows;
            col_align = elem_align * lane_align(t->rows);
            stride = round_up_u32(col_size, col_align, &ok);
            if (!ok || stride > UINT32_MAX /
                (t->columns == 0 ? 1 : t->columns)) {
                return false;
            }
            *out = stride * t->columns;
            return true;
        }
        case TSIR_IR_TYPE_ARRAY:
            if (!t->has_len) {
                *out = 0;
                return true;
            }
            if (!tsir_ir_type_size(module, t->elem, &elem_size)) {
                return false;
            }
            if (!tsir_ir_type_alignment(module, t->elem,
                &elem_align)) {
                return false;
            }
            stride = round_up_u32(elem_size, elem_align, &ok);
            if (!ok || stride > UINT32_MAX /
                (t->array_len == 0 ? 1 : t->array_len)) {
                return false;
            }
            *out = stride * t->array_len;
            return true;
        case TSIR_IR_TYPE_ATOMIC:
            return tsir_ir_type_size(module, t->elem, out);
        case TSIR_IR_TYPE_STRUCT: {
            const TsirIrStructDef *def = NULL;
            if (t->struct_id >= module->struct_count) {
                return false;
            }
            def = &module->structs[t->struct_id];
            for (i = 0; i < def->field_count; i++) {
                uint32_t field_align = 0;
                uint32_t field_size = 0;
                if (!tsir_ir_type_alignment(module,
                    def->fields[i].ty, &field_align)) {
                    return false;
                }
                if (!tsir_ir_type_size(module, def->fields[i].ty,
                    &field_size)) {
                    return false;
                }
                offset = round_up_u32(offset, field_align, &ok);
                if (!ok || offset > UINT32_MAX - field_size) {
                    return false;
                }
                offset += field_size;
                if (field_align > max_align) {
                    max_align = field_align;
                }
            }
            *out = round_up_u32(offset, max_align, &ok);
            return ok;
        }
        default:
            *out = 4;
            return true;
    }
}

bool tsir_ir_struct_field_offset(const TsirWgslModule *module,
    const TsirIrStructDef *def, uint32_t field_index, uint32_t *out) {
    uint32_t offset = 0;
    bool ok = true;
    uint32_t i = 0;
    uint32_t target_align = 0;
    if (module == NULL || def == NULL || out == NULL) {
        return false;
    }
    if (field_index >= def->field_count) {
        return false;
    }
    for (i = 0; i < field_index; i++) {
        uint32_t field_align = 0;
        uint32_t field_size = 0;
        if (!tsir_ir_type_alignment(module, def->fields[i].ty,
            &field_align)) {
            return false;
        }
        if (!tsir_ir_type_size(module, def->fields[i].ty,
            &field_size)) {
            return false;
        }
        offset = round_up_u32(offset, field_align, &ok);
        if (!ok || offset > UINT32_MAX - field_size) {
            return false;
        }
        offset += field_size;
    }
    if (!tsir_ir_type_alignment(module,
        def->fields[field_index].ty, &target_align)) {
        return false;
    }
    *out = round_up_u32(offset, target_align, &ok);
    return ok;
}

bool tsir_ir_find_global_base(const TsirIrFunction *func,
    uint32_t expr_id, uint32_t *out) {
    uint32_t cursor = expr_id;
    const TsirIrExpr *node = NULL;
    if (func == NULL || out == NULL) {
        return false;
    }
    for (;;) {
        node = get_expr(func, cursor);
        if (node == NULL) {
            return false;
        }
        switch (node->kind) {
            case TSIR_IR_EXPR_GLOBAL_REF:
                *out = node->ref_index;
                return true;
            case TSIR_IR_EXPR_INDEX:
                cursor = node->index_base;
                break;
            case TSIR_IR_EXPR_MEMBER:
                cursor = node->member_base;
                break;
            case TSIR_IR_EXPR_LOAD:
                cursor = node->inner;
                break;
            default:
                return false;
        }
    }
}

bool tsir_ir_resolve_const_local(const TsirIrFunction *func,
    uint32_t local_index, uint32_t *out) {
    size_t i = 0;
    if (func == NULL || out == NULL) {
        return false;
    }
    for (i = 0; i < func->stmt_count; i++) {
        const TsirIrStmt *stmt = &func->stmts[i];
        if (stmt->kind == TSIR_IR_STMT_LOCAL_DECL &&
            stmt->local == local_index && stmt->is_const &&
            stmt->has_initializer) {
            *out = stmt->initializer;
            return true;
        }
    }
    return false;
}

uint32_t tsir_ir_resolve_value_alias(const TsirIrFunction *func,
    uint32_t expr_id) {
    uint32_t current = expr_id;
    const TsirIrExpr *node = NULL;
    uint32_t next = TSIR_IR_NONE;
    for (;;) {
        node = get_expr(func, current);
        if (node == NULL) {
            return current;
        }
        switch (node->kind) {
            case TSIR_IR_EXPR_LOAD:
                current = node->inner;
                break;
            case TSIR_IR_EXPR_CONSTRUCT:
                if (node->args_len != 1 ||
                    node->args_start >= func->expr_arg_count) {
                    return current;
                }
                current = func->expr_args[node->args_start];
                break;
            case TSIR_IR_EXPR_LOCAL_REF:
                if (tsir_ir_resolve_const_local(func,
                    node->ref_index, &next)) {
                    current = next;
                    break;
                }
                return current;
            default:
                return current;
        }
    }
}

bool tsir_ir_match_int_literal(const TsirIrFunction *func,
    uint32_t expr_id, uint64_t *out) {
    uint32_t resolved = tsir_ir_resolve_value_alias(func, expr_id);
    const TsirIrExpr *node = get_expr(func, resolved);
    if (node == NULL || out == NULL) {
        return false;
    }
    if (node->kind != TSIR_IR_EXPR_INT_LIT) {
        return false;
    }
    *out = node->int_value;
    return true;
}
