/* SPDX-License-Identifier: MIT */
#include "sema.h"

bool sema_float_pack_call(Source *s, const Expression *node, const LocalScope *scope,
    const NumericBuiltin *builtin, bool evaluate, bool emit, ExpressionValue *out) {
    ExpressionValue argument;
    if (!sema_expression(s, s->compiler->arguments[node->left].expression, scope,
        evaluate, emit, &argument)) return false;
    const bool unpack = float_pack_unpack(builtin->kind);
    const bool quantize = builtin->kind == BUILTIN_QUANTIZE_F16;
    const uint32_t width = type_vector_width(s->compiler, argument.constant.type);
    const uint32_t required = unpack ? 1u : quantize ? width : float_pack_width(builtin->kind);
    TypeId input = unpack ? IR_U32 : IR_F32;
    if (width != required || (required != 1u && !type_intern(s, TYPE_VECTOR, input, required, &input)))
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset, "packing builtin has an invalid input shape");
    if (!sema_materialize(s, &argument, input)) return false;
    TypeId result = quantize ? input : IR_U32;
    if (unpack && !type_intern(s, TYPE_VECTOR, IR_F32, float_pack_width(builtin->kind), &result)) return false;
    *out = (ExpressionValue){.runtime=argument.runtime,.known=evaluate && argument.known && !argument.runtime,
        .overridable=argument.overridable,.constant.type=result,
        .value={.type=result,.source_offset=node->source_offset}};
    return sema_builtin_finish(s, node, builtin, &argument, emit, out);
}
