/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_source_native_bounds.h"
#include "../../targets/ir_view.h"
#include "../../targets/source_facts.h"

static bool local_space(const DxilSourceBridge *bridge, const DxilIrFunction *function,
    uint32_t expression) {
    for (uint32_t depth = 0u; depth < DXIL_IR_MAX_EXPRS && expression < function->expr_count; ++depth) {
        const DxilIrExpr *node = &function->exprs[expression];
        if (node->kind == DXIL_IR_EXPR_LOAD) { expression = node->inner; continue; }
        if (node->kind == DXIL_IR_EXPR_MEMBER || node->kind == DXIL_IR_EXPR_INDEX) {
            expression = node->base; continue;
        }
        DxilIrAddressSpace space;
        if (node->kind == DXIL_IR_EXPR_GLOBAL_REF && node->index < bridge->module->global_count) {
            const DxilIrGlobal *global = &bridge->module->globals[node->index];
            if (!global->has_addr_space) return false;
            space = global->addr_space;
        } else if ((node->kind == DXIL_IR_EXPR_LOCAL_REF || node->kind == DXIL_IR_EXPR_PARAM_REF) &&
            node->ty < bridge->module->type_count) {
            const DxilIrType *type = &bridge->module->types[node->ty];
            if (type->kind == DXIL_IR_TYPE_REF) space = type->addr_space;
            else if (node->kind == DXIL_IR_EXPR_LOCAL_REF) space = DXIL_IR_ADDR_FUNCTION;
            else return false;
        } else return false;
        return space == DXIL_IR_ADDR_FUNCTION || space == DXIL_IR_ADDR_PRIVATE || space == DXIL_IR_ADDR_WORKGROUP;
    }
    return false;
}

static bool guard_constant(DxilSourceBridge *bridge, DxilSourceFunction *function,
    uint32_t expression, ConstantValue *value, bool *known) {
    /* A guard is evaluated outside its body: later shadowing declarations in
     * the currently projected body must not rebind the original guard name. */
    DxilSourceFunction lexical = *function;
    lexical.scope_count = 0u;
    const uint32_t offset = bridge->compiler->expressions[expression].source_offset;
    for (uint32_t i = 0u; i < function->scope_count; ++i) {
        uint32_t local = function->scope[i];
        if (function->bindings[local].declaration_offset < offset)
            lexical.scope[lexical.scope_count++] = local;
    }
    return dxil_source_native_constant_if_known(bridge, &lexical, expression, value, known);
}

static bool guarded_index(DxilSourceBridge *bridge, DxilSourceFunction *function,
    uint32_t source_index, uint32_t count) {
    const Expression *index = &bridge->compiler->expressions[source_index];
    if (index->kind != EXPRESSION_NAME || index->resolved_type != IR_U32 || function->syntax == NULL) return false;
    const LocalBinding *binding = NULL;
    for (uint32_t n = function->scope_count; n != 0u; --n) {
        const LocalBinding *candidate = &function->bindings[function->scope[n - 1u]];
        if (target_source_same_name(&bridge->source, candidate->name, index->value.name)) {
            binding = candidate; break;
        }
    }
    if (binding == NULL || binding->type != IR_U32) return false;
    for (uint32_t cursor = function->syntax->statement_start; cursor < function->syntax->statement_end; ++cursor) {
        TargetSourceLoopGuard fact;
        if (!target_source_loop_guard(&bridge->source, function->syntax, bridge->source_statement,
                binding->name, binding->declaration_offset, cursor, &fact)) continue;
        ConstantValue limit, step;
        bool known;
        if (!guard_constant(bridge, function, fact.limit, &limit, &known)) return false;
        if (!known || (limit.flags & CONSTANT_AGGREGATE) != 0u ||
            (fact.inclusive ? limit.bits >= count : limit.bits == 0u || limit.bits > count)) continue;
        if (fact.step != NO_ID) {
            if (!guard_constant(bridge, function, fact.step, &step, &known)) return false;
            if (!known || (step.flags & CONSTANT_AGGREGATE) != 0u || step.bits == 0u || step.bits > UINT32_MAX) continue;
        }
        return true;
    }
    return false;
}

static bool arguments(DxilSourceBridge *bridge, DxilSourceFunction *function,
    const uint32_t *values, uint32_t count, DxilIrRange *range) {
    DxilIrFunction *view = function->view;
    if (bridge->status != DXIL_OK) return false;
    if (count > DXIL_IR_MAX_EXPR_ARGS - view->expr_arg_count)
        return dxil_source_native_fail(bridge, 0u, DXIL_OUTPUT_TOO_LARGE,
            "native DXIL bounds argument capacity exceeded");
    *range = (DxilIrRange){view->expr_arg_count, count};
    for (uint32_t i = 0u; i < count; ++i) view->expr_args[view->expr_arg_count++] = values[i];
    return true;
}

uint32_t dxil_source_native_bound_index(DxilSourceBridge *bridge,
    DxilSourceFunction *function, uint32_t index_expression, uint32_t source_index) {
    if (bridge->status != DXIL_OK) return DXIL_IR_INVALID_ID;
    DxilIrFunction *view = function->view;
    if (index_expression >= view->expr_count || view->exprs[index_expression].kind != DXIL_IR_EXPR_INDEX ||
        source_index >= bridge->compiler->expression_count) {
        (void)dxil_source_native_fail(bridge, 0u, DXIL_INVALID_IR,
            "native DXIL index projection identity is invalid");
        return DXIL_IR_INVALID_ID;
    }
    const DxilIrExpr original = view->exprs[index_expression];
    if (original.base >= view->expr_count) {
        (void)dxil_source_native_fail(bridge, 0u, DXIL_INVALID_IR,
            "native DXIL index base identity is invalid");
        return DXIL_IR_INVALID_ID;
    }
    const DxilIrTypeId type = view->exprs[original.base].ty;
    uint32_t count;
    bool runtime;
    /* Canonical type IDs are retained by this view; generated packed-vector
     * helper types never create source index expressions. */
    if (!target_index_extent(bridge->compiler, type, &count, &runtime)) {
        (void)dxil_source_native_fail(bridge, 0u, DXIL_INVALID_IR,
            "native DXIL index has no canonical typed extent");
        return DXIL_IR_INVALID_ID;
    }
    const DxilIrType *base = &bridge->module->types[type];
    for (uint32_t depth = 0u; base->kind == DXIL_IR_TYPE_REF && depth < DXIL_IR_MAX_TYPES; ++depth) {
        if (base->elem >= bridge->module->type_count) {
            (void)dxil_source_native_fail(bridge, 0u, DXIL_INVALID_IR,
                "native DXIL indexed pointer element identity is invalid");
            return DXIL_IR_INVALID_ID;
        }
        base = &bridge->module->types[base->elem];
    }
    if (runtime) {
        switch (view->exprs[original.base].kind) {
            case DXIL_IR_EXPR_GLOBAL_REF: case DXIL_IR_EXPR_MEMBER: case DXIL_IR_EXPR_LOAD:
            case DXIL_IR_EXPR_LOCAL_REF: case DXIL_IR_EXPR_PARAM_REF: case DXIL_IR_EXPR_INDEX:
            case DXIL_IR_EXPR_CALL: break;
            case DXIL_IR_EXPR_BOOL_LIT: case DXIL_IR_EXPR_INT_LIT: case DXIL_IR_EXPR_FLOAT_LIT:
            case DXIL_IR_EXPR_ADDRESS_OF: case DXIL_IR_EXPR_DEREF: case DXIL_IR_EXPR_UNARY:
            case DXIL_IR_EXPR_BINARY: case DXIL_IR_EXPR_CONSTRUCT: return index_expression;
        }
    }
    if (!runtime && base->kind == DXIL_IR_TYPE_ARRAY && local_space(bridge, view, original.base)) {
        ConstantValue value;
        bool known = false;
        if (!dxil_source_native_constant_if_known(bridge, function, source_index, &value, &known)) return DXIL_IR_INVALID_ID;
        if (known && (value.flags & CONSTANT_AGGREGATE) == 0u && value.bits < count) return index_expression;
        if (guarded_index(bridge, function, source_index, count)) return index_expression;
        if (bridge->status != DXIL_OK) return DXIL_IR_INVALID_ID;
    }
    const DxilIrTypeId unsigned_type = dxil_source_native_type(bridge, IR_U32);
    uint32_t maximum;
    if (runtime) {
        DxilIrRange length_arguments;
        if (!arguments(bridge, function, &original.base, 1u, &length_arguments)) return DXIL_IR_INVALID_ID;
        uint32_t length = dxil_source_native_push_expr(bridge, function, (DxilIrExpr){
            .kind = DXIL_IR_EXPR_CALL, .ty = unsigned_type, .category = DXIL_IR_EXPR_VALUE,
            .name = dxil_source_native_text(bridge, "arrayLength"), .call_kind = DXIL_IR_CALL_BUILTIN,
            .args = length_arguments});
        uint32_t one = dxil_source_native_push_expr(bridge, function, (DxilIrExpr){
            .kind = DXIL_IR_EXPR_INT_LIT, .ty = unsigned_type,
            .category = DXIL_IR_EXPR_VALUE, .uint_value = 1u});
        maximum = dxil_source_native_push_expr(bridge, function, (DxilIrExpr){
            .kind = DXIL_IR_EXPR_BINARY, .ty = unsigned_type, .category = DXIL_IR_EXPR_VALUE,
            .binary_op = DXIL_IR_BINARY_SUB, .lhs = length, .rhs = one});
    } else maximum = dxil_source_native_push_expr(bridge, function, (DxilIrExpr){
        .kind = DXIL_IR_EXPR_INT_LIT, .ty = unsigned_type,
        .category = DXIL_IR_EXPR_VALUE, .uint_value = count - 1u});
    uint32_t values[] = {original.index_expr, maximum};
    DxilIrRange clamp_arguments;
    if (!arguments(bridge, function, values, 2u, &clamp_arguments)) return DXIL_IR_INVALID_ID;
    uint32_t clamp = dxil_source_native_push_expr(bridge, function, (DxilIrExpr){
        .kind = DXIL_IR_EXPR_CALL, .ty = unsigned_type, .category = DXIL_IR_EXPR_VALUE,
        .name = dxil_source_native_text(bridge, "min"), .call_kind = DXIL_IR_CALL_BUILTIN,
        .args = clamp_arguments, .robustness_generated = true});
    if (bridge->status != DXIL_OK) return DXIL_IR_INVALID_ID;
    view->exprs[index_expression].index_expr = clamp;
    return index_expression;
}
