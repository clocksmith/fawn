/* SPDX-License-Identifier: MIT */
#include "parse_internal.h"
#include "override.h"
#include <string.h>

bool parse_override(Parser *p, Syntax *c) {
    SpecializationContext *context = p->source->specialization;
    if (context == NULL)
        return source_error(p->source, DOE_COMPILER_UNSUPPORTED, parser_peek(p)->offset,
                            "override declarations require explicit ABI13 specialization");
    if (context->count == context->capacity)
        return source_pool_error(p->source, DOE_COMPILER_POOL_OVERRIDES,
            (size_t)context->count + 1u, parser_peek(p)->offset, "override declaration capacity exceeded");
    if (c->constant_decl_count == c->limits.expressions + 1u)
        return source_pool_error(p->source, DOE_COMPILER_POOL_EXPRESSIONS,
            (size_t)c->constant_decl_count + 1u, parser_peek(p)->offset, "module declaration capacity exceeded");
    ConstantSyntax *declaration = &c->constant_decls[c->constant_decl_count];
    *declaration = (ConstantSyntax){.annotation = {NO_ID, NO_ID},
        .first_expression = c->expression_count, .expression = OVERRIDE_NO_INITIALIZER,
        .state = DECLARATION_OVERRIDE};
    OverrideRecord *record = &context->records[context->count];
    const uint32_t required = context->specializing ? record->required : 0u;
    const uint32_t supplied = context->specializing ? record->supplied : NO_ID;
    const TypeId type = context->specializing ? record->type : IR_VOID;
    *record = (OverrideRecord){.declaration = c->constant_decl_count,
        .id_expression = NO_ID, .id = NO_ID, .supplied = supplied, .required = required, .type = type};
    while (parser_peek(p)->kind == '@') {
        if ((record->flags & DOE_COMPILER_OVERRIDE_HAS_ID) != 0u)
            return source_error(p->source, DOE_COMPILER_SEMANTIC_ERROR,
                parser_peek(p)->offset, "duplicate override id attribute");
        ++p->cursor;
        if (!parser_word(p, "id") || !parser_punctuation(p, '(', "(") ||
            !expression(p, c, 0u, 0u, &record->id_expression)) return false;
        if (parser_peek(p)->kind == ',') ++p->cursor;
        if (!parser_punctuation(p, ')', ")")) return false;
        record->flags |= DOE_COMPILER_OVERRIDE_HAS_ID;
    }
    if (!parser_word(p, "override") || !parser_name_span(p, &declaration->name)) return false;
    if (parser_peek(p)->kind == ':') {
        ++p->cursor;
        if (!type_reference(p, c, &declaration->annotation)) return false;
    }
    if (parser_peek(p)->kind == '=') {
        ++p->cursor;
        if (!expression(p, c, 0u, 0u, &declaration->expression)) return false;
        record->flags |= DOE_COMPILER_OVERRIDE_HAS_INITIALIZER;
    } else if (declaration->annotation.offset == NO_ID)
        return parser_reject(p, "override type or initializer");
    if (!parser_punctuation(p, ';', ";")) return false;
    declaration->type = c->expression_count;
    ++c->constant_decl_count;
    ++context->count;
    return true;
}
