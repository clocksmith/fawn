/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "sema.h"

/* Pointer and direct-address operands share the canonical reference owner.
 * Global-root specialization resolves a formal to its exact resource tail
 * before any target consumes IR_ARRAY_LENGTH. */
bool sema_array_length(Source *s, uint32_t call, const LocalScope *scope,
                       bool emit, ExpressionValue *out) {
    CompilerWorkspace *c = s->compiler;
    const Expression *node = &c->expressions[call];
    if (c->current_function == NO_ID)
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
            "arrayLength is runtime-only, not a constant expression");
    const uint32_t argument = c->arguments[node->left].expression;
    ValueReference ref;
    if (!sema_address_reference(s,argument,scope,true,emit,&ref)) return false;
    const uint32_t space = sema_reference_space(c,ref);
    if ((space != POINTER_STORAGE_READ && space != POINTER_STORAGE_WRITE) ||
        ref.type < IR_PRIMITIVE_COUNT || c->types[ref.type-IR_PRIMITIVE_COUNT].kind != TYPE_RUNTIME_ARRAY)
        return source_error(s,DOE_COMPILER_SEMANTIC_ERROR,node->source_offset,
            "arrayLength requires a storage runtime-array pointer");
    if (ref.category != ROOT_RESOURCE && ref.category != ROOT_POINTER)
        return source_error(s,DOE_COMPILER_INPUT_ERROR,node->source_offset,"runtime-array pointer root is invalid");
    if (!source_reference_record(s,&c->expression_references[call],
        ref.category == ROOT_RESOURCE ? SOURCE_RESOURCE : SOURCE_POINTER_READ,ref.root,node->source_offset)) return false;
    *out = (ExpressionValue){.runtime=true,.constant.type=IR_U32,
        .value={.type=IR_U32,.source_offset=node->source_offset}};
    return sema_emit(s,emit,(IrInstruction){.opcode=IR_ARRAY_LENGTH,.type=IR_U32,.source_offset=node->source_offset,
        .operands.scalar={.binding=ref.root,.literal=ref.category}},&out->value);
}
