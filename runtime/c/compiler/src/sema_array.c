/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "sema.h"

/* A runtime array is either the whole storage variable or the direct final
 * member of its structure. These are the only address paths admitted here;
 * first-class pointer values remain outside this incremental owner. */
bool sema_array_length(Source *s, uint32_t call, const LocalScope *scope,
                       bool emit, ExpressionValue *out) {
    CompilerWorkspace *c = s->compiler;
    const Expression *node = &c->expressions[call];
    if (c->current_function == NO_ID)
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, node->source_offset,
            "arrayLength is runtime-only, not a constant expression");
    const uint32_t argument = c->arguments[node->left].expression;
    const Expression *address = &c->expressions[argument];
    if (address->kind != EXPRESSION_ADDRESS)
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, address->source_offset,
            "arrayLength requires the address of a storage runtime array");
    const Expression *path = &c->expressions[address->left];
    NameSpan root = {0};
    if (path->kind == EXPRESSION_NAME) root = path->value.name;
    else if (path->kind == EXPRESSION_MEMBER) {
        if (path->left == NO_ID) root = path->value.member.object;
        else if (c->expressions[path->left].kind == EXPRESSION_NAME)
            root = c->expressions[path->left].value.name;
    }
    if (root.length == 0u)
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, path->source_offset,
            "runtime array address must name a storage variable or its final member");
    ValueIdentity identity;
    if (!sema_value_identity(s, scope, root, NULL, &identity)) return false;
    if (identity.kind != IDENTITY_RESOURCE)
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, root.offset,
            "runtime array address does not resolve to a storage resource");
    const ResourceRecord *resource = &c->resources[identity.index];
    if (resource->tail_type == NO_ID || resource->type < 7u)
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, root.offset,
            "arrayLength requires a runtime-sized storage array");
    const TypeRecord *type = &c->types[resource->type - 7u];
    if ((path->kind == EXPRESSION_NAME && type->kind != TYPE_RUNTIME_ARRAY) ||
        (path->kind == EXPRESSION_MEMBER && (type->kind != TYPE_STRUCT ||
            !source_spans_equal(s, path->value.member.member,
                c->members[type->element + type->count - 1u].name))))
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, path->source_offset,
            "arrayLength address does not identify the runtime array tail");
    if (!source_reference_record(s, &c->expression_references[call], SOURCE_RESOURCE,
                                  identity.index, node->source_offset)) return false;
    *out = (ExpressionValue){ .runtime = true, .constant.type = IR_U32,
        .value = { .type = IR_U32, .source_offset = node->source_offset } };
    return sema_emit(s, emit, (IrInstruction){ .opcode = IR_ARRAY_LENGTH,
        .type = IR_U32, .source_offset = node->source_offset,
        .operands.scalar.binding = identity.index }, &out->value);
}
