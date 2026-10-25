/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "sema.h"

bool sema_unary_value(Source *s, const Expression *e, ExpressionValue *a, bool evaluate, bool emit, ExpressionValue *out) {
    CompilerWorkspace *c = s->compiler;
    const TypeId leaf = type_leaf(c, a->constant.type);
    if (leaf == IR_VOID || (e->kind == EXPRESSION_NOT && leaf != IR_BOOL) || (e->kind == EXPRESSION_COMPLEMENT && !(leaf == IR_U32 || leaf == IR_I32 || leaf == IR_ABSTRACT_INT)) || (e->kind == EXPRESSION_NEGATE && (leaf == IR_BOOL || leaf == IR_U32)))
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, e->source_offset, "unary operator is not defined for this type");
    *out = (ExpressionValue){
        .runtime = a->runtime, .overridable = a->overridable, .known = evaluate && a->known, .constant.type = a->constant.type, .value = {.type = a->constant.type, .source_offset = e->source_offset}
    };
    if (!a->runtime) {
        if (!out->known) return true;
        const uint32_t n = type_vector_width(c, a->constant.type);
        ConstantValue values[4];
        for (uint32_t i = 0u;i<n;++i) {
            NumericValue x, r;
            if (!sema_constant_lane(s, a->constant, i, &x) || !numeric_unary(s, e->source_offset, e->kind, x, &r)) return false;
            values[i] = (ConstantValue){.type = (TypeId)r.type, .bits = r.bits};
        }
        return sema_finish_lanes(s, a->constant.type, values, n, &out->constant);
    }
    if (!sema_ensure_value(s, a, emit)) return false;
    return sema_emit(s, emit, (IrInstruction){.opcode = e->kind == EXPRESSION_NOT?IR_NOT:e->kind == EXPRESSION_NEGATE?IR_NEGATE:IR_COMPLEMENT, .type = a->constant.type, .source_offset = e->source_offset, .operands.scalar.left = a->value.id}, &out->value);
}

bool sema_builtin_finish(Source *source, const Expression *node, const NumericBuiltin *builtin,
                          ExpressionValue *arguments, bool emit, ExpressionValue *out) {
    CompilerWorkspace *compiler = source->compiler;
    if (!out->runtime) {
        if (!out->known) return true;
        const ConstantValue values[3] = {
            arguments[0].constant,
            builtin->arity > 1u ? arguments[1].constant : (ConstantValue){0},
            builtin->arity > 2u ? arguments[2].constant : (ConstantValue){0}
        };
        return builtin_constant(source, node->source_offset, builtin, values, out->constant.type, &out->constant);
    }

    if (builtin->family == BUILTIN_PACKED_INTEGER)
        return sema_packed_integer(source, node->source_offset, builtin, arguments, emit, out);
    uint32_t first = 0u;
    if (emit) {
        if (builtin->arity > compiler->limits.operands - compiler->operand_count)
            return source_pool_error(source, DOE_COMPILER_POOL_IR_OPERANDS,
                (size_t)compiler->operand_count + builtin->arity, node->source_offset,
                "typed operand capacity exceeded");
        first = compiler->operand_count;
        compiler->operand_count += builtin->arity;
    }
    for (uint32_t i = 0u; i < builtin->arity; ++i) {
        if (!sema_ensure_value(source, &arguments[i], emit)) return false;
        if (emit) compiler->typed_operands[first + i] = arguments[i].value.id;
    }
    /* FMix and both text targets consume matching vector operands. Reuse the
     * third saved operand slot for a scalar factor splat; never replay source. */
    if (emit && builtin->kind == BUILTIN_MIX && out->constant.type >= 7u &&
        arguments[2].constant.type < 7u &&
        !sema_emit(source, true, (IrInstruction){.opcode = IR_COMPOSITE,
            .type = out->constant.type, .source_offset = node->source_offset,
            .operands.composite = {first + 2u, 1u, 2u, 0u}}, &arguments[2].value)) return false;
    return sema_emit(source, emit, (IrInstruction){
        .opcode = IR_BUILTIN, .type = out->constant.type, .source_offset = node->source_offset,
        .operands.scalar = {arguments[0].value.id,
            builtin->arity > 1u ? arguments[1].value.id : 0u,
            builtin->arity > 2u ? arguments[2].value.id : 0u, (uint32_t)builtin->kind}
    }, &out->value);
}

bool sema_swizzle_value(Source *s, const Expression *node, ExpressionValue *base, TypeId output,
            uint32_t mask, bool evaluate, bool emit, ExpressionValue *out) {
    *out = (ExpressionValue){
        .runtime = base->runtime, .overridable = base->overridable, .known = evaluate && base->known, .constant.type = output, .value = {.type = output, .source_offset = node->source_offset}
    };
    if (!base->runtime) {
        if (!out->known) return true;
        ConstantValue lanes[4];
        for (uint32_t i = 0u;i<node->value.member.member.length;++i) if (!constant_project(s, base->constant, (mask>>(2u*i))&3u, &lanes[i])) return false;
        return constant_composite(s, output, lanes, node->value.member.member.length, false, &out->constant);
    }
    if (!sema_ensure_value(s, base, emit)) return false;
    return sema_emit(s, emit, (IrInstruction){.opcode = IR_SWIZZLE, .type = output, .source_offset = node->source_offset, .operands.scalar = {base->value.id, node->value.member.member.length, base->constant.type, mask}}, &out->value);
}
