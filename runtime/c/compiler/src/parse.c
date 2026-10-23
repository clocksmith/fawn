/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "internal.h"
#include <string.h>

typedef struct { Source *source; const TokenList *tokens; size_t cursor; unsigned block_nesting; } Parser;
static const Token *peek(const Parser *p) { return &p->tokens->tokens[p->cursor]; }
static bool spelled(const Parser *p, const Token *t, const char *text) {
    return strlen(text) == t->length && memcmp(p->source->source + t->offset, text, t->length) == 0;
}
static bool reject(Parser *p, const char *expectation) {
    char message[192];
    const char *const parts[] = { "expected ", expectation, "; unsupported or malformed WGSL subset" };
    size_t used = 0;
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i) {
        size_t length = strlen(parts[i]);
        if (length > sizeof(message) - 1u - used) length = sizeof(message) - 1u - used;
        memcpy(message + used, parts[i], length);
        used += length;
    }
    message[used] = '\0';
    return source_error(p->source, DOE_COMPILER_PARSE_ERROR, peek(p)->offset, message);
}
static bool punctuation(Parser *p, unsigned kind, const char *expectation) {
    if (peek(p)->kind != kind) return reject(p, expectation);
    ++p->cursor;
    return true;
}
static bool word(Parser *p, const char *text) {
    if (peek(p)->kind != TOKEN_WORD || !spelled(p, peek(p), text)) return reject(p, text);
    ++p->cursor;
    return true;
}
static bool name(Parser *p, Name *out) {
    const Token *t = peek(p);
    if (t->kind != TOKEN_WORD) return reject(p, "ASCII WGSL identifier");
    if (t->length > DOE_COMPILER_MAX_NAME_BYTES)
        return source_error(p->source, DOE_COMPILER_INPUT_ERROR, t->offset,
                            "identifier exceeds 63 bytes");
    memcpy(out->text, p->source->source + t->offset, t->length);
    out->text[t->length] = '\0';
    out->offset = t->offset;
    ++p->cursor;
    return true;
}
static bool name_span(Parser *p, NameSpan *out) {
    const Token *t = peek(p);
    if (t->kind != TOKEN_WORD) return reject(p, "ASCII WGSL identifier");
    if (t->length > DOE_COMPILER_MAX_NAME_BYTES)
        return source_error(p->source, DOE_COMPILER_INPUT_ERROR, t->offset,
                            "identifier exceeds 63 bytes");
    if (t->offset > p->source->length || t->length > p->source->length - t->offset ||
        t->offset > UINT32_MAX)
        return source_error(p->source, DOE_COMPILER_INPUT_ERROR, t->offset,
                            "invalid source name span");
    *out = (NameSpan){ .offset = (uint32_t)t->offset, .length = (uint32_t)t->length };
    ++p->cursor;
    return true;
}
static bool integer(Parser *p, bool require_u, Integer *out) {
    const Token *t = peek(p);
    if (t->kind != TOKEN_NUMBER) return reject(p, require_u ? "decimal u32 literal with u suffix" : "decimal integer");
    const char *s = p->source->source + t->offset;
    const size_t digit_count = require_u && t->length > 0u ? t->length - 1u : t->length;
    if (digit_count == 0u || (require_u && s[t->length - 1u] != 'u'))
        return reject(p, "decimal u32 literal with required u suffix");
    if (digit_count > 1u && s[0] == '0')
        return source_error(p->source, DOE_COMPILER_PARSE_ERROR, t->offset,
                            "decimal WGSL integers may not have leading zeroes");
    uint32_t value = 0u;
    for (size_t i = 0; i < digit_count; ++i) {
        if (s[i] < '0' || s[i] > '9') return reject(p, "decimal digits only");
        const uint32_t digit = (uint32_t)(s[i] - '0');
        if (value > (UINT32_MAX - digit) / 10u)
            return source_error(p->source, DOE_COMPILER_SEMANTIC_ERROR, t->offset,
                                "integer literal exceeds u32 range");
        value = value * 10u + digit;
    }
    *out = (Integer){ .value = value, .offset = t->offset };
    ++p->cursor;
    return true;
}
static bool parenthesized_integer(Parser *p, Integer *out) {
    return punctuation(p, '(', "(") && integer(p, false, out) && punctuation(p, ')', ")");
}
static bool resource_attributes(Parser *p, ResourceSyntax *s) {
    unsigned seen = 0u;
    while (peek(p)->kind == '@') {
        ++p->cursor;
        const Token *t = peek(p);
        unsigned flag;
        Integer *value;
        if (spelled(p, t, "group")) { flag = 1u; value = &s->group; }
        else if (spelled(p, t, "binding")) { flag = 2u; value = &s->binding; }
        else return source_error(p->source, DOE_COMPILER_UNSUPPORTED, t->offset,
                                 "storage requires @group and @binding attributes");
        if ((seen & flag) != 0u)
            return source_error(p->source, DOE_COMPILER_SEMANTIC_ERROR, t->offset, "duplicate resource attribute");
        seen |= flag;
        ++p->cursor;
        if (!parenthesized_integer(p, value)) return false;
    }
    return seen == 3u || reject(p, "one @group(G) and one @binding(B)");
}
static bool workgroup_dimensions(Parser *p, Syntax *s) {
    if (!punctuation(p, '(', "(")) return false;
    for (size_t i = 0; i < 3u; ++i) s->local_size[i].value = 1u;
    size_t count = 0u;
    do {
        if (count == 3u) return reject(p, "at most three workgroup dimensions");
        if (!integer(p, false, &s->local_size[count++])) return false;
        if (peek(p)->kind != ',') break;
        ++p->cursor;
    } while (peek(p)->kind != ')');
    return punctuation(p, ')', ")");
}
static bool entry_attributes(Parser *p, Syntax *s) {
    unsigned seen = 0u;
    while (peek(p)->kind == '@') {
        ++p->cursor;
        const Token *t = peek(p);
        if (t->kind != TOKEN_WORD) return reject(p, "entry-point attribute name");
        unsigned flag;
        if (spelled(p, t, "compute")) flag = 1u;
        else if (spelled(p, t, "workgroup_size")) flag = 2u;
        else return source_error(p->source, DOE_COMPILER_UNSUPPORTED, t->offset,
                                 "only @compute and @workgroup_size are supported on the entry point");
        if ((seen & flag) != 0u)
            return source_error(p->source, DOE_COMPILER_SEMANTIC_ERROR, t->offset, "duplicate entry-point attribute");
        seen |= flag;
        ++p->cursor;
        if (flag == 2u && !workgroup_dimensions(p, s)) return false;
    }
    return seen == 3u || reject(p, "one @compute and one @workgroup_size(N)");
}
static bool scalar_annotation(Parser *p, Name *annotation, DoeCompilerScalar *scalar) {
    if (!name(p, annotation)) return false;
    if (strcmp(annotation->text, "u32") == 0) *scalar = DOE_COMPILER_U32;
    else if (strcmp(annotation->text, "f32") == 0) *scalar = DOE_COMPILER_F32;
    else return source_error(p->source, DOE_COMPILER_UNSUPPORTED, annotation->offset,
                             "flat scalar type must be u32 or f32");
    return true;
}
static bool struct_declaration(Parser *p, StructSyntax *s) {
    if (!word(p, "struct") || !name(p, &s->name) || !punctuation(p, '{', "{")) return false;
    do {
        if (s->member_count == DOE_COMPILER_MAX_STRUCT_MEMBERS)
            return source_error(p->source, DOE_COMPILER_INPUT_ERROR, peek(p)->offset,
                                "flat struct exceeds eight members");
        MemberDeclaration *member = &s->members[s->member_count++];
        if (!name(p, &member->name) || !punctuation(p, ':', ":") ||
            !scalar_annotation(p, &member->annotation, &member->scalar)) return false;
        if (peek(p)->kind != ',') break;
        ++p->cursor;
    } while (peek(p)->kind != '}');
    return punctuation(p, '}', "}");
}
static bool resource_declaration(Parser *p, ResourceSyntax *s, bool allow_uniforms) {
    if (!resource_attributes(p, s) || !word(p, "var") ||
        !punctuation(p, '<', "<storage, access> or <uniform>")) return false;
    if (spelled(p, peek(p), "uniform")) {
        if (!allow_uniforms)
            return source_error(p->source, DOE_COMPILER_UNSUPPORTED, peek(p)->offset,
                                "uniform declarations require compiler API v2");
        ++p->cursor;
        s->kind = DOE_COMPILER_UNIFORM_BUFFER;
        s->access = DOE_COMPILER_READ;
        return punctuation(p, '>', ">") && name(p, &s->name) &&
               punctuation(p, ':', ":") && name(p, &s->type_name) && punctuation(p, ';', ";");
    }
    s->kind = DOE_COMPILER_STORAGE_BUFFER;
    if (!word(p, "storage") || !punctuation(p, ',', ",")) return false;
    if (spelled(p, peek(p), "read")) s->access = DOE_COMPILER_READ;
    else if (spelled(p, peek(p), "read_write")) s->access = DOE_COMPILER_READ_WRITE;
    else return reject(p, "read or read_write storage access");
    ++p->cursor;
    if (!punctuation(p, '>', ">") || !name(p, &s->name) || !punctuation(p, ':', ":") ||
        !word(p, "array") || !punctuation(p, '<', "<u32 or f32>") ||
        !scalar_annotation(p, &s->type_name, &s->scalar)) return false;
    return punctuation(p, '>', ">") && punctuation(p, ';', ";");
}
static bool expression_node(Parser *p, Syntax *s, Expression node, bool charged, uint32_t *id) {
    if (node.depth > p->source->limits->expression_depth)
        return source_error(p->source, DOE_COMPILER_INPUT_ERROR, node.source_offset, "expression depth capacity exceeded");
    if (s->expression_count == s->expression_capacity ||
        (charged && s->expression_budget_count == p->source->limits->expressions))
        return source_error(p->source, DOE_COMPILER_INPUT_ERROR, node.source_offset, "expression node capacity exceeded");
    *id = (uint32_t)s->expression_count;
    s->expressions[s->expression_count++] = node;
    if (charged) ++s->expression_budget_count;
    return true;
}
static bool expression(Parser *p, Syntax *s, unsigned minimum, unsigned nesting, uint32_t *id);
static bool primary(Parser *p, Syntax *s, unsigned nesting, uint32_t *id) {
    if (nesting >= p->source->limits->expression_depth)
        return source_error(p->source, DOE_COMPILER_INPUT_ERROR, peek(p)->offset, "expression depth capacity exceeded");
    Expression node = { .source_offset = peek(p)->offset, .depth = 1u };
    if (peek(p)->kind == '(') {
        ++p->cursor;
        if (!expression(p, s, 0u, nesting + 1u, id) || !punctuation(p, ')', ")")) return false;
        if (s->expressions[*id].depth >= p->source->limits->expression_depth)
            return source_error(p->source, DOE_COMPILER_INPUT_ERROR, node.source_offset, "expression depth capacity exceeded");
        ++s->expressions[*id].depth;
        s->expressions[*id].grouped = true;
        return true;
    }
    if (peek(p)->kind == '!') {
        ++p->cursor;
        node.kind = EXPRESSION_NOT;
        if (!primary(p, s, nesting + 1u, &node.left)) return false;
        node.depth = 1u + s->expressions[node.left].depth;
    } else if (peek(p)->kind == TOKEN_NUMBER) {
        const Token *token = peek(p);
        const bool suffixed = token->length != 0u && p->source->source[token->offset + token->length - 1u] == 'u';
        Integer value;
        if (!integer(p, suffixed || !p->source->limits->extended, &value)) return false;
        node.kind = suffixed ? EXPRESSION_INTEGER : EXPRESSION_ABSTRACT_INTEGER;
        node.value.integer = value.value;
    } else if (peek(p)->kind == TOKEN_DECIMAL) {
        Decimal value;
        if (!parse_decimal(p->source, peek(p), &value)) return false;
        ++p->cursor;
        node.kind = EXPRESSION_DECIMAL;
        node.value.decimal.numerator = value.numerator;
        node.value.decimal.denominator = value.denominator;
    } else if (p->source->limits->extended &&
               (spelled(p, peek(p), "true") || spelled(p, peek(p), "false"))) {
        node.kind = EXPRESSION_BOOLEAN;
        node.value.integer = spelled(p, peek(p), "true") ? 1u : 0u;
        ++p->cursor;
    } else {
        const Token *identifier_token = peek(p);
        NameSpan identifier;
        if (!name_span(p, &identifier)) return false;
        if (peek(p)->kind == '[') {
            node.kind = EXPRESSION_LOAD;
            node.value.reference.storage = identifier;
            ++p->cursor;
            if (!expression(p, s, 0u, nesting + 1u, &node.value.reference.index) || !punctuation(p, ']', "]")) return false;
            node.depth = 1u + s->expressions[node.value.reference.index].depth;
        } else if (peek(p)->kind == '.') {
            ++p->cursor;
            node.kind = EXPRESSION_MEMBER;
            node.value.member.object = identifier;
            if (!name_span(p, &node.value.member.member)) return false;
        } else if (peek(p)->kind == '(') {
            if (!spelled(p, identifier_token, "max"))
                return source_error(p->source, DOE_COMPILER_UNSUPPORTED, identifier.offset, "only scalar max calls are supported");
            ++p->cursor;
            node.kind = EXPRESSION_MAX;
            node.value.name = identifier;
            if (!expression(p, s, 0u, nesting + 1u, &node.left) || !punctuation(p, ',', ",") ||
                !expression(p, s, 0u, nesting + 1u, &node.right) || !punctuation(p, ')', ")")) return false;
            node.depth = 1u + (s->expressions[node.left].depth > s->expressions[node.right].depth ?
                              s->expressions[node.left].depth : s->expressions[node.right].depth);
        } else { node.kind = EXPRESSION_NAME; node.value.name = identifier; }
    }
    return expression_node(p, s, node, true, id);
}
static bool comparison_kind(unsigned kind, Comparison *comparison) {
    switch (kind) {
        case TOKEN_EQUAL: *comparison = COMPARE_EQUAL; return true;
        case TOKEN_NOT_EQUAL: *comparison = COMPARE_NOT_EQUAL; return true;
        case '<': *comparison = COMPARE_LESS; return true;
        case TOKEN_LESS_EQUAL: *comparison = COMPARE_LESS_EQUAL; return true;
        case '>': *comparison = COMPARE_GREATER; return true;
        case TOKEN_GREATER_EQUAL: *comparison = COMPARE_GREATER_EQUAL; return true;
        default: return false;
    }
}
static unsigned binary_precedence(const Parser *p, unsigned token, ExpressionKind *kind, Comparison *comparison) {
    if (token == '*') { *kind = EXPRESSION_MULTIPLY; return 4u; }
    if (token == '+' || token == '-') { *kind = token == '+' ? EXPRESSION_ADD : EXPRESSION_SUBTRACT; return 3u; }
    if (!p->source->limits->extended) return 0u;
    if (comparison_kind(token, comparison)) { *kind = EXPRESSION_COMPARE; return 2u; }
    if (token == TOKEN_AND || token == TOKEN_OR) { *kind = token == TOKEN_AND ? EXPRESSION_AND : EXPRESSION_OR; return 1u; }
    return 0u;
}
static bool expression(Parser *p, Syntax *s, unsigned minimum, unsigned nesting, uint32_t *id) {
    if (!primary(p, s, nesting, id)) return false;
    for (;;) {
        ExpressionKind kind = EXPRESSION_ADD;
        Comparison comparison = COMPARE_EQUAL;
        const unsigned precedence = binary_precedence(p, peek(p)->kind, &kind, &comparison);
        if (precedence == 0u || precedence < minimum) return true;
        const Expression left = s->expressions[*id];
        if (!left.grouped && ((kind == EXPRESSION_COMPARE && left.kind == EXPRESSION_COMPARE) ||
            ((kind == EXPRESSION_AND || kind == EXPRESSION_OR) &&
             (left.kind == EXPRESSION_AND || left.kind == EXPRESSION_OR) && kind != left.kind)))
            return reject(p, "parentheses around mixed logical or chained comparisons");
        Expression node = { .source_offset = peek(p)->offset, .kind = kind, .left = *id };
        node.value.comparison = comparison;
        ++p->cursor;
        if (!expression(p, s, precedence + 1u, nesting + 1u, &node.right)) return false;
        const Expression *right = &s->expressions[node.right];
        node.depth = 1u + (left.depth > right->depth ? left.depth : right->depth);
        if (!expression_node(p, s, node, true, id)) return false;
    }
}
static bool local_declaration(Parser *p, Syntax *s, LocalSyntax *local, unsigned terminator) {
    local->mutable = spelled(p, peek(p), "var");
    ++p->cursor;
    if (local->mutable && peek(p)->kind == '<') {
        ++p->cursor;
        if (!word(p, "function") || !punctuation(p, '>', ">")) return false;
    }
    if (!name_span(p, &local->name)) return false;
    if (peek(p)->kind == ':') {
        ++p->cursor;
        local->annotated = true;
        const Token *type = peek(p);
        if (!name_span(p, &local->annotation)) return false;
        if (spelled(p, type, "u32")) local->type = IR_U32;
        else if (spelled(p, type, "f32")) local->type = IR_F32;
        else if (p->source->limits->extended && spelled(p, type, "bool")) local->type = IR_BOOL;
        else return source_error(p->source, DOE_COMPILER_UNSUPPORTED, type->offset, "unsupported local scalar annotation");
    }
    if (peek(p)->kind == '=') {
        ++p->cursor;
        local->initialized = true;
        if (!expression(p, s, 0u, 0u, &local->expression)) return false;
    } else if (!local->mutable || !local->annotated) return reject(p, "local initializer or explicit var type");
    return terminator == 0u || punctuation(p, terminator, ";");
}
static bool condition(Parser *p, Syntax *s, uint32_t *id) {
    if (p->source->limits->extended) return expression(p, s, 0u, 0u, id);
    Expression node = { .source_offset = peek(p)->offset, .kind = EXPRESSION_COMPARE };
    if (!punctuation(p, '(', "(") || !expression(p, s, 0u, 0u, &node.left)) return false;
    if (!comparison_kind(peek(p)->kind, &node.value.comparison)) return reject(p, "unsigned comparison operator");
    ++p->cursor;
    if (!expression(p, s, 0u, 0u, &node.right) || !punctuation(p, ')', ")")) return false;
    node.depth = s->expressions[node.left].depth > s->expressions[node.right].depth ?
                 s->expressions[node.left].depth : s->expressions[node.right].depth;
    return expression_node(p, s, node, false, id);
}
static bool assignment(Parser *p, Syntax *s, Assignment *out, unsigned terminator) {
    if (!name_span(p, &out->target.storage)) return false;
    out->local = peek(p)->kind != '[';
    if (out->local && !p->source->limits->extended) return reject(p, "[");
    if (!out->local) {
        ++p->cursor;
        if (!expression(p, s, 0u, 0u, &out->target.index) || !punctuation(p, ']', "]")) return false;
    }
    switch (peek(p)->kind) {
        case '=': out->operation = UPDATE_SET; break;
        case TOKEN_ADD_ASSIGN: out->operation = UPDATE_ADD; break;
        case TOKEN_SUB_ASSIGN: out->operation = UPDATE_SUBTRACT; break;
        case TOKEN_MUL_ASSIGN: out->operation = UPDATE_MULTIPLY; break;
        case TOKEN_INCREMENT: out->operation = UPDATE_INCREMENT; break;
        case TOKEN_DECREMENT: out->operation = UPDATE_DECREMENT; break;
        default: return reject(p, "assignment or update operator");
    }
    ++p->cursor;
    if (out->operation != UPDATE_INCREMENT && out->operation != UPDATE_DECREMENT &&
        !expression(p, s, 0u, 0u, &out->expression)) return false;
    return terminator == 0u || punctuation(p, terminator, ";");
}
static bool statement_slot(Parser *p, Syntax *s, uint32_t *id) {
    if (s->statement_count == s->statement_capacity)
        return source_error(p->source, DOE_COMPILER_INPUT_ERROR, peek(p)->offset, "statement capacity exceeded");
    *id = (uint32_t)s->statement_count++;
    Statement *statement = &s->statements[*id];
    memset(statement, 0, sizeof(*statement));
    statement->source_offset = peek(p)->offset;
    statement->initializer = UINT32_MAX;
    statement->update = UINT32_MAX;
    return true;
}
static bool simple_statement(Parser *p, Syntax *s, bool declaration, unsigned terminator, uint32_t *id) {
    if (!statement_slot(p, s, id)) return false;
    Statement *st = &s->statements[*id];
    if (declaration && (spelled(p, peek(p), "let") || spelled(p, peek(p), "var"))) {
        st->kind = spelled(p, peek(p), "var") ? STATEMENT_VAR : STATEMENT_LET;
        if (!local_declaration(p, s, &st->value.local, terminator)) return false;
    } else {
        st->kind = STATEMENT_ASSIGN;
        if (!assignment(p, s, &st->value.assignment, terminator)) return false;
    }
    st->end = (uint32_t)s->statement_count;
    return true;
}
static bool block(Parser *p, Syntax *s, unsigned depth);
static bool statement(Parser *p, Syntax *s, unsigned depth) {
    const bool extended = p->source->limits->extended;
    const Token *token = peek(p);
    const bool is_if = spelled(p, token, "if");
    const bool is_for = extended && spelled(p, token, "for");
    const bool is_while = extended && spelled(p, token, "while");
    const bool is_loop = extended && spelled(p, token, "loop");
    const bool is_block = extended && token->kind == '{';
    const bool is_continuing = extended && spelled(p, token, "continuing");
    if (is_if || is_for || is_while || is_loop || is_block) {
        if (depth >= p->source->limits->control_depth)
            return source_error(p->source, DOE_COMPILER_INPUT_ERROR, token->offset, "control depth capacity exceeded");
    }
    uint32_t id;
    if (spelled(p, token, "let") || (extended && spelled(p, token, "var")))
        return simple_statement(p, s, true, ';', &id);
    if (!is_if && !is_for && !is_while && !is_loop && !is_block && !is_continuing &&
        !spelled(p, token, "return") && !(extended &&
        (spelled(p, token, "break") || spelled(p, token, "continue") || token->kind == ';')))
        return simple_statement(p, s, false, ';', &id);
    if (!statement_slot(p, s, &id)) return false;
    Statement *st = &s->statements[id];
    ++p->cursor;
    if (is_if) {
        st->kind = STATEMENT_IF;
        if (!condition(p, s, &st->value.condition) || !punctuation(p, '{', "{")) return false;
        st->body_start = (uint32_t)s->statement_count;
        if (!block(p, s, depth + 1u)) return false;
        st->else_start = (uint32_t)s->statement_count;
        if (spelled(p, peek(p), "else")) {
            ++p->cursor;
            if (extended && spelled(p, peek(p), "if")) {
                if (!statement(p, s, depth + 1u)) return false;
            } else if (!punctuation(p, '{', "{") || !block(p, s, depth + 1u)) return false;
        }
    } else if (is_for) {
        st->kind = STATEMENT_FOR;
        st->value.condition = UINT32_MAX;
        if (!punctuation(p, '(', "(")) return false;
        if (peek(p)->kind == ';') ++p->cursor;
        else if (!simple_statement(p, s, true, ';', &st->initializer)) return false;
        if (peek(p)->kind != ';' && !expression(p, s, 0u, 0u, &st->value.condition)) return false;
        if (!punctuation(p, ';', ";")) return false;
        if (peek(p)->kind != ')' && !simple_statement(p, s, false, 0u, &st->update)) return false;
        if (!punctuation(p, ')', ")") || !punctuation(p, '{', "{")) return false;
        st->body_start = (uint32_t)s->statement_count;
        if (!block(p, s, depth + 1u)) return false;
    } else if (is_while || is_loop || is_block || is_continuing) {
        st->kind = is_while ? STATEMENT_WHILE : is_loop ? STATEMENT_LOOP :
                   is_block ? STATEMENT_BLOCK : STATEMENT_CONTINUING;
        st->value.condition = UINT32_MAX;
        if (is_while && !expression(p, s, 0u, 0u, &st->value.condition)) return false;
        if (!is_block && !punctuation(p, '{', "{")) return false;
        st->body_start = (uint32_t)s->statement_count;
        if (!block(p, s, depth + (is_continuing ? 0u : 1u))) return false;
    } else if (spelled(p, token, "return")) {
        st->kind = STATEMENT_RETURN;
        if (!punctuation(p, ';', ";")) return false;
    } else if (spelled(p, token, "break")) {
        st->kind = STATEMENT_BREAK;
        if (spelled(p, peek(p), "if")) {
            ++p->cursor;
            st->kind = STATEMENT_BREAK_IF;
            if (!expression(p, s, 0u, 0u, &st->value.condition)) return false;
        }
        if (!punctuation(p, ';', ";")) return false;
    } else if (spelled(p, token, "continue")) {
        st->kind = STATEMENT_CONTINUE;
        if (!punctuation(p, ';', ";")) return false;
    } else st->kind = STATEMENT_EMPTY;
    st->end = (uint32_t)s->statement_count;
    return true;
}
static bool block(Parser *p, Syntax *s, unsigned depth) {
    /* Required loop/continuing braces may share one language depth, but even
     * malformed nested continuing blocks must have a bounded C call path. */
    if (p->block_nesting >= 2u * p->source->limits->control_depth + 1u)
        return source_error(p->source, DOE_COMPILER_INPUT_ERROR, peek(p)->offset,
                            "control recursion capacity exceeded");
    ++p->block_nesting;
    while (peek(p)->kind != '}') {
        if (peek(p)->kind == TOKEN_END) return reject(p, "statement or }");
        if (!statement(p, s, depth)) return false;
    }
    --p->block_nesting;
    return punctuation(p, '}', "}");
}
static bool starts_storage(const Parser *p) {
    if (peek(p)->kind != '@' || p->cursor + 1u >= p->tokens->count) return false;
    const Token *attribute = &p->tokens->tokens[p->cursor + 1u];
    return spelled(p, attribute, "group") || spelled(p, attribute, "binding");
}

/* Recognize only the retained unannotated leading alias grammar. Other lets
 * belong to the general expression/statement budgets, without speculative parse. */
static bool starts_legacy_alias(const Parser *p, const Syntax *s) {
    if (p->tokens->count - p->cursor < 7u) return false;
    const Token *t = p->tokens->tokens + p->cursor;
    bool global = false;
    for (size_t i = 0; i < s->parameter_count; ++i)
        if (s->parameters[i].kind == INPUT_GLOBAL_INVOCATION &&
            spelled(p, &t[3], s->parameters[i].name.text)) global = true;
    return global && t[0].kind == TOKEN_WORD && spelled(p, &t[0], "let") &&
           t[1].kind == TOKEN_WORD && t[2].kind == '=' && t[3].kind == TOKEN_WORD &&
           t[4].kind == '.' && t[5].kind == TOKEN_WORD && spelled(p, &t[5], "x") &&
           t[6].kind == ';';
}

static bool entry_parameter(Parser *p, EntryParameter *parameter) {
    if (!punctuation(p, '@', "@builtin") || !word(p, "builtin") ||
        !punctuation(p, '(', "(")) return false;
    const Token *builtin = peek(p);
    if (spelled(p, builtin, "global_invocation_id")) parameter->kind = INPUT_GLOBAL_INVOCATION;
    else if (spelled(p, builtin, "local_invocation_id")) parameter->kind = INPUT_LOCAL_INVOCATION;
    else if (spelled(p, builtin, "workgroup_id")) parameter->kind = INPUT_WORKGROUP;
    else if (spelled(p, builtin, "num_workgroups")) parameter->kind = INPUT_NUM_WORKGROUPS;
    else if (spelled(p, builtin, "local_invocation_index")) parameter->kind = INPUT_LOCAL_INDEX;
    else return source_error(p->source, DOE_COMPILER_UNSUPPORTED, builtin->offset,
                             "unsupported compute input builtin");
    ++p->cursor;
    if (!punctuation(p, ')', ")") || !name(p, &parameter->name) ||
        !punctuation(p, ':', ":")) return false;
    if (parameter->kind == INPUT_LOCAL_INDEX) return word(p, "u32");
    if (spelled(p, peek(p), "vec3u")) {
        parameter->vector_alias = true;
        ++p->cursor;
        return true;
    }
    return word(p, "vec3") && punctuation(p, '<', "<u32>") &&
           word(p, "u32") && punctuation(p, '>', ">");
}
static bool entry_parameters(Parser *p, Syntax *s) {
    if (!punctuation(p, '(', "(")) return false;
    while (peek(p)->kind != ')') {
        if (s->parameter_count == MAX_ENTRY_PARAMETERS)
            return source_error(p->source, DOE_COMPILER_INPUT_ERROR, peek(p)->offset,
                                "entry point exceeds five builtin parameters");
        if (!entry_parameter(p, &s->parameters[s->parameter_count++])) return false;
        if (peek(p)->kind != ',') break;
        ++p->cursor;
    }
    return punctuation(p, ')', ")");
}

bool parse_tokens(Source *source, const TokenList *tokens, Syntax *syntax, bool allow_uniforms) {
    Parser p = { .source = source, .tokens = tokens };
    Statement *statements = syntax->statements;
    Expression *expressions = syntax->expressions;
    const size_t statement_capacity = syntax->statement_capacity;
    const size_t expression_capacity = syntax->expression_capacity;
    memset(syntax, 0, sizeof(*syntax));
    syntax->statements = statements; syntax->expressions = expressions;
    syntax->statement_capacity = statement_capacity; syntax->expression_capacity = expression_capacity;
    while (peek(&p)->kind == ';') ++p.cursor;
    if (peek(&p)->kind == TOKEN_WORD && spelled(&p, peek(&p), "const")) {
        ++p.cursor;
        syntax->has_constant = true;
        if (!name(&p, &syntax->constant_name) || !punctuation(&p, ':', ":") ||
            !word(&p, "u32") || !punctuation(&p, '=', "=") ||
            !integer(&p, true, &syntax->constant_value) || !punctuation(&p, ';', ";")) return false;
    }
    while (starts_storage(&p) || spelled(&p, peek(&p), "struct") ||
           peek(&p)->kind == ';') {
        if (peek(&p)->kind == ';') { ++p.cursor; continue; }
        if (spelled(&p, peek(&p), "struct")) {
            if (!allow_uniforms)
                return source_error(source, DOE_COMPILER_UNSUPPORTED, peek(&p)->offset,
                                    "struct declarations require compiler API v2");
            if (syntax->struct_count == DOE_COMPILER_MAX_STRUCTS)
                return source_error(source, DOE_COMPILER_INPUT_ERROR, peek(&p)->offset,
                                    "module exceeds eight flat structs");
            if (!struct_declaration(&p, &syntax->structs[syntax->struct_count++])) return false;
            continue;
        }
        if (syntax->resource_count == DOE_COMPILER_MAX_BINDINGS)
            return source_error(source, DOE_COMPILER_UNSUPPORTED, peek(&p)->offset,
                                "resource declaration count exceeds 8 bindings");
        if (!resource_declaration(&p, &syntax->resources[syntax->resource_count], allow_uniforms)) return false;
        ++syntax->resource_count;
    }
    if (!entry_attributes(&p, syntax) || !word(&p, "fn") || !name(&p, &syntax->entry) ||
        !entry_parameters(&p, syntax) || !punctuation(&p, '{', "{")) return false;
    if (!source->limits->extended && starts_legacy_alias(&p, syntax)) {
        ++p.cursor;
        syntax->has_alias = true;
        if (!name(&p, &syntax->alias) || !punctuation(&p, '=', "=") ||
            !name(&p, &syntax->alias_source) || !punctuation(&p, '.', ".") ||
            !word(&p, "x") || !punctuation(&p, ';', ";")) return false;
    }
    if (!block(&p, syntax, 0u)) return false;
    while (peek(&p)->kind == ';') ++p.cursor;
    if (peek(&p)->kind != TOKEN_END)
        return source_error(source, DOE_COMPILER_UNSUPPORTED, peek(&p)->offset,
                            "trailing declarations, statements or tokens are outside the subset");
    return true;
}
