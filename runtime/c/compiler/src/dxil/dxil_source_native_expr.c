/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_source_native_internal.h"
#include "dxil_source_native_packed.h"
#include "dxil_source_native_bounds.h"
#include "../sema.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

uint32_t dxil_source_native_push_expr(DxilSourceBridge *bridge,
    DxilSourceFunction *function, DxilIrExpr expression) {
    uint32_t result = DXIL_IR_INVALID_ID;
    if (bridge->status != DXIL_OK) return result;
    DxilStatus status = dxil_ir_append_expr(function->view, &expression, &result);
    if (status != DXIL_OK) {
        (void)dxil_source_native_fail(bridge, 0u, status,
            "native DXIL expression projection capacity exceeded");
    }
    return result;
}

static uint32_t value_expr(DxilSourceBridge *bridge,
    DxilSourceFunction *function, uint32_t id, bool reference) {
    if (bridge->status != DXIL_OK || id == DXIL_IR_INVALID_ID) return DXIL_IR_INVALID_ID;
    if (reference || function->view->exprs[id].category == DXIL_IR_EXPR_VALUE) return id;
    return dxil_source_native_push_expr(bridge, function, (DxilIrExpr){
        .kind = DXIL_IR_EXPR_LOAD, .ty = function->view->exprs[id].ty,
        .category = DXIL_IR_EXPR_VALUE, .inner = id});
}

static bool name_equal(const Source *source, NameSpan name, const char *text) {
    return text != NULL && name.offset <= source->length &&
        name.length <= source->length - name.offset &&
        strlen(text) == name.length &&
        memcmp(source->source + name.offset, text, name.length) == 0;
}

uint32_t dxil_source_native_root(DxilSourceBridge *bridge,
    DxilSourceFunction *function, NameSpan name, bool reference) {
    if (bridge->status != DXIL_OK) return DXIL_IR_INVALID_ID;
    DxilIrFunction *view = function->view;
    DxilIrExpr expression = {.category = DXIL_IR_EXPR_REF};
    for (uint32_t n = function->scope_count; n != 0u; --n) {
        uint32_t local = function->scope[n - 1u];
        if (local >= view->local_count ||
            !source_spans_equal(&bridge->source, name, function->local_names[local])) continue;
        expression.kind = DXIL_IR_EXPR_LOCAL_REF;
        expression.ty = view->locals[local].ty;
        expression.index = local;
        return value_expr(bridge, function,
            dxil_source_native_push_expr(bridge, function, expression), reference);
    }
    for (uint32_t i = 0u; i < view->param_count; ++i) {
        if (!name_equal(&bridge->source, name, view->params[i].name)) continue;
        expression.kind = DXIL_IR_EXPR_PARAM_REF;
        expression.ty = view->params[i].ty;
        expression.index = i;
        return value_expr(bridge, function,
            dxil_source_native_push_expr(bridge, function, expression), reference);
    }
    for (uint32_t i = 0u; i < bridge->module->global_count; ++i) {
        const DxilIrGlobal *global = &bridge->module->globals[i];
        if (!name_equal(&bridge->source, name, global->name)) continue;
        expression.kind = DXIL_IR_EXPR_GLOBAL_REF;
        expression.ty = global->ty;
        expression.index = i;
        if (global->ty >= bridge->module->type_count) break;
        DxilIrTypeKind kind = bridge->module->types[global->ty].kind;
        bool handle = kind >= DXIL_IR_TYPE_SAMPLER && kind <= DXIL_IR_TYPE_STORAGE_TEXTURE_2D;
        if (global->class == DXIL_IR_GLOBAL_CONST || handle) expression.category = DXIL_IR_EXPR_VALUE;
        return value_expr(bridge, function,
            dxil_source_native_push_expr(bridge, function, expression), reference);
    }
    (void)dxil_source_native_fail(bridge, name.offset, DXIL_INVALID_IR,
        "typed source identity has no native DXIL projection binding");
    return DXIL_IR_INVALID_ID;
}

static bool constructor(const DxilSourceBridge *bridge, const Expression *expression) {
    NameSpan name = expression->value.call.callee;
    if (source_span_is(&bridge->source, name, "bitcast")) return false;
    if (expression->kind == EXPRESSION_CONSTRUCT ||
        expression->value.call.type.offset != NO_ID ||
        sema_find_type_alias(&bridge->source, name) != NO_ID ||
        sema_find_struct(&bridge->source, name) != NO_ID) return true;
    if (source_span_is(&bridge->source, name, "array") ||
        source_span_is(&bridge->source, name, "bool") ||
        source_span_is(&bridge->source, name, "i32") ||
        source_span_is(&bridge->source, name, "u32") ||
        source_span_is(&bridge->source, name, "f32") ||
        source_span_is(&bridge->source, name, "f16")) return true;
    uint32_t columns, rows;
    if (type_matrix_name(&bridge->source, name, &columns, &rows)) return true;
    const char *text = bridge->source.source + name.offset;
    return (name.length == 4u || name.length == 5u) &&
        memcmp(text, "vec", 3u) == 0 && text[3] >= '2' && text[3] <= '4';
}

static bool binary(ExpressionKind kind, Comparison comparison, DxilIrBinaryOp *out) {
    switch (kind) {
        case EXPRESSION_ADD: *out = DXIL_IR_BINARY_ADD; return true;
        case EXPRESSION_SUBTRACT: *out = DXIL_IR_BINARY_SUB; return true;
        case EXPRESSION_MULTIPLY: *out = DXIL_IR_BINARY_MUL; return true;
        case EXPRESSION_DIVIDE: *out = DXIL_IR_BINARY_DIV; return true;
        case EXPRESSION_REMAINDER: *out = DXIL_IR_BINARY_REM; return true;
        case EXPRESSION_BIT_AND: *out = DXIL_IR_BINARY_BIT_AND; return true;
        case EXPRESSION_BIT_OR: *out = DXIL_IR_BINARY_BIT_OR; return true;
        case EXPRESSION_BIT_XOR: *out = DXIL_IR_BINARY_BIT_XOR; return true;
        case EXPRESSION_SHIFT_LEFT: *out = DXIL_IR_BINARY_SHIFT_LEFT; return true;
        case EXPRESSION_SHIFT_RIGHT: *out = DXIL_IR_BINARY_SHIFT_RIGHT; return true;
        case EXPRESSION_AND: *out = DXIL_IR_BINARY_LOGICAL_AND; return true;
        case EXPRESSION_OR: *out = DXIL_IR_BINARY_LOGICAL_OR; return true;
        case EXPRESSION_COMPARE:
            switch (comparison) {
                case COMPARE_EQUAL: *out = DXIL_IR_BINARY_EQUAL; return true;
                case COMPARE_NOT_EQUAL: *out = DXIL_IR_BINARY_NOT_EQUAL; return true;
                case COMPARE_LESS: *out = DXIL_IR_BINARY_LESS; return true;
                case COMPARE_LESS_EQUAL: *out = DXIL_IR_BINARY_LESS_EQUAL; return true;
                case COMPARE_GREATER: *out = DXIL_IR_BINARY_GREATER; return true;
                case COMPARE_GREATER_EQUAL: *out = DXIL_IR_BINARY_GREATER_EQUAL; return true;
            }
            return false;
        case EXPRESSION_LOAD: case EXPRESSION_INTEGER: case EXPRESSION_DECIMAL:
        case EXPRESSION_NAME: case EXPRESSION_MEMBER: case EXPRESSION_MAX:
        case EXPRESSION_ABSTRACT_INTEGER: case EXPRESSION_BOOLEAN:
        case EXPRESSION_NOT: case EXPRESSION_LITERAL: case EXPRESSION_NEGATE:
        case EXPRESSION_COMPLEMENT: case EXPRESSION_CONSTRUCT:
        case EXPRESSION_BITCAST: case EXPRESSION_CALL:
        case EXPRESSION_ADDRESS: case EXPRESSION_DEREFERENCE: return false;
    }
    return false;
}

/* Retained IR stores an f64 token payload even for f32/f16-typed literals.
 * Parse the unsuffixed token with the canonical numeric owner, not strtod or
 * a second WGSL lexer. Semantic validation already admitted the original. */
static bool literal(DxilSourceBridge *bridge, NameSpan span, NumericValue *value) {
    if (!numeric_parse_literal(&bridge->source, span, value)) return false;
    if (value->type != IR_F32 && value->type != IR_F16) return true;
    const char *text = bridge->source.source + span.offset;
    size_t length = span.length;
    if (length != 0u && (text[length - 1u] == 'f' || text[length - 1u] == 'h')) --length;
    bool hexadecimal = length > 1u && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
    bool float_syntax = false;
    for (size_t i = 0u; i < length; ++i) {
        if (text[i] == '.' || (hexadecimal ? text[i] == 'p' || text[i] == 'P'
                : text[i] == 'e' || text[i] == 'E')) float_syntax = true;
    }
    char *normalized = malloc(length + 3u);
    if (normalized == NULL) return dxil_source_native_fail(bridge, span.offset,
        DXIL_OUT_OF_MEMORY, "native DXIL literal projection allocation failed");
    memcpy(normalized, text, length);
    if (!float_syntax) { normalized[length++] = '.'; normalized[length++] = '0'; }
    normalized[length] = '\0';
    Source input = bridge->source;
    input.source = normalized;
    input.length = length;
    bool ok = numeric_parse_literal(&input, (NameSpan){0u, (uint32_t)length}, value);
    free(normalized);
    return ok;
}

static bool reserve_arguments(DxilSourceBridge *bridge, DxilSourceFunction *function,
    uint32_t count, uint32_t offset, DxilIrRange *range) {
    if (bridge->status != DXIL_OK) return false;
    DxilIrFunction *view = function->view;
    if (count > DXIL_IR_MAX_EXPR_ARGS - view->expr_arg_count) {
        return dxil_source_native_fail(bridge, offset, DXIL_OUTPUT_TOO_LARGE,
            "native DXIL argument projection capacity exceeded");
    }
    *range = (DxilIrRange){view->expr_arg_count, count};
    view->expr_arg_count += count;
    return true;
}

static uint32_t member_index(DxilSourceBridge *bridge, DxilIrTypeId type,
    NameSpan name, const char *text) {
    if (bridge->status != DXIL_OK) return DXIL_IR_INVALID_ID;
    if (type >= bridge->module->type_count) {
        (void)dxil_source_native_fail(bridge, name.offset, DXIL_INVALID_IR,
            "typed member base type is invalid");
        return DXIL_IR_INVALID_ID;
    }
    const DxilIrType *base = &bridge->module->types[type];
    if (base->kind == DXIL_IR_TYPE_STRUCT && base->struct_id < bridge->module->struct_count) {
        const DxilIrStructDef *structure = &bridge->module->structs[base->struct_id];
        for (uint32_t i = 0u; i < structure->field_count; ++i) {
            if (strcmp(structure->fields[i].name, text) == 0) return i;
        }
    } else if (base->kind == DXIL_IR_TYPE_VECTOR && name.length != 0u) {
        const char *component = strchr("xyzw", text[0]);
        if (component != NULL) return (uint32_t)(component - "xyzw");
        component = strchr("rgba", text[0]);
        if (component != NULL) return (uint32_t)(component - "rgba");
    }
    char detail[160];
    (void)snprintf(detail, sizeof(detail),
        "typed member `%s` has no native DXIL field projection for type %u (kind %u)",
        text, type, (unsigned)base->kind);
    (void)dxil_source_native_fail(bridge, name.offset, DXIL_INVALID_IR, detail);
    return DXIL_IR_INVALID_ID;
}

/* Canonical reference-path resolution does not cache resolved_type on every
 * intermediate syntax node. Read the already-resolved aggregate relationship
 * instead of treating that unused syntax word as a semantic type identity. */
static DxilIrTypeId access_type(DxilSourceBridge *bridge, DxilIrTypeId base,
    bool member, uint32_t field, uint32_t width, uint32_t offset) {
    if (base >= bridge->module->type_count) return DXIL_IR_INVALID_ID;
    const DxilIrType *type = &bridge->module->types[base];
    if (member && type->kind == DXIL_IR_TYPE_STRUCT && type->struct_id < bridge->module->struct_count) {
        const DxilIrStructDef *structure = &bridge->module->structs[type->struct_id];
        if (field < structure->field_count) return structure->fields[field].ty;
    } else if (type->kind == DXIL_IR_TYPE_VECTOR) {
        if (!member || width == 1u) return type->elem;
        for (uint32_t i = 0u; i < bridge->module->type_count; ++i) {
            const DxilIrType *candidate = &bridge->module->types[i];
            if (candidate->kind == DXIL_IR_TYPE_VECTOR && candidate->elem == type->elem &&
                candidate->len == width) return i;
        }
    } else if (!member && type->kind == DXIL_IR_TYPE_ARRAY) return type->elem;
    else if (!member && type->kind == DXIL_IR_TYPE_MATRIX) {
        for (uint32_t i = 0u; i < bridge->module->type_count; ++i) {
            const DxilIrType *candidate = &bridge->module->types[i];
            if (candidate->kind == DXIL_IR_TYPE_VECTOR && candidate->elem == type->elem &&
                candidate->len == type->rows) return i;
        }
    }
    (void)dxil_source_native_fail(bridge, offset, DXIL_INVALID_IR,
        "typed aggregate access has no native DXIL result identity");
    return DXIL_IR_INVALID_ID;
}

uint32_t dxil_source_native_expr(DxilSourceBridge *bridge,
    DxilSourceFunction *function, uint32_t id, bool reference, unsigned depth) {
    if (bridge->status != DXIL_OK) return DXIL_IR_INVALID_ID;
    if (id >= bridge->compiler->expression_count || depth >= DXIL_IR_MAX_EXPRS) {
        (void)dxil_source_native_fail(bridge, 0u, DXIL_OUTPUT_TOO_LARGE,
            "native DXIL expression traversal exceeded its explicit bound");
        return DXIL_IR_INVALID_ID;
    }
    const Expression *expression = &bridge->compiler->expressions[id];
    DxilIrFunction *view = function->view;
    DxilIrExpr output = {.ty = dxil_source_native_type(bridge, expression->resolved_type),
        .category = DXIL_IR_EXPR_VALUE};
    if (bridge->status != DXIL_OK) return DXIL_IR_INVALID_ID;
    DxilIrBinaryOp operation;
    const bool is_binary = binary(expression->kind, expression->value.comparison, &operation);
    /* The retained rewrite only folds concrete scalar integer arithmetic, not
     * abstract arithmetic, comparisons or floating-point trees. Ask the shared
     * constant owner for its validated value; never duplicate numeric rules. */
    if (!reference && (expression->resolved_type == IR_U32 || expression->resolved_type == IR_I32) &&
        (is_binary || expression->kind == EXPRESSION_NEGATE || expression->kind == EXPRESSION_COMPLEMENT)) {
        ConstantValue constant;
        bool known = false;
        if (!dxil_source_native_constant_if_known(bridge, function, id, &constant, &known))
            return DXIL_IR_INVALID_ID;
        if (known && (constant.flags & CONSTANT_AGGREGATE) == 0u) {
            output.kind = DXIL_IR_EXPR_INT_LIT;
            output.uint_value = constant.bits;
            return dxil_source_native_push_expr(bridge, function, output);
        }
    }
    if (is_binary) {
        output.kind = DXIL_IR_EXPR_BINARY;
        output.binary_op = operation;
        output.lhs = dxil_source_native_expr(bridge, function, expression->left, false, depth + 1u);
        output.rhs = dxil_source_native_expr(bridge, function, expression->right, false, depth + 1u);
    } else switch (expression->kind) {
        case EXPRESSION_NAME:
            return dxil_source_native_root(bridge, function, expression->value.name, reference);
        case EXPRESSION_BOOLEAN:
            output.kind = DXIL_IR_EXPR_BOOL_LIT;
            output.bool_value = expression->value.integer != 0u;
            break;
        case EXPRESSION_LITERAL: {
            NumericValue value;
            if (!literal(bridge, expression->value.name, &value)) {
                (void)dxil_source_native_fail(bridge, expression->source_offset, DXIL_INVALID_IR,
                    "typed literal payload could not be projected to native DXIL");
                return DXIL_IR_INVALID_ID;
            }
            if (value.type == IR_ABSTRACT_FLOAT || value.type == IR_F32 || value.type == IR_F16) {
                output.kind = DXIL_IR_EXPR_FLOAT_LIT;
                if (value.type == IR_ABSTRACT_FLOAT) memcpy(&output.float_value, &value.bits, sizeof(double));
                else if (value.type == IR_F32) {
                    uint32_t bits = (uint32_t)value.bits;
                    float number;
                    memcpy(&number, &bits, sizeof(number));
                    output.float_value = number;
                } else output.float_value = numeric_half_value(value.bits);
            } else { output.kind = DXIL_IR_EXPR_INT_LIT; output.uint_value = value.bits; }
            break;
        }
        case EXPRESSION_INTEGER: case EXPRESSION_ABSTRACT_INTEGER:
            output.kind = DXIL_IR_EXPR_INT_LIT; output.uint_value = expression->value.integer; break;
        case EXPRESSION_DECIMAL: {
            uint32_t bits = expression->value.integer;
            float value;
            memcpy(&value, &bits, sizeof(value));
            output.kind = DXIL_IR_EXPR_FLOAT_LIT; output.float_value = value; break;
        }
        case EXPRESSION_MEMBER: case EXPRESSION_LOAD: {
            uint32_t base = expression->left == NO_ID
                ? dxil_source_native_root(bridge, function, expression->kind == EXPRESSION_MEMBER
                    ? expression->value.member.object : expression->value.reference.storage, true)
                : dxil_source_native_expr(bridge, function, expression->left, true, depth + 1u);
            if (base == DXIL_IR_INVALID_ID) return base;
            output.base = base;
            output.category = view->exprs[base].category;
            if (expression->kind == EXPRESSION_LOAD) {
                output.kind = DXIL_IR_EXPR_INDEX;
                output.ty = access_type(bridge, view->exprs[base].ty, false, 0u, 0u,
                    expression->source_offset);
                output.index_expr = dxil_source_native_expr(bridge, function,
                    expression->value.reference.index, false, depth + 1u);
            } else {
                output.kind = DXIL_IR_EXPR_MEMBER;
                output.field_name = dxil_source_native_name(bridge, expression->value.member.member);
                output.field_index = member_index(bridge, view->exprs[base].ty,
                    expression->value.member.member, output.field_name);
                if (bridge->status != DXIL_OK) return DXIL_IR_INVALID_ID;
                output.ty = access_type(bridge, view->exprs[base].ty, true, output.field_index,
                    expression->value.member.member.length, expression->source_offset);
                if (bridge->module->types[view->exprs[base].ty].kind == DXIL_IR_TYPE_VECTOR &&
                    expression->value.member.member.length > 1u) {
                    output.category = DXIL_IR_EXPR_VALUE;
                    output.base = value_expr(bridge, function, base, false);
                }
            }
            uint32_t projected = dxil_source_native_push_expr(bridge, function, output);
            if (expression->kind == EXPRESSION_LOAD) projected = dxil_source_native_bound_index(
                bridge, function, projected, expression->value.reference.index);
            return value_expr(bridge, function, projected, reference);
        }
        case EXPRESSION_NOT: case EXPRESSION_NEGATE: case EXPRESSION_COMPLEMENT:
            output.kind = DXIL_IR_EXPR_UNARY;
            output.unary_op = expression->kind == EXPRESSION_NOT ? DXIL_IR_UNARY_NOT
                : expression->kind == EXPRESSION_NEGATE ? DXIL_IR_UNARY_NEG : DXIL_IR_UNARY_BIT_NOT;
            output.operand = dxil_source_native_expr(bridge, function, expression->left, false, depth + 1u);
            break;
        case EXPRESSION_CALL: case EXPRESSION_CONSTRUCT: case EXPRESSION_BITCAST: {
            bool is_constructor = constructor(bridge, expression);
            output.kind = is_constructor ? DXIL_IR_EXPR_CONSTRUCT : DXIL_IR_EXPR_CALL;
            output.name = dxil_source_native_name(bridge, expression->value.call.callee);
            if (bridge->status != DXIL_OK || output.name == NULL) return DXIL_IR_INVALID_ID;
            output.call_kind = DXIL_IR_CALL_BUILTIN;
            output.construct_ty = output.ty;
            for (uint32_t i = 0u; i < bridge->compiler->function_count; ++i) {
                if (source_spans_equal(&bridge->source, expression->value.call.callee,
                        bridge->compiler->functions[i].name)) output.call_kind = DXIL_IR_CALL_USER;
            }
            if (!reserve_arguments(bridge, function, expression->right,
                    expression->source_offset, &output.args)) return DXIL_IR_INVALID_ID;
            uint32_t cursor = expression->left;
            for (uint32_t i = 0u; i < expression->right; ++i) {
                if (cursor >= bridge->compiler->argument_count) {
                    (void)dxil_source_native_fail(bridge, expression->source_offset, DXIL_INVALID_IR,
                        "typed call argument chain is invalid");
                    return DXIL_IR_INVALID_ID;
                }
                uint32_t argument = bridge->compiler->arguments[cursor].expression;
                bool ref_argument = !is_constructor && output.call_kind == DXIL_IR_CALL_BUILTIN &&
                    i == 0u && (strncmp(output.name, "atomic", 6u) == 0 || strcmp(output.name, "arrayLength") == 0);
                if (ref_argument && argument < bridge->compiler->expression_count &&
                    bridge->compiler->expressions[argument].kind == EXPRESSION_ADDRESS)
                    argument = bridge->compiler->expressions[argument].left;
                view->expr_args[output.args.start + i] = dxil_source_native_expr(bridge,
                    function, argument, ref_argument, depth + 1u);
                cursor = bridge->compiler->arguments[cursor].next;
            }
            if (bridge->status != DXIL_OK) return DXIL_IR_INVALID_ID;
            if (!is_constructor && output.call_kind == DXIL_IR_CALL_BUILTIN &&
                strcmp(output.name, "ldexp") == 0 && output.args.len == 2u) {
                uint32_t first = view->expr_args[output.args.start];
                DxilIrTypeId type = view->exprs[first].ty;
                if (bridge->module->types[type].kind == DXIL_IR_TYPE_VECTOR)
                    type = bridge->module->types[type].elem;
                if (bridge->module->types[type].kind == DXIL_IR_TYPE_SCALAR &&
                    bridge->module->types[type].scalar == DXIL_IR_SCALAR_ABSTRACT_INT) {
                    DxilIrRange arguments;
                    if (!reserve_arguments(bridge, function, 1u, expression->source_offset, &arguments)) return DXIL_IR_INVALID_ID;
                    view->expr_args[arguments.start] = first;
                    view->expr_args[output.args.start] = dxil_source_native_push_expr(bridge, function,
                        (DxilIrExpr){.kind = DXIL_IR_EXPR_CONSTRUCT, .ty = output.ty,
                            .category = DXIL_IR_EXPR_VALUE, .construct_ty = output.ty, .args = arguments});
                }
            }
            if (!is_constructor && output.call_kind == DXIL_IR_CALL_BUILTIN) {
                const NumericBuiltin *builtin = builtin_named(&bridge->source,
                    expression->value.call.callee);
                if (builtin != NULL && builtin->family == BUILTIN_PACKED_INTEGER) {
                    /* Retained packed lowering materializes abstract scalar
                     * arguments before expansion. Reuse canonical constant
                     * evaluation, including lexical const bindings. */
                    uint32_t argument_cursor = expression->left;
                    for (uint32_t i = 0u; !packed_pack(builtin->kind) && i < output.args.len; ++i) {
                        uint32_t projected = view->expr_args[output.args.start + i];
                        DxilIrTypeId type = view->exprs[projected].ty;
                        if (bridge->module->types[type].kind == DXIL_IR_TYPE_SCALAR &&
                            bridge->module->types[type].scalar == DXIL_IR_SCALAR_ABSTRACT_INT) {
                            ConstantValue value, materialized;
                            uint32_t original = bridge->compiler->arguments[argument_cursor].expression;
                            if (!dxil_source_native_constant(bridge, function, original, &value) ||
                                !constant_materialize(&bridge->source, expression->source_offset,
                                    value, IR_U32, &materialized)) {
                                (void)dxil_source_native_fail(bridge, expression->source_offset, DXIL_INVALID_IR,
                                    "packed argument constant could not be materialized");
                                return DXIL_IR_INVALID_ID;
                            }
                            view->expr_args[output.args.start + i] = dxil_source_native_push_expr(bridge, function,
                                (DxilIrExpr){.kind = DXIL_IR_EXPR_INT_LIT,
                                    .ty = dxil_source_native_type(bridge, IR_U32),
                                    .category = DXIL_IR_EXPR_VALUE, .uint_value = materialized.bits});
                        }
                        argument_cursor = bridge->compiler->arguments[argument_cursor].next;
                    }
                    return dxil_source_native_packed(bridge, function, builtin->kind, &output);
                }
            }
            break;
        }
        case EXPRESSION_ADDRESS: case EXPRESSION_DEREFERENCE:
            output.kind = expression->kind == EXPRESSION_ADDRESS ? DXIL_IR_EXPR_ADDRESS_OF : DXIL_IR_EXPR_DEREF;
            output.inner = dxil_source_native_expr(bridge, function, expression->left,
                expression->kind == EXPRESSION_ADDRESS, depth + 1u);
            if (expression->kind == EXPRESSION_DEREFERENCE) {
                output.category = DXIL_IR_EXPR_REF;
                return value_expr(bridge, function,
                    dxil_source_native_push_expr(bridge, function, output), reference);
            }
            break;
        case EXPRESSION_MAX:
            output.kind = DXIL_IR_EXPR_CALL;
            output.name = dxil_source_native_text(bridge, "max");
            output.call_kind = DXIL_IR_CALL_BUILTIN;
            if (!reserve_arguments(bridge, function, 2u, expression->source_offset, &output.args)) return DXIL_IR_INVALID_ID;
            view->expr_args[output.args.start] = dxil_source_native_expr(bridge, function, expression->left, false, depth + 1u);
            view->expr_args[output.args.start + 1u] = dxil_source_native_expr(bridge, function, expression->right, false, depth + 1u);
            break;
        case EXPRESSION_ADD: case EXPRESSION_SUBTRACT: case EXPRESSION_MULTIPLY:
        case EXPRESSION_COMPARE: case EXPRESSION_AND: case EXPRESSION_OR:
        case EXPRESSION_DIVIDE: case EXPRESSION_REMAINDER: case EXPRESSION_BIT_AND:
        case EXPRESSION_BIT_OR: case EXPRESSION_BIT_XOR: case EXPRESSION_SHIFT_LEFT:
        case EXPRESSION_SHIFT_RIGHT:
            (void)dxil_source_native_fail(bridge, expression->source_offset, DXIL_INVALID_IR,
                "typed binary operation has no native DXIL projection");
            return DXIL_IR_INVALID_ID;
    }
    return dxil_source_native_push_expr(bridge, function, output);
}
