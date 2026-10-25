/* SPDX-License-Identifier: MIT */
#include "sema.h"

bool sema_subgroup_call(Source *s, const Expression *node, const LocalScope *scope,
    const NumericBuiltin *builtin, bool evaluate, bool emit, ExpressionValue *out) {
    ExpressionValue arguments[2] = {0};
    TypeId result = IR_BOOL;
    uint32_t link = node->left;
    for (uint32_t i = 0u; i < builtin->arity; ++i) {
        if (!sema_expression(s, s->compiler->arguments[link].expression, scope, evaluate, emit, &arguments[i])) return false;
        if (i == 0u) {
            if (builtin->kind == BUILTIN_SUBGROUP_ALL || builtin->kind == BUILTIN_SUBGROUP_ANY || builtin->kind == BUILTIN_SUBGROUP_BALLOT) {
                if (!sema_materialize(s, &arguments[i], IR_BOOL)) return false;
                if (builtin->kind == BUILTIN_SUBGROUP_BALLOT && !type_intern(s, TYPE_VECTOR, IR_U32, 4u, &result)) return false;
            } else {
                const TypeId original = arguments[i].constant.type;
                if (!sema_default_type(s, original, node->source_offset, &result)) return false;
                const TypeId leaf = type_leaf(s->compiler, result);
                if ((result >= IR_PRIMITIVE_COUNT && s->compiler->types[result - IR_PRIMITIVE_COUNT].kind != TYPE_VECTOR) ||
                    (leaf != IR_F32 && leaf != IR_F16 && leaf != IR_I32 && leaf != IR_U32) ||
                    !sema_materialize(s, &arguments[i], result))
                    return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset, "subgroup exchange requires a numeric scalar or vector");
            }
        } else {
            TypeId type = arguments[i].constant.type;
            if (type == IR_ABSTRACT_INT) type = IR_U32;
            const bool signed_index = builtin->kind == BUILTIN_QUAD_BROADCAST || builtin->kind == BUILTIN_SUBGROUP_BROADCAST || builtin->kind == BUILTIN_SUBGROUP_SHUFFLE;
            if ((type != IR_U32 && !(signed_index && type == IR_I32)) || !sema_materialize(s, &arguments[i], type))
                return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset, "subgroup lane index requires an integer scalar");
            if (!arguments[i].runtime && !arguments[i].overridable && !arguments[i].known &&
                (!sema_expression(s, s->compiler->arguments[link].expression, scope, true, false, &arguments[i]) ||
                 !sema_materialize(s, &arguments[i], type))) return false;
            if ((builtin->kind == BUILTIN_SUBGROUP_BROADCAST || builtin->kind == BUILTIN_QUAD_BROADCAST) && (arguments[i].runtime || arguments[i].overridable || !arguments[i].known))
                return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset, "subgroupBroadcast lane must be a const-expression");
            if (arguments[i].known && arguments[i].constant.bits >= (builtin->kind == BUILTIN_QUAD_BROADCAST ? 4u : 128u))
                return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset, "collective lane or mask is outside its permitted range");
        }
        link = s->compiler->arguments[link].next;
    }
    *out = (ExpressionValue){.runtime=true,.constant.type=result,
        .value={.type=result,.source_offset=node->source_offset}};
    return sema_builtin_finish(s,node,builtin,arguments,emit,out);
}
