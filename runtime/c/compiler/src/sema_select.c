/* SPDX-License-Identifier: MIT */
#include "sema.h"

static bool selected_constant(Source *s, ExpressionValue arguments[3], TypeId type,
                              ConstantValue *out) {
    if (arguments[2].constant.type == IR_BOOL) {
        *out = arguments[arguments[2].constant.bits != 0u ? 1u : 0u].constant;
        return true;
    }
    const uint32_t width = type_vector_width(s->compiler, type);
    ConstantValue lanes[4];
    for (uint32_t i = 0u; i < width; ++i) {
        ConstantValue mask;
        if (!constant_project(s, arguments[2].constant, i, &mask) ||
            !constant_project(s, arguments[mask.bits != 0u ? 1u : 0u].constant, i, &lanes[i])) return false;
    }
    return constant_composite(s, type, lanes, width, false, out);
}
static bool vector_mask(Source *s, uint32_t width, uint32_t offset, bool emit,
                         ExpressionValue *mask) {
    CompilerWorkspace *c = s->compiler;
    if (width == 1u || mask->constant.type != IR_BOOL) return true;
    TypeId type;
    if (!type_intern(s, TYPE_VECTOR, IR_BOOL, width, &type)) return false;
    if (!emit) return true;
    if (c->operand_count == c->limits.operands)
        return source_pool_error(s, DOE_COMPILER_POOL_IR_OPERANDS, (size_t)c->operand_count + 1u,
                                 offset, "select mask splat operand capacity exceeded");
    const uint32_t first = c->operand_count++;
    c->typed_operands[first] = mask->value.id;
    return sema_emit(s, true, (IrInstruction){.opcode=IR_COMPOSITE,.type=type,.source_offset=offset,
        .operands.composite={first,1u,2u,0u}}, &mask->value);
}
bool sema_select(Source *s, const Expression *node, ExpressionValue arguments[3],
                 bool evaluate, bool emit, ExpressionValue *out) {
    CompilerWorkspace *c = s->compiler;
    bool runtime = false, known = evaluate, overridable = false;
    for (uint32_t i = 0u; i < 3u; ++i) {
        runtime = runtime || arguments[i].runtime;
        known = known && arguments[i].known;
        overridable = overridable || arguments[i].overridable;
    }
    TypeId type;
    const TypeId left = arguments[0].constant.type, right = arguments[1].constant.type;
    const uint32_t width = type_vector_width(c, left);
    if (type_leaf(c, left) == IR_VOID || type_leaf(c, right) == IR_VOID ||
        type_vector_width(c, right) != width || !sema_common_type(s, left, right, node->source_offset, &type))
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
                            "select values require matching scalar or vector types");
    const TypeId mask = arguments[2].constant.type;
    if (type_leaf(c, mask) != IR_BOOL ||
        (mask != IR_BOOL && (width == 1u || type_vector_width(c, mask) != width)))
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
                            "select mask requires bool or a matching boolean vector");
    if (runtime && type_abstract(c, type) && !sema_default_type(s, type, node->source_offset, &type)) return false;
    if (!sema_materialize(s, &arguments[0], type) || !sema_materialize(s, &arguments[1], type)) return false;
    *out = (ExpressionValue){.runtime=runtime,.known=known && !runtime,.overridable=overridable,
        .constant.type=type,.value={.type=type,.source_offset=node->source_offset}};
    if (!runtime) return !out->known || selected_constant(s, arguments, type, &out->constant);
    for (uint32_t i = 0u; i < 3u; ++i)
        if (!sema_ensure_value(s, &arguments[i], emit)) return false;
    /* Baseline SPIR-V1.3 requires a boolean vector for a vector OpSelect result.
     * This typed splat also makes every text target consume the same saved mask. */
    if (!vector_mask(s, width, node->source_offset, emit, &arguments[2])) return false;
    return sema_emit(s, emit, (IrInstruction){.opcode=IR_SELECT,.type=type,.source_offset=node->source_offset,
        .operands.scalar={arguments[0].value.id,arguments[1].value.id,arguments[2].value.id,0u}}, &out->value);
}
