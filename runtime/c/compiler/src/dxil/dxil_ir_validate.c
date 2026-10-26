/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_ir.h"

/* Structural caller boundary only. Semantic admission belongs to the shared
 * compiler, not this compatibility serializer. Names remain caller-owned,
 * valid NUL-terminated C strings for the duration of the call. */
static bool range_ok(DxilIrRange range, uint32_t count) {
    return range.start <= count && range.len <= count - range.start;
}
static bool io_ok(const DxilIrIoAttr *io) {
    return (unsigned)io->builtin <= DXIL_IR_BUILTIN_PRIMITIVE_INDEX &&
        (!io->has_interpolation || (unsigned)io->interpolation <= DXIL_IR_INTERP_FLAT) &&
        (!io->has_sampling || ((unsigned)io->sampling <= DXIL_IR_SAMPLING_EITHER &&
            ((unsigned)io->sampling <= DXIL_IR_SAMPLING_SAMPLE ||
                (io->has_interpolation && io->interpolation == DXIL_IR_INTERP_FLAT))));
}
static bool type_walk(const DxilIrModule *module, uint32_t id, unsigned char *colors) {
    if (id >= module->type_count || colors[id] == 1u) return false;
    if (colors[id] == 2u) return true;
    colors[id] = 1u;
    const DxilIrType *type = &module->types[id];
    switch (type->kind) {
        case DXIL_IR_TYPE_VECTOR: case DXIL_IR_TYPE_MATRIX: case DXIL_IR_TYPE_ARRAY:
        case DXIL_IR_TYPE_ATOMIC: case DXIL_IR_TYPE_REF:
            if (!type_walk(module, type->elem, colors)) return false;
            break;
        case DXIL_IR_TYPE_STRUCT:
            if (type->struct_id >= module->struct_count) return false;
            for (uint32_t i = 0u; i < module->structs[type->struct_id].field_count; ++i)
                if (!type_walk(module, module->structs[type->struct_id].fields[i].ty, colors)) return false;
            break;
        case DXIL_IR_TYPE_SCALAR: case DXIL_IR_TYPE_SAMPLER: case DXIL_IR_TYPE_SAMPLER_COMPARISON:
        case DXIL_IR_TYPE_TEXTURE_1D: case DXIL_IR_TYPE_TEXTURE_2D: case DXIL_IR_TYPE_TEXTURE_2D_ARRAY:
        case DXIL_IR_TYPE_TEXTURE_CUBE: case DXIL_IR_TYPE_TEXTURE_MULTISAMPLED_2D:
        case DXIL_IR_TYPE_TEXTURE_DEPTH_2D: case DXIL_IR_TYPE_TEXTURE_DEPTH_CUBE:
        case DXIL_IR_TYPE_TEXTURE_3D: case DXIL_IR_TYPE_STORAGE_TEXTURE_2D: break;
    }
    colors[id] = 2u;
    return true;
}
static bool constants_walk(const DxilIrModule *module, uint32_t id, unsigned char *colors, uint32_t depth) {
    if (id >= module->constant_count || colors[id] == 1u || depth > DXIL_IR_MAX_CONSTANTS) return false;
    if (colors[id] == 2u) return true;
    const DxilIrConstant *constant = &module->constants[id];
    colors[id] = 1u;
    if (constant->composite) {
        for (uint32_t i = 0u; i < constant->children.len; ++i)
            if (!constants_walk(module, module->constant_children[constant->children.start + i], colors, depth + 1u)) return false;
    }
    colors[id] = 2u;
    return true;
}
static bool expression_ok(const DxilIrModule *module, const DxilIrFunction *function, const DxilIrExpr *node) {
    if (node->ty >= module->type_count || (unsigned)node->kind > DXIL_IR_EXPR_INDEX ||
        (unsigned)node->category > DXIL_IR_EXPR_REF) return false;
    switch (node->kind) {
        case DXIL_IR_EXPR_BOOL_LIT: case DXIL_IR_EXPR_INT_LIT: case DXIL_IR_EXPR_FLOAT_LIT: return true;
        case DXIL_IR_EXPR_PARAM_REF: return node->index < function->param_count;
        case DXIL_IR_EXPR_LOCAL_REF: return node->index < function->local_count;
        case DXIL_IR_EXPR_GLOBAL_REF: return node->index < module->global_count;
        case DXIL_IR_EXPR_LOAD: case DXIL_IR_EXPR_ADDRESS_OF: case DXIL_IR_EXPR_DEREF:
            return node->inner < function->expr_count;
        case DXIL_IR_EXPR_UNARY:
            return node->operand < function->expr_count && (unsigned)node->unary_op <= DXIL_IR_UNARY_BIT_NOT;
        case DXIL_IR_EXPR_BINARY:
            return node->lhs < function->expr_count && node->rhs < function->expr_count &&
                (unsigned)node->binary_op <= DXIL_IR_BINARY_LOGICAL_OR;
        case DXIL_IR_EXPR_CALL:
            return node->name != NULL && (unsigned)node->call_kind <= DXIL_IR_CALL_BUILTIN &&
                range_ok(node->args, function->expr_arg_count);
        case DXIL_IR_EXPR_CONSTRUCT:
            return node->construct_ty < module->type_count && range_ok(node->args, function->expr_arg_count);
        case DXIL_IR_EXPR_MEMBER: return node->field_name != NULL && node->base < function->expr_count;
        case DXIL_IR_EXPR_INDEX:
            return node->base < function->expr_count && node->index_expr < function->expr_count;
    }
    return false;
}
static bool statement_ok(const DxilIrFunction *function, const DxilIrStmt *node) {
    if ((unsigned)node->kind > DXIL_IR_STMT_DISCARD) return false;
    switch (node->kind) {
        case DXIL_IR_STMT_BLOCK: return range_ok(node->range, function->stmt_child_count);
        case DXIL_IR_STMT_LOCAL_DECL:
            return node->local < function->local_count && (!node->has_initializer || node->initializer < function->expr_count);
        case DXIL_IR_STMT_EXPR: return node->expr < function->expr_count;
        case DXIL_IR_STMT_ASSIGN:
            return node->assign_lhs < function->expr_count && node->assign_rhs < function->expr_count &&
                (unsigned)node->assign_op <= DXIL_IR_ASSIGN_SHIFT_RIGHT;
        case DXIL_IR_STMT_RETURN: return !node->has_return_value || node->return_value < function->expr_count;
        case DXIL_IR_STMT_IF:
            return node->cond < function->expr_count && node->then_block < function->stmt_count &&
                (!node->has_else_block || node->else_block < function->stmt_count);
        case DXIL_IR_STMT_LOOP:
            return (unsigned)node->loop_kind <= DXIL_IR_LOOP_FOR && node->body < function->stmt_count &&
                (!node->has_init || node->init < function->stmt_count) &&
                (!node->has_cond || node->cond < function->expr_count) &&
                (!node->has_continuing || node->continuing < function->stmt_count);
        case DXIL_IR_STMT_SWITCH:
            return node->switch_expr < function->expr_count && range_ok(node->cases, function->switch_case_count);
        case DXIL_IR_STMT_BREAK: case DXIL_IR_STMT_CONTINUE: case DXIL_IR_STMT_DISCARD: return true;
    }
    return false;
}
static bool function_ok(const DxilIrModule *module, const DxilIrFunction *function) {
    if (function->name == NULL || function->return_type >= module->type_count ||
        (function->has_stage && (unsigned)function->stage > DXIL_IR_STAGE_FRAGMENT) ||
        (function->has_return_io && !io_ok(&function->return_io)) ||
        function->param_count > DXIL_IR_MAX_PARAMS || function->local_count > DXIL_IR_MAX_LOCALS ||
        function->expr_count > DXIL_IR_MAX_EXPRS || function->expr_arg_count > DXIL_IR_MAX_EXPR_ARGS ||
        function->stmt_count > DXIL_IR_MAX_STMTS || function->stmt_child_count > DXIL_IR_MAX_STMT_CHILDREN ||
        function->switch_case_count > DXIL_IR_MAX_SWITCH_CASES ||
        function->switch_selector_count > DXIL_IR_MAX_SWITCH_SELECTORS || function->root_stmt >= function->stmt_count) return false;
    for (uint32_t i = 0u; i < function->param_count; ++i) {
        const DxilIrParam *parameter = &function->params[i];
        if (parameter->name == NULL || parameter->ty >= module->type_count ||
            (parameter->has_io && !io_ok(&parameter->io))) return false;
    }
    for (uint32_t i = 0u; i < function->local_count; ++i)
        if (function->locals[i].name == NULL || function->locals[i].ty >= module->type_count) return false;
    for (uint32_t i = 0u; i < function->expr_count; ++i)
        if (!expression_ok(module, function, &function->exprs[i])) return false;
    for (uint32_t i = 0u; i < function->expr_arg_count; ++i)
        if (function->expr_args[i] >= function->expr_count) return false;
    for (uint32_t i = 0u; i < function->stmt_count; ++i)
        if (!statement_ok(function, &function->stmts[i])) return false;
    for (uint32_t i = 0u; i < function->stmt_child_count; ++i)
        if (function->stmt_children[i] >= function->stmt_count) return false;
    for (uint32_t i = 0u; i < function->switch_selector_count; ++i)
        if (function->switch_selectors[i] >= function->expr_count) return false;
    for (uint32_t i = 0u; i < function->switch_case_count; ++i)
        if (function->switch_cases[i].body >= function->stmt_count ||
            !range_ok(function->switch_cases[i].selectors, function->switch_selector_count)) return false;
    return true;
}

DxilStatus dxil_ir_validate(const DxilIrModule *module) {
    if (module == NULL || module->type_count > DXIL_IR_MAX_TYPES || module->struct_count > DXIL_IR_MAX_STRUCTS ||
        module->global_count > DXIL_IR_MAX_GLOBALS || module->function_count > DXIL_IR_MAX_FUNCTIONS ||
        module->entry_point_count > DXIL_IR_MAX_ENTRY_POINTS || module->constant_count > DXIL_IR_MAX_CONSTANTS ||
        module->constant_child_count > DXIL_IR_MAX_CONSTANTS) return DXIL_INVALID_IR;
    for (uint32_t i = 0u; i < module->struct_count; ++i) {
        const DxilIrStructDef *structure = &module->structs[i];
        if (structure->name == NULL || structure->field_count > DXIL_IR_MAX_STRUCT_FIELDS) return DXIL_INVALID_IR;
        for (uint32_t j = 0u; j < structure->field_count; ++j) {
            const DxilIrStructField *field = &structure->fields[j];
            if (field->name == NULL || field->ty >= module->type_count || (field->has_io && !io_ok(&field->io))) return DXIL_INVALID_IR;
        }
    }
    for (uint32_t i = 0u; i < module->type_count; ++i) {
        const DxilIrType *type = &module->types[i];
        if ((unsigned)type->kind > DXIL_IR_TYPE_REF ||
            (type->kind == DXIL_IR_TYPE_SCALAR && (unsigned)type->scalar > DXIL_IR_SCALAR_F16) ||
            (type->kind == DXIL_IR_TYPE_REF && ((unsigned)type->access > DXIL_IR_ACCESS_READ_WRITE ||
                (unsigned)type->addr_space > DXIL_IR_ADDR_HANDLE))) return DXIL_INVALID_IR;
    }
    unsigned char types[DXIL_IR_MAX_TYPES] = {0};
    for (uint32_t i = 0u; i < module->type_count; ++i)
        if (!type_walk(module, i, types)) return DXIL_INVALID_IR;
    for (uint32_t i = 0u; i < module->constant_count; ++i) {
        const DxilIrConstant *constant = &module->constants[i];
        if (constant->ty >= module->type_count ||
            (constant->composite ? !range_ok(constant->children, module->constant_child_count) :
                (unsigned)constant->kind > DXIL_IR_CONST_FLOAT)) return DXIL_INVALID_IR;
    }
    unsigned char constants[DXIL_IR_MAX_CONSTANTS] = {0};
    for (uint32_t i = 0u; i < module->constant_count; ++i)
        if (!constants_walk(module, i, constants, 0u)) return DXIL_INVALID_IR;
    for (uint32_t i = 0u; i < module->global_count; ++i) {
        const DxilIrGlobal *global = &module->globals[i];
        if (global->name == NULL || global->ty >= module->type_count || (unsigned)global->class > DXIL_IR_GLOBAL_OUTPUT ||
            (global->has_addr_space && (unsigned)global->addr_space > DXIL_IR_ADDR_HANDLE) ||
            (global->has_access && (unsigned)global->access > DXIL_IR_ACCESS_READ_WRITE) ||
            (global->has_io && !io_ok(&global->io)) ||
            (global->has_initializer && (global->has_init_tree ? global->init_tree >= module->constant_count :
                (unsigned)global->init_kind > DXIL_IR_CONST_FLOAT))) return DXIL_INVALID_IR;
    }
    for (uint32_t i = 0u; i < module->function_count; ++i)
        if (!function_ok(module, &module->functions[i])) return DXIL_INVALID_IR;
    for (uint32_t i = 0u; i < module->entry_point_count; ++i)
        if (module->entry_points[i].function >= module->function_count ||
            (unsigned)module->entry_points[i].stage > DXIL_IR_STAGE_FRAGMENT) return DXIL_INVALID_IR;
    return DXIL_OK;
}
