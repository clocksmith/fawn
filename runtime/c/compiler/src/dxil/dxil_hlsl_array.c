/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_hlsl_internal.h"

static bool runtime_global(const DxilIrModule *module, uint32_t global) {
    if (global >= module->global_count) return false;
    const DxilIrGlobal *root = &module->globals[global];
    DxilIrType type;
    return root->has_addr_space && root->addr_space == DXIL_IR_ADDR_STORAGE && root->has_binding &&
        dxil_ir_type_get(module, root->ty, &type) == DXIL_OK &&
        type.kind == DXIL_IR_TYPE_ARRAY && !type.has_len;
}

DxilStatus dxil_hlsl_emit_array_length(DxilHlslEmitter *emitter,
    const DxilIrFunction *function, DxilIrRange arguments) {
    if (arguments.len != 1u || arguments.start >= function->expr_arg_count) return DXIL_INVALID_IR;
    uint32_t expression = function->expr_args[arguments.start];
    for (uint32_t depth = 0u; depth < function->expr_count; ++depth) {
        if (expression >= function->expr_count) return DXIL_INVALID_IR;
        const DxilIrExpr *node = &function->exprs[expression];
        if (node->kind == DXIL_IR_EXPR_LOAD || node->kind == DXIL_IR_EXPR_DEREF || node->kind == DXIL_IR_EXPR_ADDRESS_OF) {
            expression = node->inner; continue;
        }
        if (node->kind == DXIL_IR_EXPR_LOCAL_REF) {
            if (!dxil_ir_resolve_const_local_initializer(function, node->index, &expression)) return DXIL_INVALID_IR;
            continue;
        }
        uint32_t root = node->index;
        const char *field_name = NULL;
        if (node->kind == DXIL_IR_EXPR_MEMBER) {
            if (!dxil_ir_resolve_member_global(function, node->base, &root) || root >= emitter->module->global_count)
                return DXIL_INVALID_IR;
            const DxilIrGlobal *global = &emitter->module->globals[root];
            DxilIrType structure, array;
            if (!global->has_addr_space || global->addr_space != DXIL_IR_ADDR_STORAGE || !global->has_binding ||
                dxil_ir_type_get(emitter->module, global->ty, &structure) != DXIL_OK ||
                structure.kind != DXIL_IR_TYPE_STRUCT || structure.struct_id >= emitter->module->struct_count)
                return DXIL_INVALID_IR;
            const DxilIrStructDef *definition = &emitter->module->structs[structure.struct_id];
            if (node->field_index >= definition->field_count) return DXIL_INVALID_IR;
            const DxilIrStructField *field = &definition->fields[node->field_index];
            if (dxil_ir_type_get(emitter->module, field->ty, &array) != DXIL_OK ||
                array.kind != DXIL_IR_TYPE_ARRAY || array.has_len) return DXIL_INVALID_IR;
            field_name = field->name;
        } else if (node->kind != DXIL_IR_EXPR_GLOBAL_REF || !runtime_global(emitter->module, root)) return DXIL_INVALID_IR;
        DxilStatus status = dxil_hlsl_write_text(emitter, "doe_arrayLength_");
        if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, emitter->module->globals[root].name);
        if (status == DXIL_OK && field_name != NULL) {
            status = dxil_hlsl_write_text(emitter, "_");
            if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, field_name);
        }
        if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, "()");
        return status;
    }
    return DXIL_INVALID_IR;
}

static DxilStatus emit_helper(DxilHlslEmitter *emitter, const char *global,
    const char *field, uint32_t offset, uint32_t size) {
    DxilStatus status = dxil_hlsl_write_text(emitter, "\nuint doe_arrayLength_");
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, global);
    if (field != NULL && status == DXIL_OK) {
        status = dxil_hlsl_write_text(emitter, "_");
        if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, field);
    }
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter,
        "() {\n    uint count = 0u;\n    uint stride = 0u;\n    ");
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, global);
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, ".GetDimensions(count, stride);\n    return ");
    if (field == NULL) {
        if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, "count");
    } else {
        if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, "(stride - ");
        if (status == DXIL_OK) status = dxil_hlsl_write_u32(emitter, offset);
        if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, ") / ");
        if (status == DXIL_OK) status = dxil_hlsl_write_u32(emitter, size);
    }
    if (status == DXIL_OK) status = dxil_hlsl_write_text(emitter, ";\n}\n");
    return status;
}
DxilStatus dxil_hlsl_emit_array_helpers(DxilHlslEmitter *emitter) {
    for (uint32_t i = 0u; i < emitter->module->global_count; ++i) {
        const DxilIrGlobal *global = &emitter->module->globals[i];
        if (runtime_global(emitter->module, i)) {
            DxilStatus status = emit_helper(emitter, global->name, NULL, 0u, 0u);
            if (status != DXIL_OK) return status;
            continue;
        }
        DxilIrType type;
        if (!global->has_addr_space || global->addr_space != DXIL_IR_ADDR_STORAGE || !global->has_binding ||
            dxil_ir_type_get(emitter->module, global->ty, &type) != DXIL_OK || type.kind != DXIL_IR_TYPE_STRUCT) continue;
        if (type.struct_id >= emitter->module->struct_count) return DXIL_INVALID_IR;
        const DxilIrStructDef *structure = &emitter->module->structs[type.struct_id];
        for (uint32_t field = 0u; field < structure->field_count; ++field) {
            const DxilIrStructField *member = &structure->fields[field];
            DxilIrType array;
            if (dxil_ir_type_get(emitter->module, member->ty, &array) != DXIL_OK ||
                array.kind != DXIL_IR_TYPE_ARRAY || array.has_len) continue;
            uint32_t offset;
            DxilStatus status = dxil_ir_struct_field_offset(emitter->module, type.struct_id, field, &offset);
            uint32_t size = dxil_ir_type_size(emitter->module, array.elem);
            if (status != DXIL_OK || size == 0u) return DXIL_INVALID_IR;
            /* Compatibility with the retained serializer's structured-tail
             * formula. Physical acceptance still requires actual DXC. */
            status = emit_helper(emitter, global->name, member->name, offset, size);
            if (status != DXIL_OK) return status;
        }
    }
    return DXIL_OK;
}
