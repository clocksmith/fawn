/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_ir.h"


#include <string.h>

void dxil_ir_init(DxilIrModule *module) {
    memset(module, 0, sizeof(*module));
}

static const DxilIrType *type_at(const DxilIrModule *module,
    DxilIrTypeId id) {
    if (id == DXIL_IR_INVALID_ID || id >= module->type_count) {
        return NULL;
    }
    return &module->types[id];
}

static bool type_eql(const DxilIrType *a, const DxilIrType *b) {
    if (a->kind != b->kind) {
        return false;
    }
    switch (a->kind) {
        case DXIL_IR_TYPE_SCALAR:
            return a->scalar == b->scalar;
        case DXIL_IR_TYPE_VECTOR:
            return a->elem == b->elem && a->len == b->len;
        case DXIL_IR_TYPE_MATRIX:
            return a->elem == b->elem && a->columns == b->columns
                && a->rows == b->rows;
        case DXIL_IR_TYPE_ARRAY:
            return a->elem == b->elem && a->has_len == b->has_len
                && (!a->has_len || a->array_len == b->array_len);
        case DXIL_IR_TYPE_ATOMIC:
            return a->elem == b->elem;
        case DXIL_IR_TYPE_STRUCT:
            return a->struct_id == b->struct_id;
        case DXIL_IR_TYPE_SAMPLER:
        case DXIL_IR_TYPE_SAMPLER_COMPARISON:
        case DXIL_IR_TYPE_TEXTURE_DEPTH_2D:
        case DXIL_IR_TYPE_TEXTURE_DEPTH_CUBE:
            return true;
        case DXIL_IR_TYPE_TEXTURE_1D:
        case DXIL_IR_TYPE_TEXTURE_2D:
        case DXIL_IR_TYPE_TEXTURE_2D_ARRAY:
        case DXIL_IR_TYPE_TEXTURE_CUBE:
        case DXIL_IR_TYPE_TEXTURE_MULTISAMPLED_2D:
        case DXIL_IR_TYPE_TEXTURE_3D:
            return a->elem == b->elem;
        case DXIL_IR_TYPE_STORAGE_TEXTURE_2D:
            return a->format == b->format && a->access == b->access;
        case DXIL_IR_TYPE_REF:
            return a->elem == b->elem
                && a->addr_space == b->addr_space
                && a->access == b->access;
    }
    return false;
}

DxilStatus dxil_ir_intern_type(DxilIrModule *module,
    const DxilIrType *type, DxilIrTypeId *out_id) {
    uint32_t i = 0;
    for (i = 0; i < module->type_count; i++) {
        if (type_eql(&module->types[i], type)) {
            *out_id = i;
            return DXIL_OK;
        }
    }
    if (module->type_count >= DXIL_IR_MAX_TYPES) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    module->types[module->type_count] = *type;
    *out_id = module->type_count;
    module->type_count += 1;
    return DXIL_OK;
}

DxilStatus dxil_ir_type_get(const DxilIrModule *module, DxilIrTypeId id,
    DxilIrType *out) {
    const DxilIrType *type = type_at(module, id);
    if (type == NULL) {
        return DXIL_INVALID_IR;
    }
    *out = *type;
    return DXIL_OK;
}

DxilStatus dxil_ir_add_struct(DxilIrModule *module, const char *name,
    DxilIrStructId *out_id) {
    DxilIrStructDef def;
    if (module->struct_count >= DXIL_IR_MAX_STRUCTS) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    memset(&def, 0, sizeof(def));
    def.name = name;
    module->structs[module->struct_count] = def;
    *out_id = module->struct_count;
    module->struct_count += 1;
    return DXIL_OK;
}

DxilStatus dxil_ir_add_struct_field(DxilIrModule *module,
    DxilIrStructId struct_id, const DxilIrStructField *field) {
    DxilIrStructDef *def = NULL;
    if (struct_id >= module->struct_count) {
        return DXIL_INVALID_IR;
    }
    def = &module->structs[struct_id];
    if (def->field_count >= DXIL_IR_MAX_STRUCT_FIELDS) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    def->fields[def->field_count] = *field;
    def->field_count += 1;
    return DXIL_OK;
}

DxilStatus dxil_ir_add_global(DxilIrModule *module,
    const DxilIrGlobal *global, DxilIrGlobalId *out_id) {
    if (module->global_count >= DXIL_IR_MAX_GLOBALS) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    module->globals[module->global_count] = *global;
    *out_id = module->global_count;
    module->global_count += 1;
    return DXIL_OK;
}

DxilStatus dxil_ir_add_function(DxilIrModule *module, const char *name,
    DxilIrTypeId return_type, DxilIrFunction **out_function) {
    DxilIrFunction *function = NULL;
    if (module->function_count >= DXIL_IR_MAX_FUNCTIONS) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    function = &module->functions[module->function_count];
    memset(function, 0, sizeof(*function));
    function->name = name;
    function->return_type = return_type;
    function->workgroup_size[0] = 1;
    function->workgroup_size[1] = 1;
    function->workgroup_size[2] = 1;
    *out_function = function;
    module->function_count += 1;
    return DXIL_OK;
}

DxilStatus dxil_ir_add_entry_point(DxilIrModule *module,
    const DxilIrEntryPoint *entry) {
    if (module->entry_point_count >= DXIL_IR_MAX_ENTRY_POINTS) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    module->entry_points[module->entry_point_count] = *entry;
    module->entry_point_count += 1;
    return DXIL_OK;
}

DxilStatus dxil_ir_append_expr(DxilIrFunction *function,
    const DxilIrExpr *expr, DxilIrExprId *out_id) {
    if (function->expr_count >= DXIL_IR_MAX_EXPRS) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    function->exprs[function->expr_count] = *expr;
    *out_id = function->expr_count;
    function->expr_count += 1;
    return DXIL_OK;
}

DxilStatus dxil_ir_append_stmt(DxilIrFunction *function,
    const DxilIrStmt *stmt, DxilIrStmtId *out_id) {
    if (function->stmt_count >= DXIL_IR_MAX_STMTS) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    function->stmts[function->stmt_count] = *stmt;
    *out_id = function->stmt_count;
    function->stmt_count += 1;
    return DXIL_OK;
}

DxilStatus dxil_ir_append_expr_arg(DxilIrFunction *function,
    DxilIrExprId arg) {
    if (function->expr_arg_count >= DXIL_IR_MAX_EXPR_ARGS) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    function->expr_args[function->expr_arg_count] = arg;
    function->expr_arg_count += 1;
    return DXIL_OK;
}

DxilStatus dxil_ir_append_stmt_child(DxilIrFunction *function,
    DxilIrStmtId child) {
    if (function->stmt_child_count >= DXIL_IR_MAX_STMT_CHILDREN) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    function->stmt_children[function->stmt_child_count] = child;
    function->stmt_child_count += 1;
    return DXIL_OK;
}

DxilStatus dxil_ir_append_selector(DxilIrFunction *function,
    DxilIrExprId selector) {
    if (function->switch_selector_count >= DXIL_IR_MAX_SWITCH_SELECTORS) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    function->switch_selectors[function->switch_selector_count] =
        selector;
    function->switch_selector_count += 1;
    return DXIL_OK;
}

DxilStatus dxil_ir_append_switch_case(DxilIrFunction *function,
    const DxilIrSwitchCase *switch_case) {
    if (function->switch_case_count >= DXIL_IR_MAX_SWITCH_CASES) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    function->switch_cases[function->switch_case_count] = *switch_case;
    function->switch_case_count += 1;
    return DXIL_OK;
}

bool dxil_ir_is_float_type(const DxilIrModule *module, DxilIrTypeId ty) {
    const DxilIrType *type = type_at(module, ty);
    if (type == NULL) {
        return false;
    }
    switch (type->kind) {
        case DXIL_IR_TYPE_SCALAR:
            return type->scalar == DXIL_IR_SCALAR_F32
                || type->scalar == DXIL_IR_SCALAR_F16
                || type->scalar == DXIL_IR_SCALAR_ABSTRACT_FLOAT;
        case DXIL_IR_TYPE_VECTOR:
            return dxil_ir_is_float_type(module, type->elem);
        case DXIL_IR_TYPE_MATRIX:
        case DXIL_IR_TYPE_ARRAY:
        case DXIL_IR_TYPE_ATOMIC:
        case DXIL_IR_TYPE_STRUCT:
        case DXIL_IR_TYPE_SAMPLER:
        case DXIL_IR_TYPE_SAMPLER_COMPARISON:
        case DXIL_IR_TYPE_TEXTURE_1D:
        case DXIL_IR_TYPE_TEXTURE_2D:
        case DXIL_IR_TYPE_TEXTURE_2D_ARRAY:
        case DXIL_IR_TYPE_TEXTURE_CUBE:
        case DXIL_IR_TYPE_TEXTURE_MULTISAMPLED_2D:
        case DXIL_IR_TYPE_TEXTURE_DEPTH_2D:
        case DXIL_IR_TYPE_TEXTURE_DEPTH_CUBE:
        case DXIL_IR_TYPE_TEXTURE_3D:
        case DXIL_IR_TYPE_STORAGE_TEXTURE_2D:
        case DXIL_IR_TYPE_REF:
        default:
            return false;
    }
}

bool dxil_ir_is_signed_type(const DxilIrModule *module, DxilIrTypeId ty) {
    const DxilIrType *type = type_at(module, ty);
    if (type == NULL) {
        return false;
    }
    switch (type->kind) {
        case DXIL_IR_TYPE_SCALAR:
            return type->scalar == DXIL_IR_SCALAR_I32
                || type->scalar == DXIL_IR_SCALAR_ABSTRACT_INT;
        case DXIL_IR_TYPE_VECTOR:
            return dxil_ir_is_signed_type(module, type->elem);
        case DXIL_IR_TYPE_MATRIX:
        case DXIL_IR_TYPE_ARRAY:
        case DXIL_IR_TYPE_ATOMIC:
        case DXIL_IR_TYPE_STRUCT:
        case DXIL_IR_TYPE_SAMPLER:
        case DXIL_IR_TYPE_SAMPLER_COMPARISON:
        case DXIL_IR_TYPE_TEXTURE_1D:
        case DXIL_IR_TYPE_TEXTURE_2D:
        case DXIL_IR_TYPE_TEXTURE_2D_ARRAY:
        case DXIL_IR_TYPE_TEXTURE_CUBE:
        case DXIL_IR_TYPE_TEXTURE_MULTISAMPLED_2D:
        case DXIL_IR_TYPE_TEXTURE_DEPTH_2D:
        case DXIL_IR_TYPE_TEXTURE_DEPTH_CUBE:
        case DXIL_IR_TYPE_TEXTURE_3D:
        case DXIL_IR_TYPE_STORAGE_TEXTURE_2D:
        case DXIL_IR_TYPE_REF:
        default:
            return false;
    }
}

uint32_t dxil_ir_round_up(uint32_t value, uint32_t alignment) {
    uint64_t result = 0;
    uint64_t remainder = 0;
    if (alignment <= 1) {
        return value;
    }
    remainder = (uint64_t)value % alignment;
    if (remainder == 0) {
        return value;
    }
    result = (uint64_t)value + alignment - remainder;
    return (uint32_t)result;
}

static uint32_t lane_align(uint32_t count) {
    switch (count) {
        case 2:
            return 2;
        case 3:
        case 4:
            return 4;
        default:
            return 1;
    }
}

static uint32_t alignment_depth(const DxilIrModule *module,
    DxilIrTypeId ty, unsigned depth) {
    const DxilIrType *type = type_at(module, ty);
    uint32_t max_align = 1;
    uint32_t i = 0;
    uint32_t field_align = 0;
    if (type == NULL || depth > 64) {
        return 4;
    }
    switch (type->kind) {
        case DXIL_IR_TYPE_SCALAR:
            return type->scalar == DXIL_IR_SCALAR_F16 ? 2u : 4u;
        case DXIL_IR_TYPE_VECTOR:
            return alignment_depth(module, type->elem, depth + 1)
                * lane_align(type->len);
        case DXIL_IR_TYPE_MATRIX:
            return alignment_depth(module, type->elem, depth + 1)
                * lane_align(type->rows);
        case DXIL_IR_TYPE_ARRAY:
        case DXIL_IR_TYPE_ATOMIC:
            return alignment_depth(module, type->elem, depth + 1);
        case DXIL_IR_TYPE_STRUCT: {
            const DxilIrStructDef *def = NULL;
            if (type->struct_id >= module->struct_count) {
                return 4;
            }
            def = &module->structs[type->struct_id];
            for (i = 0; i < def->field_count; i++) {
                field_align = alignment_depth(module,
                    def->fields[i].ty, depth + 1);
                if (field_align > max_align) {
                    max_align = field_align;
                }
            }
            return max_align;
        }
        case DXIL_IR_TYPE_SAMPLER:
        case DXIL_IR_TYPE_SAMPLER_COMPARISON:
        case DXIL_IR_TYPE_TEXTURE_1D:
        case DXIL_IR_TYPE_TEXTURE_2D:
        case DXIL_IR_TYPE_TEXTURE_2D_ARRAY:
        case DXIL_IR_TYPE_TEXTURE_CUBE:
        case DXIL_IR_TYPE_TEXTURE_MULTISAMPLED_2D:
        case DXIL_IR_TYPE_TEXTURE_DEPTH_2D:
        case DXIL_IR_TYPE_TEXTURE_DEPTH_CUBE:
        case DXIL_IR_TYPE_TEXTURE_3D:
        case DXIL_IR_TYPE_STORAGE_TEXTURE_2D:
        case DXIL_IR_TYPE_REF:
        default:
            return 4;
    }
}

uint32_t dxil_ir_type_alignment(const DxilIrModule *module,
    DxilIrTypeId ty) {
    return alignment_depth(module, ty, 0);
}

static uint32_t size_depth(const DxilIrModule *module, DxilIrTypeId ty,
    unsigned depth) {
    const DxilIrType *type = type_at(module, ty);
    uint32_t offset = 0;
    uint32_t max_align = 1;
    uint32_t i = 0;
    uint32_t field_align = 0;
    uint32_t col_size = 0;
    uint32_t col_align = 0;
    uint32_t stride = 0;
    if (type == NULL || depth > 64) {
        return 4;
    }
    switch (type->kind) {
        case DXIL_IR_TYPE_SCALAR:
            return type->scalar == DXIL_IR_SCALAR_F16 ? 2u : 4u;
        case DXIL_IR_TYPE_VECTOR:
            return size_depth(module, type->elem, depth + 1)
                * type->len;
        case DXIL_IR_TYPE_MATRIX:
            col_size = size_depth(module, type->elem, depth + 1)
                * type->rows;
            col_align = alignment_depth(module, type->elem, depth + 1)
                * lane_align(type->rows);
            stride = dxil_ir_round_up(col_size, col_align);
            return stride * type->columns;
        case DXIL_IR_TYPE_ARRAY:
            if (!type->has_len) {
                return 0;
            }
            stride = dxil_ir_round_up(
                size_depth(module, type->elem, depth + 1),
                alignment_depth(module, type->elem, depth + 1));
            return stride * type->array_len;
        case DXIL_IR_TYPE_ATOMIC:
            return size_depth(module, type->elem, depth + 1);
        case DXIL_IR_TYPE_STRUCT: {
            const DxilIrStructDef *def = NULL;
            if (type->struct_id >= module->struct_count) {
                return 4;
            }
            def = &module->structs[type->struct_id];
            for (i = 0; i < def->field_count; i++) {
                field_align = alignment_depth(module,
                    def->fields[i].ty, depth + 1);
                offset = dxil_ir_round_up(offset, field_align);
                offset += size_depth(module, def->fields[i].ty,
                    depth + 1);
                if (field_align > max_align) {
                    max_align = field_align;
                }
            }
            return dxil_ir_round_up(offset, max_align);
        }
        case DXIL_IR_TYPE_SAMPLER:
        case DXIL_IR_TYPE_SAMPLER_COMPARISON:
        case DXIL_IR_TYPE_TEXTURE_1D:
        case DXIL_IR_TYPE_TEXTURE_2D:
        case DXIL_IR_TYPE_TEXTURE_2D_ARRAY:
        case DXIL_IR_TYPE_TEXTURE_CUBE:
        case DXIL_IR_TYPE_TEXTURE_MULTISAMPLED_2D:
        case DXIL_IR_TYPE_TEXTURE_DEPTH_2D:
        case DXIL_IR_TYPE_TEXTURE_DEPTH_CUBE:
        case DXIL_IR_TYPE_TEXTURE_3D:
        case DXIL_IR_TYPE_STORAGE_TEXTURE_2D:
        case DXIL_IR_TYPE_REF:
        default:
            return 4;
    }
}

uint32_t dxil_ir_type_size(const DxilIrModule *module, DxilIrTypeId ty) {
    return size_depth(module, ty, 0);
}

DxilStatus dxil_ir_struct_field_offset(const DxilIrModule *module,
    DxilIrStructId struct_id, uint32_t target_field, uint32_t *out) {
    const DxilIrStructDef *def = NULL;
    uint32_t offset = 0;
    uint32_t i = 0;
    uint32_t field_align = 0;
    if (struct_id >= module->struct_count) {
        return DXIL_INVALID_IR;
    }
    def = &module->structs[struct_id];
    if (target_field >= def->field_count) {
        return DXIL_INVALID_IR;
    }
    for (i = 0; i < target_field; i++) {
        field_align = dxil_ir_type_alignment(module, def->fields[i].ty);
        offset = dxil_ir_round_up(offset, field_align);
        offset += dxil_ir_type_size(module, def->fields[i].ty);
    }
    field_align = dxil_ir_type_alignment(module,
        def->fields[target_field].ty);
    *out = dxil_ir_round_up(offset, field_align);
    return DXIL_OK;
}

static DxilStatus minimum_binding_depth(const DxilIrModule *module,
    DxilIrTypeId ty, unsigned depth, uint64_t *out) {
    const DxilIrType *type = type_at(module, ty);
    uint64_t end = 0;
    uint64_t alignment = 1;
    uint64_t field_alignment = 0;
    uint64_t field_size = 0;
    uint64_t element = 0;
    uint64_t stride = 0;
    uint64_t count = 0;
    uint32_t i = 0;
    if (type == NULL || depth > 64) {
        return DXIL_INVALID_IR;
    }
    switch (type->kind) {
        case DXIL_IR_TYPE_ARRAY: {
            DxilStatus status = minimum_binding_depth(module,
                type->elem, depth + 1, &element);
            uint64_t elem_align = 0;
            if (status != DXIL_OK) {
                return status;
            }
            elem_align = dxil_ir_type_alignment(module, type->elem);
            if (elem_align > 1 && element % elem_align != 0) {
                stride = element + elem_align - element % elem_align;
            } else {
                stride = element;
            }
            count = type->has_len ? type->array_len : 1u;
            *out = stride * count;
            return DXIL_OK;
        }
        case DXIL_IR_TYPE_STRUCT: {
            const DxilIrStructDef *def = NULL;
            if (type->struct_id >= module->struct_count) {
                return DXIL_INVALID_IR;
            }
            def = &module->structs[type->struct_id];
            for (i = 0; i < def->field_count; i++) {
                field_alignment = dxil_ir_type_alignment(module,
                    def->fields[i].ty);
                if (field_alignment > alignment) {
                    alignment = field_alignment;
                }
                end = dxil_ir_round_up((uint32_t)end,
                    (uint32_t)field_alignment);
                {
                    DxilStatus field_status =
                        minimum_binding_depth(module,
                            def->fields[i].ty, depth + 1, &field_size);
                    if (field_status != DXIL_OK) {
                        return field_status;
                    }
                }
                end += field_size;
                if (end > UINT32_MAX) {
                    return DXIL_INVALID_IR;
                }
            }
            *out = dxil_ir_round_up((uint32_t)end,
                (uint32_t)alignment);
            return DXIL_OK;
        }
        case DXIL_IR_TYPE_SCALAR:
        case DXIL_IR_TYPE_VECTOR:
        case DXIL_IR_TYPE_MATRIX:
        case DXIL_IR_TYPE_ATOMIC:
        case DXIL_IR_TYPE_SAMPLER:
        case DXIL_IR_TYPE_SAMPLER_COMPARISON:
        case DXIL_IR_TYPE_TEXTURE_1D:
        case DXIL_IR_TYPE_TEXTURE_2D:
        case DXIL_IR_TYPE_TEXTURE_2D_ARRAY:
        case DXIL_IR_TYPE_TEXTURE_CUBE:
        case DXIL_IR_TYPE_TEXTURE_MULTISAMPLED_2D:
        case DXIL_IR_TYPE_TEXTURE_DEPTH_2D:
        case DXIL_IR_TYPE_TEXTURE_DEPTH_CUBE:
        case DXIL_IR_TYPE_TEXTURE_3D:
        case DXIL_IR_TYPE_STORAGE_TEXTURE_2D:
        case DXIL_IR_TYPE_REF:
        default:
            *out = dxil_ir_type_size(module, ty);
            return DXIL_OK;
    }
}

DxilStatus dxil_ir_minimum_binding_size(const DxilIrModule *module,
    DxilIrTypeId ty, uint32_t *out) {
    uint64_t size = 0;
    DxilStatus status = minimum_binding_depth(module, ty, 0, &size);
    if (status != DXIL_OK) {
        return status;
    }
    if (size > UINT32_MAX) {
        return DXIL_INVALID_IR;
    }
    *out = (uint32_t)size;
    return DXIL_OK;
}

