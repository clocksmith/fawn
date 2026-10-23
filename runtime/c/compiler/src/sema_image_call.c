/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "sema.h"

bool sema_image_call(Source *s, const Expression *node, const LocalScope *scope, bool evaluate, bool emit,
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
        if (!sema_expression(s, c->arguments[link].expression, scope, evaluate, emit, out)) return false;
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
    const uint32_t varying = operation == IMAGE_LOAD && c->types[texture - 7u].state.image.access == DOE_COMPILER_READ_WRITE ? 256u : 0u;
    if (!source_reference_record(s, &c->expression_references[(size_t)(node - c->expressions)], SOURCE_IMAGE,
                                 (uint32_t)operation | varying, node->source_offset)) return false;
    *out = (ExpressionValue){.runtime = true, .constant.type = result,
        .value = {.type = result, .source_offset = node->source_offset}};
    return sema_emit(s, emit, (IrInstruction){.opcode = IR_IMAGE, .type = result,
        .source_offset = node->source_offset, .operands.composite = {first, node->right, (uint32_t)operation, texture}},
        result == IR_VOID ? NULL : &out->value);
}
