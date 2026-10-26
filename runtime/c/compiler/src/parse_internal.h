/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_PARSE_INTERNAL_H
#define DOE_COMPILER_PARSE_INTERNAL_H
#include "stage.h"
typedef struct {
    Source *source; CompilerWorkspace *tokens; size_t cursor;
    unsigned block_nesting, active_expression, active_type, template_count;
    uint32_t exempt_comparison, exempt_literal;
} Parser;

const Token *parser_peek(const Parser *);
bool parser_spelled(const Parser *, const Token *, const char *);
bool parser_reject(Parser *, const char *);
bool parser_punctuation(Parser *, unsigned, const char *);
bool parser_word(Parser *, const char *);
bool parser_name_span(Parser *, NameSpan *);
bool expression(Parser *, Syntax *, unsigned, unsigned, uint32_t *);
bool type_reference(Parser *, Syntax *, TypeRef *);
bool parse_interface_attribute(Parser *, Syntax *, uint32_t, uint32_t);
bool parse_graphics_parameters(Parser *, Syntax *);
bool parse_override(Parser *, Syntax *);
bool parser_single_type_call(const Parser *, size_t);
bool parser_primary(Parser *, Syntax *, unsigned, uint32_t *);
bool parser_local_declaration(Parser *, Syntax *, LocalSyntax *, unsigned);
bool parser_condition(Parser *, Syntax *, uint32_t *);
bool parser_assignment(Parser *, Syntax *, Assignment *, unsigned);
bool parser_block(Parser *, Syntax *, unsigned);
#endif
