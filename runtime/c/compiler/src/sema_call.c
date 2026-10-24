/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "sema.h"
#include <string.h>
static bool reserve_operands(Source *s, uint32_t count, uint32_t offset, uint32_t *first) {
    CompilerWorkspace *c = s->compiler;
    if (count>c->limits.operands-c->operand_count)
        return source_pool_error(s, DOE_COMPILER_POOL_IR_OPERANDS, (size_t)c->operand_count+count, offset, "typed operand capacity exceeded");
    *first = c->operand_count;
    c->operand_count+=count;
    return true;
}
static bool convert_value(Source *s, ExpressionValue *a, TypeId target, uint32_t offset, bool bitcast, bool evaluate, bool emit, ExpressionValue *out) {
    CompilerWorkspace *c = s->compiler;
    const uint32_t n = type_vector_width(c, target);
    const TypeId leaf = type_leaf(c, target), original = type_leaf(c, a->constant.type);
    if (type_vector_width(c, a->constant.type) != n || leaf == IR_VOID || leaf>IR_F32 || original == IR_VOID || (a->constant.type >= 7u && c->types[a->constant.type-7u].kind != TYPE_VECTOR) || (bitcast && (leaf == IR_BOOL || original == IR_BOOL)))
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, offset, "no matching scalar or vector conversion");
    if (type_abstract(c, a->constant.type) && (leaf == IR_F32 || bitcast)) {
        TypeId input = original == IR_ABSTRACT_INT?IR_I32:IR_F32;
        if (bitcast && leaf == IR_U32 && original == IR_ABSTRACT_INT) input = IR_U32;
        if (n != 1u && !type_intern(s, TYPE_VECTOR, input, n, &input)) return false;
        if (!sema_materialize(s, a, input)) return false;
    }
    *out = (ExpressionValue){
        .runtime = a->runtime, .overridable = a->overridable, .known = evaluate && a->known && !a->runtime, .constant.type = target, .value = {.type = target, .source_offset = offset}
    };
    if (!a->runtime) {
        if (!out->known) return true;
        ConstantValue lanes[4];
        for (uint32_t i = 0u;i<n;++i) {
            ConstantValue v = a->constant;
            NumericValue r;
            if (n != 1u && !constant_project(s, v, i, &v)) return false;
            const NumericValue input = {v.bits, (IrType)v.type};
            if (!(bitcast?numeric_bitcast(s, offset, input, (IrType)leaf, &r):numeric_convert(s, offset, input, (IrType)leaf, &r))) return false;
            lanes[i] = (ConstantValue){.type = leaf, .bits = r.bits};
        }
        if (n == 1u) {
            out->constant = lanes[0];
            return true;
        }
        return constant_composite(s, target, lanes, n, false, &out->constant);
    }
    if (!sema_ensure_value(s, a, emit)) return false;
    if (emit) {
        uint32_t first = 0u;
        if (!reserve_operands(s, 1u, offset, &first)) return false;
        c->typed_operands[first] = a->value.id;
    }
    if (a->constant.type == target) {
        out->value = a->value;
        return true;
    }
    return sema_emit(s, emit, (IrInstruction){.opcode = bitcast?IR_BITCAST:IR_CONVERT, .type = target, .source_offset = offset, .operands.scalar = {.left = a->value.id, .binding = a->constant.type}}, &out->value);
}
static bool user_call(Source *s, uint32_t id, const LocalScope *scope, uint32_t callee, bool evaluate, bool emit, bool statement, ExpressionValue *out) {
    CompilerWorkspace *c = s->compiler;
    const Expression *node = &c->expressions[id];
    const FunctionRecord *f = &c->functions[callee];
    if (c->current_function == NO_ID) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset, "module const expression cannot call a user function");
    if ((f->flags&FUNCTION_ENTRY) != 0u || node->value.call.type.offset != NO_ID || node->right != f->formal_count || (!statement && f->result_type == IR_VOID) || (statement && (f->flags&FUNCTION_MUST_USE) != 0u))
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset, "invalid function call or result context");
    if (node->right>UINT16_MAX-4u) return source_error(s, DOE_COMPILER_UNSUPPORTED, node->source_offset, "SPIR-V instruction word-count format capacity exceeded");
    if (!source_reference_record(s,&c->expression_references[id],SOURCE_FUNCTION,callee,node->source_offset)) return false;
    uint32_t first = 0u;
    if (emit && !reserve_operands(s, node->right, node->source_offset, &first)) return false;
    uint32_t link = node->left;
    for (uint32_t i = 0u;i<node->right;++i) {
        ExpressionValue a;
        if (!sema_expression(s, c->arguments[link].expression, scope, evaluate, emit, &a) || !sema_materialize(s, &a, c->formals[f->formal_start+i].type.resolved.type) || !sema_ensure_value(s, &a, emit)) return false;
        if (type_pointer(c, a.constant.type) && !sema_pointer_argument(s, c->arguments[link].expression, &a)) return false;
        if (emit) c->typed_operands[first+i] = a.value.id;
        link = c->arguments[link].next;
    }
    *out = (ExpressionValue){
        .runtime = true, .constant.type = f->result_type, .value = {.type = f->result_type, .source_offset = node->source_offset}
    };
    return sema_emit(s, emit, (IrInstruction){.opcode = IR_CALL, .type = f->result_type, .source_offset = node->source_offset, .operands.scalar = {.left = first, .right = node->right, .binding = callee}}, f->result_type == IR_VOID?NULL:&out->value);
}
/* Resolve argument shapes without executing or emitting them, then evaluate the
 * committed overload's operands exactly once in source order. */
static bool aggregate_call(Source *s, const Expression *node, const LocalScope *scope, TypeId target, uint32_t inferred, bool evaluate, bool emit, ExpressionValue *out) {
    CompilerWorkspace *c = s->compiler;
    uint32_t kind = inferred == 1u?TYPE_VECTOR:inferred == 2u?TYPE_ARRAY:inferred == 3u?TYPE_MATRIX:c->types[target-7u].kind;
    uint32_t n = (inferred == 1u || inferred == 3u)?(uint32_t)(s->source[node->value.call.callee.offset+3u]-'0'):inferred == 2u?node->right:c->types[target-7u].count;
    const uint32_t rows = kind == TYPE_MATRIX ? (inferred == 3u ?
        (uint32_t)(s->source[node->value.call.callee.offset + 5u] - '0') :
        type_vector_width(c, c->types[target - 7u].element)) : 0u;
    if (kind == TYPE_MATRIX && node->right == 1u) {
        ExpressionValue a;
        if (!sema_expression(s, c->arguments[node->left].expression, scope, evaluate, emit, &a)) return false;
        if (!type_matrix(c, a.constant.type) || c->types[a.constant.type - 7u].count != n ||
            type_vector_width(c, c->types[a.constant.type - 7u].element) != rows)
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset, "matrix conversion requires identical dimensions");
        if (inferred == 3u) target = a.constant.type;
        if (!sema_materialize(s, &a, target)) return false;
        *out = a;
        return true;
    }
    if (kind == TYPE_RUNTIME_ARRAY || (inferred == 2u && n == 0u) || (target != IR_VOID && !type_constructible(c, target)))
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset, "constructor requires a fixed constructible type");
    if (node->right == 0u) {
        if (inferred == 1u && !type_intern(s, TYPE_VECTOR, IR_ABSTRACT_INT, n, &target)) return false;
        if (inferred == 3u)
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
                "zero matrix construction requires an explicit concrete type");
        *out = (ExpressionValue){
            .known = evaluate, .constant.type = target, .value = {.type = target, .source_offset = node->source_offset}
        };
        return !evaluate || constant_zero(s, target, &out->constant);
    }
    bool runtime = false, overridable = false;
    TypeId common = IR_VOID;
    uint32_t lanes = 0u, link = node->left;
    for (uint32_t i = 0u;i<node->right;++i) {
        ExpressionValue a;
        if (!sema_expression(s, c->arguments[link].expression, scope, false, false, &a)) return false;
        runtime = runtime || a.runtime;
        overridable = overridable || a.overridable;
        if (kind == TYPE_MATRIX) {
            const uint32_t width = type_vector_width(c, a.constant.type);
            const TypeId leaf = type_leaf(c, a.constant.type);
            if ((node->right != n && node->right != n * rows) || width != (node->right == n ? rows : 1u) ||
                (leaf != IR_F32 && leaf != IR_ABSTRACT_FLOAT && leaf != IR_ABSTRACT_INT))
                return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset, "matrix constructor requires whole floating-point columns or scalar elements");
            if (i == 0u) common = leaf;
            else if (!sema_common_type(s, common, leaf, node->source_offset, &common)) return false;
        }
        else if (kind == TYPE_VECTOR) {
            if (a.constant.type >= 7u && c->types[a.constant.type-7u].kind != TYPE_VECTOR)
                return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset, "vector pieces must be scalar or vector");
            const uint32_t part = type_vector_width(c, a.constant.type);
            if (part>n || lanes>n-part) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset, "vector constructor lane count mismatch");
            lanes+=part;
            const TypeId leaf = type_leaf(c, a.constant.type);
            if (i == 0u) common = leaf;
            else if (!sema_common_type(s, common, leaf, node->source_offset, &common)) return false;
        }
        else if (inferred == 2u) {
            if (i == 0u) common = a.constant.type;
            else if (!sema_common_type(s, common, a.constant.type, node->source_offset, &common)) return false;
        }
        link = c->arguments[link].next;
    }
    if (kind == TYPE_VECTOR && node->right == 1u && lanes == n) {
        ExpressionValue a;
        if (!sema_expression(s, c->arguments[node->left].expression, scope, evaluate, emit, &a)) return false;
        if (inferred == 1u) {
            if (a.runtime && emit) {
                uint32_t first = 0u;
                if (!sema_ensure_value(s, &a, true) || !reserve_operands(s, 1u, node->source_offset, &first)) return false;
                c->typed_operands[first] = a.value.id;
            }
            *out = a;
            return true;
        }
        return convert_value(s, &a, target, node->source_offset, false, evaluate, emit, out);
    }
    const bool splat = kind == TYPE_VECTOR && node->right == 1u && lanes == 1u;
    if ((kind == TYPE_VECTOR && !splat && lanes != n) || (kind != TYPE_VECTOR && kind != TYPE_MATRIX && node->right != n))
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset, "aggregate constructor component count mismatch");
    if (inferred != 0u) {
        if (kind == TYPE_MATRIX && common == IR_ABSTRACT_INT) common = IR_ABSTRACT_FLOAT;
        if (runtime && type_abstract(c, common) && !sema_default_type(s, common, node->source_offset, &common)) return false;
        if (kind == TYPE_MATRIX) {
            if (!type_matrix_intern(s, common, n, rows, &target)) return false;
        } else if (!type_intern(s, kind, common, n, &target)) return false;
    }
    *out = (ExpressionValue){
        .runtime = runtime, .overridable = overridable, .known = evaluate && !runtime, .constant.type = target, .value = {.type = target, .source_offset = node->source_offset}
    };
    uint32_t first = 0u;
    if (runtime && emit && !reserve_operands(s, node->right, node->source_offset, &first)) return false;
    if (!runtime && out->known && !constant_reserve(s, target, splat?CONSTANT_REPEAT:CONSTANT_COMPOSITE, n, &out->constant, &first)) return false;
    link = node->left;
    uint32_t lane_index = 0u, column_first = 0u;
    const bool matrix_scalars = kind == TYPE_MATRIX && node->right != n;
    for (uint32_t i = 0u;i<node->right;++i) {
        ExpressionValue a;
        TypeId expected = type_child(c, target, i);
        if (!sema_expression(s, c->arguments[link].expression, scope, evaluate, emit && runtime, &a)) return false;
        if (matrix_scalars) expected = type_leaf(c, c->types[target - 7u].element);
        if (kind == TYPE_VECTOR) {
            expected = type_leaf(c, target);
            const uint32_t part = type_vector_width(c, a.constant.type);
            if (part != 1u && !type_intern(s, TYPE_VECTOR, expected, part, &expected)) return false;
        }
        if (!sema_materialize(s, &a, expected)) return false;
        if (runtime) {
            if (!sema_ensure_value(s, &a, emit)) return false;
            if (emit) c->typed_operands[first+i] = a.value.id;
        }
        else if (evaluate) {
            if (!a.known) {
                if (!a.overridable)
                    return source_error(s, DOE_COMPILER_INPUT_ERROR, node->source_offset, "constant constructor lost its value");
                out->known = false;
                link = c->arguments[link].next;
                continue;
            }
            if (!out->known) {
                link = c->arguments[link].next;
                continue;
            }
            if (matrix_scalars) {
                if (i % rows == 0u && !constant_reserve(s, c->types[target - 7u].element,
                    CONSTANT_COMPOSITE, rows, &c->constant_edges[first + i / rows], &column_first)) return false;
                c->constant_edges[column_first + i % rows] = a.constant;
            }
            else if (kind != TYPE_VECTOR || splat) c->constant_edges[first+i] = a.constant;
            else {
                const uint32_t part = type_vector_width(c, a.constant.type);
                for (uint32_t j = 0u;j<part;++j) {
                    ConstantValue v = a.constant;
                    if (part != 1u && !constant_project(s, v, j, &v)) return false;
                    c->constant_edges[first+lane_index++] = v;
                }
            }
        }
        link = c->arguments[link].next;
    }
    if (!runtime) return true;
    return sema_emit(s, emit, (IrInstruction){.opcode = IR_COMPOSITE, .type = target, .source_offset = node->source_offset, .operands.composite = {first, node->right, kind == TYPE_VECTOR?(splat?2u:1u):matrix_scalars?3u:0u, 0u}}, &out->value);
}
static inline bool sema_builtin_value(Source *source, const Expression *node, const LocalScope *scope,
                        const NumericBuiltin *builtin, bool evaluate, bool emit,
                        ExpressionValue *out) {
    CompilerWorkspace *compiler = source->compiler;
    ExpressionValue arguments[3];
    uint32_t link = node->left, width = 0u;
    bool runtime = builtin_subgroup(builtin), known = evaluate, overridable = false;
    TypeId leaf = IR_VOID;
    for (uint32_t i = 0u; i < builtin->arity; ++i) {
        if (!sema_expression(source, compiler->arguments[link].expression, scope,
                             evaluate, emit, &arguments[i])) return false;
        const TypeId type = arguments[i].constant.type;
        if (builtin->family == BUILTIN_MATRIX) {
            if (!sema_matrix_result(source, builtin->kind, type, node->source_offset, &leaf)) return false;
            runtime = arguments[0].runtime;
            overridable = arguments[0].overridable;
            known = known && arguments[0].known;
            break;
        }
        const TypeId argument_leaf = type_leaf(compiler, type);
        const uint32_t argument_width = type_vector_width(compiler, type);
        if (argument_leaf == IR_VOID ||
            (builtin->family == BUILTIN_BOOLEAN_REDUCTION ? argument_leaf != IR_BOOL : argument_leaf == IR_BOOL) ||
            (i != 0u && argument_width != builtin_argument_width(builtin, i, width)))
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
                                builtin->family == BUILTIN_BOOLEAN_REDUCTION ? "logical builtin requires a boolean scalar or vector" :
                                "numeric builtin arguments require matching scalar or vector shapes");
        if (i == 0u) {
            leaf = argument_leaf;
            width = argument_width;
        } else if (!sema_common_type(source, leaf, argument_leaf, node->source_offset, &leaf)) return false;
        runtime = runtime || arguments[i].runtime;
        overridable = overridable || arguments[i].overridable;
        known = known && arguments[i].known;
        link = compiler->arguments[link].next;
    }
    TypeId operand_type = leaf;
    if (builtin->family != BUILTIN_MATRIX) {
        if (builtin_floating(builtin) && leaf == IR_ABSTRACT_INT) leaf = IR_ABSTRACT_FLOAT;
        if (!builtin_shape(builtin, width) ||
            (builtin->kind == BUILTIN_SIGN && leaf == IR_U32) ||
            (builtin->family == BUILTIN_SUBGROUP_INTEGER && leaf != IR_I32 && leaf != IR_U32 && leaf != IR_ABSTRACT_INT) ||
            (builtin_floating(builtin) && leaf != IR_F32 && leaf != IR_ABSTRACT_FLOAT))
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
                                "no matching numeric builtin overload");
        if (runtime && !sema_default_type(source, leaf, node->source_offset, &leaf)) return false;
        operand_type = leaf;
        if (width != 1u && !type_intern(source, TYPE_VECTOR, leaf, width, &operand_type)) return false;
        for (uint32_t i = 0u; i < builtin->arity; ++i)
            if (!sema_materialize(source, &arguments[i],
                builtin_argument_width(builtin, i, width) == 1u ? leaf : operand_type)) return false;

        if (builtin->kind == BUILTIN_CLAMP && !arguments[1].runtime && !arguments[2].runtime) {
            /* A skipped constant bound still has mandatory creation-time meaning.
             * Force only unknown const expressions, never an effectful runtime input. */
            link = compiler->arguments[node->left].next;
            for (uint32_t i = 1u; i < 3u; ++i) {
                if (!arguments[i].known && !arguments[i].overridable &&
                    (!sema_expression(source, compiler->arguments[link].expression, scope,
                                      true, false, &arguments[i]) ||
                     !sema_materialize(source, &arguments[i], operand_type))) return false;
                link = compiler->arguments[link].next;
            }
            if (arguments[1].known && arguments[2].known && !builtin_clamp_bounds(source, node->source_offset,
                                      &arguments[1].constant, &arguments[2].constant)) return false;
        }

    }
    const TypeId result_type = builtin->family == BUILTIN_MATRIX || builtin_scalar_result(builtin) ? leaf : operand_type;
    *out = (ExpressionValue){
        .runtime = runtime, .overridable = overridable, .known = known && !runtime, .constant.type = result_type,
        .value = {.type = result_type, .source_offset = node->source_offset}
    };
    return sema_builtin_finish(source, node, builtin, arguments, emit, out);
}

static inline bool sema_image_call(Source *s, const Expression *node, const LocalScope *scope, bool evaluate, bool emit,
                     ImageOperation operation, ExpressionValue *out) {
    CompilerWorkspace *c = s->compiler;
    if (c->current_function == NO_ID || node->value.call.type.offset != NO_ID || node->right == 0u)
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset, "texture builtin has invalid stage or result context");
    uint32_t link = node->left, first = 0u;
    if (emit) {
        if (node->right > c->limits.operands - c->operand_count)
            return source_pool_error(s, DOE_COMPILER_POOL_IR_OPERANDS, (size_t)c->operand_count + node->right,
                node->source_offset, "texture operand capacity exceeded");
        first = c->operand_count;
        c->operand_count += node->right;
    }
    TypeId texture = IR_VOID, result = IR_VOID;
    for (uint32_t index = 0u; index < node->right; ++index) {
        if (!sema_expression(s, c->arguments[link].expression, scope, sema_image_evaluate(c, texture, operation, index, evaluate), emit, out)) return false;
        if (index == 0u) {
            texture = out->constant.type;
            if (!type_handle(c, texture) || c->types[texture - 7u].kind != TYPE_TEXTURE)
                return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset, "texture builtin requires a texture handle");
            result = sema_image_result(s, node, &c->types[texture - 7u], operation);
            if (result == NO_ID) return false;
        } else if (!sema_image_argument(s, &c->types[texture - 7u], operation, index, out)) return false;
        if (!sema_ensure_value(s, out, emit)) return false;
        if (emit) c->typed_operands[first + index] = out->value.id;
        link = c->arguments[link].next;
    }
    if (!sema_image_auxiliaries(s, texture, operation)) return false;
    const uint32_t varying = image_implicit(operation) ? SOURCE_IMAGE_VARYING | SOURCE_IMAGE_DERIVATIVE :
        operation == IMAGE_LOAD && c->types[texture - 7u].state.image.access == DOE_COMPILER_READ_WRITE ? SOURCE_IMAGE_VARYING : 0u;
    if (!source_reference_record(s, &c->expression_references[(size_t)(node - c->expressions)], SOURCE_IMAGE,
                                 (uint32_t)operation | varying, node->source_offset)) return false;
    *out = (ExpressionValue){.runtime = true, .constant.type = result,
        .value = {.type = result, .source_offset = node->source_offset}};
    return sema_emit(s, emit, (IrInstruction){.opcode = IR_IMAGE, .type = result,
        .source_offset = node->source_offset, .operands.composite = {first, node->right, (uint32_t)operation, texture}},
        result == IR_VOID ? NULL : &out->value);
}

/* The address is resolved before value arguments. access.value indexes the
 * checked typed-operand span, excluding the address; compare precedes replacement.
 * Co-located call evaluators share one bounded recursive stack frame. */
static inline bool sema_atomic_call(Source *source, const Expression *node, const LocalScope *scope,
                       bool evaluate, bool emit, IrOpcode operation, ExpressionValue *out) {
    CompilerWorkspace *c = source->compiler;
    const uint32_t arity = operation == IR_ATOMIC_LOAD ? 1u : operation == IR_ATOMIC_COMPARE_EXCHANGE ? 3u : 2u;
    if (node->right != arity || node->value.call.type.offset != NO_ID || c->current_function == NO_ID)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
            "atomic builtin requires its exact arguments in a runtime function");
    ValueReference reference;
    uint32_t link = node->left;
    if (!sema_address_reference(source, c->arguments[link].expression, scope, evaluate, emit, &reference)) return false;
    if (!reference.writable || (reference.category != ROOT_RESOURCE && reference.category != ROOT_WORKGROUP) ||
        reference.type < 7u || c->types[reference.type - 7u].kind != TYPE_ATOMIC)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
            "atomic builtin requires a writable storage or workgroup atomic address");
    const TypeId scalar = c->types[reference.type - 7u].element;
    const uint32_t first = c->operand_count;
    if (emit) {
        if (arity - 1u > c->limits.operands - first)
            return source_pool_error(source, DOE_COMPILER_POOL_IR_OPERANDS, (size_t)first + arity - 1u,
                node->source_offset, "atomic operand capacity exceeded");
        c->operand_count += arity - 1u;
    }
    for (uint32_t i = 1u; i < arity; ++i) {
        link = c->arguments[link].next;
        if (!sema_expression(source, c->arguments[link].expression, scope, evaluate, emit, out) ||
            !sema_materialize(source, out, scalar) || !sema_ensure_value(source, out, emit)) return false;
        if (emit) c->typed_operands[first + i - 1u] = out->value.id;
    }
    TypeId result = operation == IR_ATOMIC_STORE ? IR_VOID : scalar;
    if (operation == IR_ATOMIC_COMPARE_EXCHANGE && !type_intern(source, TYPE_ATOMIC_RESULT, scalar, 2u, &result)) return false;
    if (!source_reference_record(source, &c->expression_references[(size_t)(node - c->expressions)],
                                  SOURCE_ATOMIC, 0u, node->source_offset)) return false;
    *out = (ExpressionValue){.runtime = true, .constant.type = result,
        .value = {.type = result, .source_offset = node->source_offset}};
    return sema_emit(source, emit, (IrInstruction){.opcode = operation, .type = result,
        .source_offset = node->source_offset, .operands.access = {reference.root, reference.path, first, reference.category}},
        result == IR_VOID ? NULL : &out->value);
}

bool sema_call_value(Source *s, uint32_t id, const LocalScope *scope, bool evaluate, bool emit, bool statement, ExpressionValue *out) {
    CompilerWorkspace *c = s->compiler;
    const Expression *node = &c->expressions[id];
    const NameSpan name = node->value.call.callee;
    if (sema_find_local(s, scope, name) != NULL || sema_find_formal(s, name) != NO_ID || sema_find_parameter(s, name) != NO_ID || (sema_alias_visible(s) && source_spans_equal(s, name, c->alias)))
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, name.offset, "call target resolves to a lexical value");
    const uint32_t function = sema_find_function(s, name);
    if (function != NO_ID) return user_call(s, id, scope, function, evaluate, emit, statement, out);
    const bool declared_type = sema_find_type_alias(s, name) != NO_ID ||
                               sema_find_struct(s, name) != NO_ID;
    if (!declared_type && sema_module_declared(s, name)) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, name.offset, "call target resolves to a module value");
    if (!declared_type && source_span_is(s, name, "select")) {
        if (statement || node->right != 3u || node->value.call.type.offset != NO_ID)
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, name.offset,
                                "select requires three arguments and a used result");
        /* Keep eager argument evaluation in the existing recursive call owner.
         * The select finisher consumes snapshots and never re-enters source. */
        ExpressionValue arguments[3];
        uint32_t link = node->left;
        for (uint32_t i = 0u; i < 3u; ++i) {
            if (!sema_expression(s, c->arguments[link].expression, scope,
                                 evaluate, emit, &arguments[i])) return false;
            link = c->arguments[link].next;
        }
        return sema_select(s, node, arguments, evaluate, emit, out);
    }
    if (!declared_type && source_span_is(s, name, "arrayLength")) {
        if (statement || node->right != 1u || node->value.call.type.offset != NO_ID)
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, name.offset,
                "arrayLength requires one runtime array pointer and a used result");
        return sema_array_length(s, id, scope, emit, out);
    }
    if (!declared_type && source_span_is(s,name,"workgroupBarrier")) {
        if (!statement || node->right!=0u || node->value.call.type.offset!=NO_ID || c->current_function==NO_ID)
            return source_error(s,DOE_COMPILER_SEMANTIC_ERROR,name.offset,"workgroupBarrier requires no arguments and no result");
        if (!source_reference_record(s,&c->expression_references[id],SOURCE_BARRIER,0u,node->source_offset)) return false;
        *out=(ExpressionValue){.runtime=true,.constant.type=IR_VOID,.value={.type=IR_VOID,.source_offset=name.offset}};
        return sema_emit(s,emit,(IrInstruction){.opcode=IR_WORKGROUP_BARRIER,.source_offset=name.offset},NULL);
    }
    if (!declared_type && (source_span_is(s,name,"storageBarrier") || source_span_is(s,name,"workgroupUniformLoad") ||
        source_span_is(s,name,"textureBarrier")))
        return source_error(s,DOE_COMPILER_UNSUPPORTED,name.offset,"synchronization builtin is outside the workgroup barrier owner");
    if (name.length >= sizeof(((Name *)0)->text))
        return source_error(s, DOE_COMPILER_UNSUPPORTED, name.offset, "identifier length exceeds supported capacity");
    const uint32_t atomic = !declared_type ? atomic_builtin_kind(s, name) : NO_ID;
    if (atomic != NO_ID) {
        if (!statement && atomic == IR_ATOMIC_STORE)
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset, "atomicStore has no result");
        return sema_atomic_call(s, node, scope, evaluate, emit, (IrOpcode)atomic, out);
    }
    const uint32_t image_operation = !declared_type ? image_builtin_kind(s, name) : NO_ID;
    if (image_operation != NO_ID) {
        if (statement != (image_operation == IMAGE_STORE))
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset, "texture builtin has invalid result context");
        return sema_image_call(s, node, scope, evaluate, emit, (ImageOperation)image_operation, out);
    }
    const NumericBuiltin *builtin = !declared_type ? builtin_named(s, name) : NULL;
    if (builtin != NULL) {
        if (statement || node->value.call.type.offset != NO_ID || node->right != builtin->arity)
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
                                "numeric builtin requires its exact arguments and a used result");
        if (builtin_subgroup(builtin)) {
            if ((c->module_flags & MODULE_SUBGROUPS) == 0u || c->current_function == NO_ID)
                return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
                    "subgroup operations require enable subgroups and a runtime function");
            if (!source_reference_record(s, &c->expression_references[id], SOURCE_SUBGROUP,
                                         (uint32_t)builtin->kind, node->source_offset)) return false;
        }
        return sema_builtin_value(s, node, scope, builtin, evaluate, emit, out);
    }
    if (statement) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, name.offset, "constructor result cannot be discarded");
    const bool bitcast = !declared_type && source_span_is(s, name, "bitcast");
    TypeId target = IR_VOID;
    uint32_t inferred = 0u;
    if (declared_type) {
        if (node->value.call.type.offset != NO_ID)
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, name.offset,
                                "named type constructor cannot have type arguments");
        if (!sema_type_reference(s, (TypeRef){name.offset, NO_ID}, scope, &target)) return false;
    }
    else if (bitcast) {
        if (node->right != 1u || node->value.call.type.offset == NO_ID || !sema_type_reference(s, node->value.call.type, scope, &target))
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, name.offset, "bitcast requires a numeric target and one argument");
    }
    else if (node->value.call.type.offset != NO_ID) {
        if (!sema_type_reference(s, node->value.call.type, scope, &target)) return false;
    }
    else if (source_span_is(s, name, "array")) inferred = 2u;
    else if (name.length == 4u && memcmp(s->source+name.offset, "vec", 3u) == 0 && s->source[name.offset+3u] >= '2' && s->source[name.offset+3u] <= '4') inferred = 1u;
    else {
        uint32_t columns, rows;
        if (name.length == 6u && type_matrix_name(s, name, &columns, &rows)) inferred = 3u;
        else if (!sema_type_reference(s, (TypeRef){name.offset, NO_ID}, scope, &target)) return false;
    }
    if (!bitcast && (inferred != 0u || target >= 7u)) return aggregate_call(s, node, scope, target, inferred, evaluate, emit, out);
    if (node->right>1u) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, name.offset, "scalar constructor accepts zero or one argument");
    if (node->right == 0u) {
        *out = (ExpressionValue){
            .known = evaluate, .constant.type = target, .value = {.type = target, .source_offset = name.offset}
        };
        return true;
    }
    ExpressionValue a;
    if (!sema_expression(s, c->arguments[node->left].expression, scope, evaluate, emit, &a)) return false;
    return convert_value(s, &a, target, name.offset, bitcast, evaluate, emit, out);
}
