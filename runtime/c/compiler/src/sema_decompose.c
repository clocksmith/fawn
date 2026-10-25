/* SPDX-License-Identifier: MIT */
#include "sema.h"

static bool shape(Source *s, TypeId leaf, uint32_t width, TypeId *out) {
    *out = leaf;
    return width == 1u || type_intern(s, TYPE_VECTOR, leaf, width, out);
}
static bool exponent_bound(Source *s, const ConstantValue *value, TypeId leaf, uint32_t offset) {
    const uint32_t width = type_vector_width(s->compiler, value->type);
    const uint64_t limit = leaf == IR_F16 ? 16u : leaf == IR_F32 ? 128u : 1024u;
    for (uint32_t i = 0u; i < width; ++i) {
        ConstantValue lane = *value;
        if (width != 1u && !constant_project(s, lane, i, &lane)) return false;
        NumericValue integer;
        if (!numeric_convert(s, offset, (NumericValue){lane.bits,(IrType)lane.type}, IR_ABSTRACT_INT, &integer)) return false;
        if ((integer.bits & (UINT64_C(1) << 63u)) == 0u && integer.bits > limit)
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, offset, "ldexp constant exponent exceeds the floating type bias plus one");
    }
    return true;
}
bool sema_decompose_call(Source *s, const Expression *node, const LocalScope *scope,
    const NumericBuiltin *builtin, bool evaluate, bool emit, ExpressionValue *out) {
    CompilerWorkspace *c = s->compiler;
    ExpressionValue arguments[2];
    uint32_t link = node->left, exponent_expression = NO_ID;
    bool runtime = false, known = evaluate, overridable = false;
    for (uint32_t i = 0u; i < builtin->arity; ++i) {
        if (i == 1u) exponent_expression = c->arguments[link].expression;
        if (!sema_expression(s, c->arguments[link].expression, scope, evaluate, emit, &arguments[i])) return false;
        runtime = runtime || arguments[i].runtime;
        known = known && arguments[i].known;
        overridable = overridable || arguments[i].overridable;
        link = c->arguments[link].next;
    }
    TypeId leaf = type_leaf(c, arguments[0].constant.type);
    if (leaf == IR_ABSTRACT_INT) leaf = IR_ABSTRACT_FLOAT;
    if (leaf != IR_ABSTRACT_FLOAT && leaf != IR_F32 && leaf != IR_F16)
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
            "floating decomposition requires a floating scalar or vector");
    const uint32_t width = type_vector_width(c, arguments[0].constant.type);
    TypeId exponent = IR_ABSTRACT_INT;
    if (builtin->kind == BUILTIN_LDEXP) {
        exponent = type_leaf(c, arguments[1].constant.type);
        if ((exponent != IR_I32 && exponent != IR_ABSTRACT_INT) ||
            type_vector_width(c, arguments[1].constant.type) != width)
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
                "ldexp requires a matching signed integer exponent shape");
    }
    if (runtime || exponent == IR_I32) {
        if (!sema_default_type(s, leaf, node->source_offset, &leaf)) return false;
    }
    TypeId operand_type;
    if (!shape(s, leaf, width, &operand_type) || !sema_materialize(s, &arguments[0], operand_type)) return false;
    TypeId result_type;
    if (builtin->kind == BUILTIN_LDEXP) {
        TypeId exponent_type;
        if (!shape(s, leaf == IR_ABSTRACT_FLOAT ? IR_ABSTRACT_INT : IR_I32, width, &exponent_type) ||
            !sema_materialize(s, &arguments[1], exponent_type)) return false;
        /* A constant exponent has a mandatory range even when the value input
         * is runtime or this surrounding expression is not evaluated yet. */
        if (!arguments[1].known && !arguments[1].runtime && !arguments[1].overridable &&
            (!sema_expression(s, exponent_expression, scope, true, false, &arguments[1]) ||
             !sema_materialize(s, &arguments[1], exponent_type))) return false;
        if (arguments[1].known && !exponent_bound(s, &arguments[1].constant, leaf, node->source_offset)) return false;
        result_type = operand_type;
    } else if (!type_intern(s, builtin->kind == BUILTIN_FREXP ? TYPE_FREXP_RESULT : TYPE_MODF_RESULT,
        operand_type, 2u, &result_type)) return false;
    *out = (ExpressionValue){.runtime=runtime,.known=known && !runtime,.overridable=overridable,
        .constant.type=result_type,.value={.type=result_type,.source_offset=node->source_offset}};
    return sema_builtin_finish(s, node, builtin, arguments, emit, out);
}
