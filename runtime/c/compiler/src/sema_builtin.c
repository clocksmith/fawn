/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "sema.h"

bool sema_builtin_value(Source *source, const Expression *node, const LocalScope *scope,
                        const NumericBuiltin *builtin, bool evaluate, bool emit,
                        ExpressionValue *out) {
    CompilerWorkspace *compiler = source->compiler;
    ExpressionValue arguments[3];
    uint32_t link = node->left, width = 0u;
    bool runtime = builtin_subgroup(builtin), known = evaluate;
    TypeId leaf = IR_VOID;
    for (uint32_t i = 0u; i < builtin->arity; ++i) {
        if (!sema_expression(source, compiler->arguments[link].expression, scope,
                             evaluate, emit, &arguments[i])) return false;
        const TypeId type = arguments[i].constant.type;
        if (builtin->family == BUILTIN_MATRIX) {
            if (!sema_matrix_result(source, builtin->kind, type, node->source_offset, &leaf)) return false;
            runtime = arguments[0].runtime;
            known = known && arguments[0].known;
            break;
        }
        const TypeId argument_leaf = type_leaf(compiler, type);
        const uint32_t argument_width = type_vector_width(compiler, type);
        if (argument_leaf == IR_VOID || argument_leaf == IR_BOOL ||
            (i != 0u && argument_width != width))
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
                                "numeric builtin arguments require matching scalar or vector shapes");
        if (i == 0u) {
            leaf = argument_leaf;
            width = argument_width;
        } else if (!sema_common_type(source, leaf, argument_leaf, node->source_offset, &leaf)) return false;
        runtime = runtime || arguments[i].runtime;
        known = known && arguments[i].known;
        link = compiler->arguments[link].next;
    }
    TypeId operand_type = leaf;
    if (builtin->family != BUILTIN_MATRIX) {
        if (builtin_floating(builtin) && leaf == IR_ABSTRACT_INT) leaf = IR_ABSTRACT_FLOAT;
        if (!builtin_shape(builtin, width) ||
            (builtin->family == BUILTIN_SUBGROUP_INTEGER && leaf != IR_I32 && leaf != IR_U32 && leaf != IR_ABSTRACT_INT) ||
            (builtin_floating(builtin) && leaf != IR_F32 && leaf != IR_ABSTRACT_FLOAT))
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
                                "no matching numeric builtin overload");
        if (runtime && !sema_default_type(source, leaf, node->source_offset, &leaf)) return false;
        operand_type = leaf;
        if (width != 1u && !type_intern(source, TYPE_VECTOR, leaf, width, &operand_type)) return false;
        for (uint32_t i = 0u; i < builtin->arity; ++i)
            if (!sema_materialize(source, &arguments[i], operand_type)) return false;

        if (builtin->kind == BUILTIN_CLAMP && !arguments[1].runtime && !arguments[2].runtime) {
            /* A skipped constant bound still has mandatory creation-time meaning.
             * Force only unknown const expressions, never an effectful runtime input. */
            link = compiler->arguments[node->left].next;
            for (uint32_t i = 1u; i < 3u; ++i) {
                if (!arguments[i].known &&
                    (!sema_expression(source, compiler->arguments[link].expression, scope,
                                      true, false, &arguments[i]) ||
                     !sema_materialize(source, &arguments[i], operand_type))) return false;
                link = compiler->arguments[link].next;
            }
            if (!builtin_clamp_bounds(source, node->source_offset,
                                      &arguments[1].constant, &arguments[2].constant)) return false;
        }

    }
    const TypeId result_type = builtin->family == BUILTIN_MATRIX || builtin_scalar_result(builtin) ? leaf : operand_type;
    *out = (ExpressionValue){
        .runtime = runtime, .known = known && !runtime, .constant.type = result_type,
        .value = {.type = result_type, .source_offset = node->source_offset}
    };
    return sema_builtin_finish(source, node, builtin, arguments, emit, out);
}
