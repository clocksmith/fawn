/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_source_internal.h"
#include "../sema.h"
#include <string.h>
#include <stdlib.h>

uint32_t tsir_source_push_expr(TsirSourceBridge *b, TsirSourceFunction *f, TsirIrExpr e) {
    if (f->view.expr_count == f->expr_cap) {
        (void)tsir_source_fail(b, 0u, "TSIR expression adaptation capacity exceeded");
        return TSIR_IR_NONE;
    }
    uint32_t id = (uint32_t)f->view.expr_count++;
    f->exprs[id] = e;
    return id;
}
static uint32_t load(TsirSourceBridge *b, TsirSourceFunction *f, uint32_t id, bool reference) {
    if (reference || id == TSIR_IR_NONE) return id;
    return tsir_source_push_expr(b, f, (TsirIrExpr){.kind = TSIR_IR_EXPR_LOAD, .ty = f->exprs[id].ty, .inner = id});
}
uint32_t tsir_source_root(TsirSourceBridge *b, TsirSourceFunction *f, NameSpan name, bool reference) {
    for (size_t n = f->scope_count; n; --n) {
        uint32_t i = f->scope[n - 1u];
        if (!source_spans_equal(&b->source, name, f->local_names[i])) continue;
        uint32_t id = tsir_source_push_expr(b, f, (TsirIrExpr){.kind = TSIR_IR_EXPR_LOCAL_REF,
            .ty = f->locals[i].ty, .ref_index = i});
        return load(b, f, id, reference);
    }
    for (size_t i = 0u; i < f->view.param_count; ++i) {
        const char *name_text = f->params[i].name;
        if (strlen(name_text) == name.length && !memcmp(b->source.source + name.offset, name_text, name.length))
            return load(b, f, tsir_source_push_expr(b, f, (TsirIrExpr){.kind = TSIR_IR_EXPR_PARAM_REF,
                .ty = f->params[i].ty, .ref_index = (uint32_t)i}), reference);
    }
    for (size_t i = 0u; i < b->module.global_count; ++i) {
        const TsirIrGlobal *global = &b->globals[i];
        if (strlen(global->name) != name.length ||
            memcmp(b->source.source + name.offset, global->name, name.length)) continue;
        uint32_t result = tsir_source_push_expr(b, f, (TsirIrExpr){
            .kind = TSIR_IR_EXPR_GLOBAL_REF, .ty = global->ty, .ref_index = (uint32_t)i});
        bool mutable = global->class != TSIR_IR_GLOBAL_CONST &&
            (!global->has_addr_space || global->addr_space != TSIR_IR_SPACE_HANDLE);
        return mutable ? load(b, f, result, reference) : result;
    }
    (void)tsir_source_fail(b, name.offset, "TSIR source identity has no adapted binding");
    return TSIR_IR_NONE;
}
/* Retained sema.zig resolve_ident_expr classifies every local/parameter
 * as ref, including immutable lets; only const globals and handles are values.
 * Mutability governs writes, not the IR load category. */
static bool is_reference(const TsirSourceBridge *b, const TsirSourceFunction *f, uint32_t id) {
    for (size_t depth = 0; depth <= f->view.expr_count && id < f->view.expr_count; ++depth) {
        const TsirIrExpr *e = &f->exprs[id];
        if (e->kind == TSIR_IR_EXPR_LOCAL_REF || e->kind == TSIR_IR_EXPR_PARAM_REF) return true;
        if (e->kind == TSIR_IR_EXPR_GLOBAL_REF) {
            const TsirIrGlobal *g = &b->globals[e->ref_index];
            return g->class != TSIR_IR_GLOBAL_CONST &&
                (!g->has_addr_space || g->addr_space != TSIR_IR_SPACE_HANDLE);
        }
        if (e->kind == TSIR_IR_EXPR_DEREF) return true;
        if (e->kind == TSIR_IR_EXPR_MEMBER) id = e->member_base;
        else if (e->kind == TSIR_IR_EXPR_INDEX) id = e->index_base;
        else return false;
    }
    return false;
}
static bool constructor(const TsirSourceBridge *b, const Expression *e) {
    NameSpan name = e->value.call.callee;
    if (sema_find_type_alias(&b->source, name) != NO_ID ||
        sema_find_struct(&b->source, name) != NO_ID) return true;
    if (source_span_is(&b->source, name, "bitcast")) return false;
    if (e->value.call.type.offset != NO_ID) return true;
    if (source_span_is(&b->source, name, "array") ||
        source_span_is(&b->source, name, "bool") ||
        source_span_is(&b->source, name, "f32") ||
        source_span_is(&b->source, name, "f16") ||
        source_span_is(&b->source, name, "i32") ||
        source_span_is(&b->source, name, "u32")) return true;
    uint32_t columns, rows;
    if (type_matrix_name(&b->source, name, &columns, &rows)) return true;
    const char *text = b->source.source + name.offset;
    return (name.length == 4u || name.length == 5u) &&
        !memcmp(text, "vec", 3u) && text[3] >= '2' && text[3] <= '4';
}
static bool binary(ExpressionKind kind, Comparison cmp, TsirIrBinaryOp *out) {
    switch (kind) {
        case EXPRESSION_ADD: *out = TSIR_IR_BIN_ADD; return true;
        case EXPRESSION_SUBTRACT: *out = TSIR_IR_BIN_SUB; return true;
        case EXPRESSION_MULTIPLY: *out = TSIR_IR_BIN_MUL; return true;
        case EXPRESSION_DIVIDE: *out = TSIR_IR_BIN_DIV; return true;
        case EXPRESSION_REMAINDER: *out = TSIR_IR_BIN_REM; return true;
        case EXPRESSION_BIT_AND: *out = TSIR_IR_BIN_BIT_AND; return true;
        case EXPRESSION_BIT_OR: *out = TSIR_IR_BIN_BIT_OR; return true;
        case EXPRESSION_BIT_XOR: *out = TSIR_IR_BIN_BIT_XOR; return true;
        case EXPRESSION_SHIFT_LEFT: *out = TSIR_IR_BIN_SHIFT_LEFT; return true;
        case EXPRESSION_SHIFT_RIGHT: *out = TSIR_IR_BIN_SHIFT_RIGHT; return true;
        case EXPRESSION_AND: *out = TSIR_IR_BIN_LOGICAL_AND; return true;
        case EXPRESSION_OR: *out = TSIR_IR_BIN_LOGICAL_OR; return true;
        case EXPRESSION_COMPARE:
            switch (cmp) {
                case COMPARE_EQUAL: *out = TSIR_IR_BIN_EQUAL; return true;
                case COMPARE_NOT_EQUAL: *out = TSIR_IR_BIN_NOT_EQUAL; return true;
                case COMPARE_LESS: *out = TSIR_IR_BIN_LESS; return true;
                case COMPARE_LESS_EQUAL: *out = TSIR_IR_BIN_LESS_EQUAL; return true;
                case COMPARE_GREATER: *out = TSIR_IR_BIN_GREATER; return true;
                case COMPARE_GREATER_EQUAL: *out = TSIR_IR_BIN_GREATER_EQUAL; return true;
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
/* Retained Zig stores literal payloads as f64 even when the semantic type
 * is f32/f16. Reuse the compiler numeric owner on an unsuffixed decimal/hex
 * float token instead of retaining its already-rounded concrete payload. */
static bool ir_literal(TsirSourceBridge *b, NameSpan span, NumericValue *value) {
    if (!numeric_parse_literal(&b->source, span, value)) return false;
    if (value->type != IR_F32 && value->type != IR_F16) return true;
    const char *token = b->source.source + span.offset;
    size_t length = span.length;
    if (length && (token[length - 1u] == 'f' || token[length - 1u] == 'h')) --length;
    bool float_syntax = false;
    bool hex = length > 1u && token[0] == '0' && (token[1] == 'x' || token[1] == 'X');
    for (size_t i = 0; i < length; ++i)
        if (token[i] == '.' || (hex ? token[i] == 'p' || token[i] == 'P' : token[i] == 'e' || token[i] == 'E'))
            float_syntax = true;
    char *normalized = malloc(length + 3u);
    if (!normalized) { b->status = TSIR_ERR_NOMEM; return false; }
    memcpy(normalized, token, length);
    if (!float_syntax) { normalized[length++] = '.'; normalized[length++] = '0'; }
    normalized[length] = '\0';
    Source input = b->source;
    input.source = normalized;
    input.length = length;
    bool ok = numeric_parse_literal(&input, (NameSpan){0u, (uint32_t)length}, value);
    free(normalized);
    return ok;
}
uint32_t tsir_source_expr(TsirSourceBridge *b, TsirSourceFunction *f, uint32_t id, bool reference, unsigned depth) {
    if (b->status != TSIR_OK) return TSIR_IR_NONE;
    if (id >= b->compiler->expression_count || depth > 64u) {
        (void)tsir_source_fail(b, 0u, "TSIR source expression traversal exceeded its bound");
        return TSIR_IR_NONE;
    }
    const Expression *e = &b->compiler->expressions[id];
    TsirIrExpr o = {.ty = e->resolved_type};
    TsirIrBinaryOp op;
    if (binary(e->kind, e->value.comparison, &op)) {
        o.kind = TSIR_IR_EXPR_BINARY;
        o.binary_op = op;
        o.binary_lhs = tsir_source_expr(b, f, e->left, false, depth + 1u);
        o.binary_rhs = tsir_source_expr(b, f, e->right, false, depth + 1u);
    } else switch (e->kind) {
        case EXPRESSION_NAME: return tsir_source_root(b, f, e->value.name, reference);
        case EXPRESSION_BOOLEAN: o.kind = TSIR_IR_EXPR_BOOL_LIT; o.bool_value = e->value.integer != 0u; break;
        case EXPRESSION_LITERAL: {
            NumericValue value;
            if (!ir_literal(b, e->value.name, &value)) {
                if (b->status == TSIR_OK) b->status = TSIR_ERR_INVALID;
                return TSIR_IR_NONE;
            }
            if (value.type == IR_ABSTRACT_FLOAT || value.type == IR_F32 || value.type == IR_F16) {
                o.kind = TSIR_IR_EXPR_FLOAT_LIT;
                if (value.type == IR_ABSTRACT_FLOAT) memcpy(&o.float_value, &value.bits, sizeof(o.float_value));
                else if (value.type == IR_F32) {
                    uint32_t bits = (uint32_t)value.bits;
                    float v;
                    memcpy(&v, &bits, sizeof(v));
                    o.float_value = v;
                } else o.float_value = numeric_half_value(value.bits);
            } else { o.kind = TSIR_IR_EXPR_INT_LIT; o.int_value = value.bits; }
            break;
        }
        case EXPRESSION_INTEGER: case EXPRESSION_ABSTRACT_INTEGER:
            o.kind = TSIR_IR_EXPR_INT_LIT; o.int_value = e->value.integer; break;
        case EXPRESSION_DECIMAL: {
            uint32_t bits = e->value.integer;
            float value;
            memcpy(&value, &bits, sizeof(value));
            o.kind = TSIR_IR_EXPR_FLOAT_LIT; o.float_value = value;
            break;
        }
        case EXPRESSION_MEMBER: case EXPRESSION_LOAD: {
            uint32_t base = e->left == NO_ID ? tsir_source_root(b, f,
                e->kind == EXPRESSION_MEMBER ? e->value.member.object : e->value.reference.storage, true) :
                tsir_source_expr(b, f, e->left, true, depth + 1u);
            if (base == TSIR_IR_NONE) return base;
            if (e->kind == EXPRESSION_LOAD) {
                o.kind = TSIR_IR_EXPR_INDEX;
                o.index_base = base;
                o.index_index = tsir_source_expr(b, f, e->value.reference.index, false, depth + 1u);
            } else {
                o.kind = TSIR_IR_EXPR_MEMBER;
                o.member_base = base;
                o.member_field_name = tsir_source_name(b, e->value.member.member);
                uint32_t base_type = f->exprs[base].ty;
                if (base_type < b->module.type_count && b->types[base_type].kind == TSIR_IR_TYPE_STRUCT) {
                    const TsirIrStructDef *def = &b->structs[b->types[base_type].struct_id];
                    for (size_t i = 0; i < def->field_count; ++i)
                        if (!strcmp(def->fields[i].name, o.member_field_name)) o.member_field_index = (uint32_t)i;
                } else if (e->value.member.member.length != 0u) {
                    const char *component = strchr("xyzw", o.member_field_name[0]);
                    if (component) o.member_field_index = (uint32_t)(component - "xyzw");
                    else {
                        component = strchr("rgba", o.member_field_name[0]);
                        if (component) o.member_field_index = (uint32_t)(component - "rgba");
                    }
                }
            }
            uint32_t result = tsir_source_push_expr(b, f, o);
            /* Preserve the retained semantic reference category, independently
             * of whether the root declaration permits assignment. */
            return is_reference(b, f, base) ? load(b, f, result, reference) : result;
        }
        case EXPRESSION_NOT: case EXPRESSION_NEGATE: case EXPRESSION_COMPLEMENT:
            o.kind = TSIR_IR_EXPR_UNARY;
            o.unary_op = e->kind == EXPRESSION_NOT ? TSIR_IR_UN_NOT : e->kind == EXPRESSION_NEGATE ? TSIR_IR_UN_NEG : TSIR_IR_UN_BIT_NOT;
            o.unary_operand = tsir_source_expr(b, f, e->left, false, depth + 1u);
            break;
        case EXPRESSION_CALL: case EXPRESSION_CONSTRUCT: {
            o.kind = TSIR_IR_EXPR_CALL;
            o.call_name = tsir_source_name(b, e->value.call.callee);
            o.call_is_builtin = true;
            bool is_constructor = constructor(b, e);
            for (uint32_t i = 0u; i < b->compiler->function_count; ++i)
                if (source_spans_equal(&b->source, e->value.call.callee, b->compiler->functions[i].name)) o.call_is_builtin = false;
            /* Reserve parent arguments before recursion; nested constructors
             * must not splice their edges into their parent's argument range. */
            if (e->right > f->expr_cap - f->view.expr_arg_count) {
                (void)tsir_source_fail(b, e->source_offset, "TSIR argument capacity exceeded");
                return TSIR_IR_NONE;
            }
            o.args_start = (uint32_t)f->view.expr_arg_count;
            o.args_len = e->right;
            f->view.expr_arg_count += e->right;
            uint32_t cursor = e->left;
            for (uint32_t i = 0u; i < e->right; ++i) {
                if (cursor >= b->compiler->argument_count) {
                    (void)tsir_source_fail(b, e->source_offset, "invalid typed argument chain");
                    return TSIR_IR_NONE;
                }
                uint32_t argument = b->compiler->arguments[cursor].expression;
                bool reference_argument = !is_constructor && o.call_is_builtin && i == 0u &&
                    (!strncmp(o.call_name, "atomic", 6u) || !strcmp(o.call_name, "arrayLength"));
                if (reference_argument && argument < b->compiler->expression_count &&
                    b->compiler->expressions[argument].kind == EXPRESSION_ADDRESS)
                    argument = b->compiler->expressions[argument].left;
                f->args[o.args_start + i] = tsir_source_expr(b, f, argument, reference_argument, depth + 1u);
                cursor = b->compiler->arguments[cursor].next;
            }
            if (is_constructor) o.kind = TSIR_IR_EXPR_CONSTRUCT;
            if (!is_constructor && o.call_is_builtin) {
                const NumericBuiltin *builtin = builtin_named(&b->source, e->value.call.callee);
                if (builtin && builtin->kind == BUILTIN_LDEXP && o.args_len == 2u) {
                    uint32_t first = f->args[o.args_start];
                    uint32_t type = f->exprs[first].ty;
                    uint32_t element = type < b->module.type_count && b->types[type].kind == TSIR_IR_TYPE_VECTOR ?
                        b->types[type].elem : type;
                    if (element == IR_ABSTRACT_INT) {
                        if (f->view.expr_arg_count == f->expr_cap) {
                            (void)tsir_source_fail(b, e->source_offset, "TSIR ldexp conversion capacity exceeded");
                            return TSIR_IR_NONE;
                        }
                        uint32_t start = (uint32_t)f->view.expr_arg_count++;
                        f->args[start] = first;
                        f->args[o.args_start] = tsir_source_push_expr(b, f, (TsirIrExpr){
                            .kind = TSIR_IR_EXPR_CONSTRUCT, .ty = o.ty, .args_start = start, .args_len = 1u});
                    }
                }
                if (builtin && builtin->family == BUILTIN_PACKED_INTEGER)
                    return tsir_source_packed(b, f, builtin->kind, &o);
            }
            break;
        }
        case EXPRESSION_ADDRESS: case EXPRESSION_DEREFERENCE:
            o.kind = e->kind == EXPRESSION_ADDRESS ? TSIR_IR_EXPR_ADDRESS_OF : TSIR_IR_EXPR_DEREF;
            o.inner = tsir_source_expr(b, f, e->left, e->kind == EXPRESSION_ADDRESS, depth + 1u);
            if (e->kind == EXPRESSION_DEREFERENCE)
                return load(b, f, tsir_source_push_expr(b, f, o), reference);
            break;
        case EXPRESSION_ADD: case EXPRESSION_SUBTRACT: case EXPRESSION_MULTIPLY:
        case EXPRESSION_COMPARE: case EXPRESSION_AND: case EXPRESSION_OR:
        case EXPRESSION_DIVIDE: case EXPRESSION_REMAINDER: case EXPRESSION_BIT_AND:
        case EXPRESSION_BIT_OR: case EXPRESSION_BIT_XOR: case EXPRESSION_SHIFT_LEFT:
        case EXPRESSION_SHIFT_RIGHT: case EXPRESSION_MAX: case EXPRESSION_BITCAST:
            (void)tsir_source_fail(b, e->source_offset, "typed expression is outside the TSIR source adapter");
            return TSIR_IR_NONE;
    }
    return tsir_source_push_expr(b, f, o);
}
