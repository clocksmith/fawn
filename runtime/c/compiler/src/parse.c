/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "internal.h"
#include <string.h>

typedef struct { Source *source; const TokenList *tokens; size_t cursor; unsigned block_nesting; uint32_t exempt_comparison, exempt_literal; } Parser;
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
    *out = (Integer){ .value = value, .expression = UINT32_MAX, .offset = t->offset };
    ++p->cursor;
    return true;
}
static bool expression(Parser *, Syntax *, unsigned, unsigned, uint32_t *);
static bool attribute_value(Parser *p, Syntax *s, Integer *out) {
    const Token *token = peek(p);
    bool bare_decimal = token->kind == TOKEN_NUMBER;
    for (uint32_t i = 0u; i < token->length && bare_decimal; ++i) {
        const char c = p->source->source[token->offset + i];
        bare_decimal = c >= '0' && c <= '9';
    }
    if (bare_decimal && (p->tokens->tokens[p->cursor + 1u].kind == ')' ||
                         p->tokens->tokens[p->cursor + 1u].kind == ','))
        return integer(p, false, out);
    out->offset = token->offset;
    return expression(p, s, 0u, 0u, &out->expression);
}
static bool parenthesized_integer(Parser *p, Syntax *s, Integer *out) {
    return punctuation(p, '(', "(") && attribute_value(p, s, out) && punctuation(p, ')', ")");
}
static bool resource_attributes(Parser *p, Syntax *syntax, ResourceSyntax *s) {
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
        if (!parenthesized_integer(p, syntax, value)) return false;
    }
    return seen == 3u || reject(p, "one @group(G) and one @binding(B)");
}
static bool workgroup_dimensions(Parser *p, Syntax *s) {
    if (!punctuation(p, '(', "(")) return false;
    for (size_t i = 0; i < 3u; ++i) s->local_size[i] = (Integer){ .value = 1u, .expression = UINT32_MAX };
    size_t count = 0u;
    do {
        if (count == 3u) return reject(p, "at most three workgroup dimensions");
        if (!attribute_value(p, s, &s->local_size[count++])) return false;
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
static bool scalar_annotation(Parser *p, NameSpan *annotation, DoeCompilerScalar *scalar) {
    const Token *token = peek(p);
    if (!name_span(p, annotation)) return false;
    if (spelled(p, token, "u32")) *scalar = DOE_COMPILER_U32;
    else if (spelled(p, token, "f32")) *scalar = DOE_COMPILER_F32;
    else return source_error(p->source, DOE_COMPILER_UNSUPPORTED, annotation->offset,
        spelled(p, token, "bool") ? "bool is not a host-shareable resource type" :
        "resource reflection represents only u32 and f32 scalars");
    return true;
}
static bool struct_declaration(Parser *p, StructSyntax *s) {
    if (!word(p, "struct") || !name_span(p, &s->name) || !punctuation(p, '{', "{")) return false;
    do {
        if (s->member_count == DOE_COMPILER_MAX_STRUCT_MEMBERS)
            return source_error(p->source, DOE_COMPILER_INPUT_ERROR, peek(p)->offset,
                                "flat struct exceeds eight members");
        MemberDeclaration *member = &s->members[s->member_count++];
        if (!name_span(p, &member->name) || !punctuation(p, ':', ":") ||
            !scalar_annotation(p, &member->annotation, &member->scalar)) return false;
        if (peek(p)->kind != ',') break;
        ++p->cursor;
    } while (peek(p)->kind != '}');
    return punctuation(p, '}', "}");
}
static bool resource_declaration(Parser *p, Syntax *syntax, ResourceSyntax *s, bool allow_uniforms) {
    if (!resource_attributes(p, syntax, s) || !word(p, "var") ||
        !punctuation(p, '<', "<storage, access> or <uniform>")) return false;
    if (spelled(p, peek(p), "uniform")) {
        if (!allow_uniforms)
            return source_error(p->source, DOE_COMPILER_UNSUPPORTED, peek(p)->offset,
                                "uniform declarations require compiler API v2");
        ++p->cursor;
        s->kind = DOE_COMPILER_UNIFORM_BUFFER;
        s->access = DOE_COMPILER_READ;
        return punctuation(p, '>', ">") && name_span(p, &s->name) &&
               punctuation(p, ':', ":") && name_span(p, &s->type_name) && punctuation(p, ';', ";");
    }
    s->kind = DOE_COMPILER_STORAGE_BUFFER;
    if (!word(p, "storage") || !punctuation(p, ',', ",")) return false;
    if (spelled(p, peek(p), "read")) s->access = DOE_COMPILER_READ;
    else if (spelled(p, peek(p), "read_write")) s->access = DOE_COMPILER_READ_WRITE;
    else return reject(p, "read or read_write storage access");
    ++p->cursor;
    if (!punctuation(p, '>', ">") || !name_span(p, &s->name) || !punctuation(p, ':', ":") ||
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
    Expression node = { .source_offset = peek(p)->offset, .depth = 1u,
                        .left = UINT32_MAX, .right = UINT32_MAX };
    if (peek(p)->kind == '(') {
        ++p->cursor;
        if (!expression(p, s, 0u, nesting + 1u, id) || !punctuation(p, ')', ")")) return false;
        if (s->expressions[*id].depth >= p->source->limits->expression_depth)
            return source_error(p->source, DOE_COMPILER_INPUT_ERROR, node.source_offset, "expression depth capacity exceeded");
        ++s->expressions[*id].depth;
        s->expressions[*id].grouped = true;
        return true;
    }
    if (peek(p)->kind == '!' || peek(p)->kind == '-' || peek(p)->kind == '~') {
        node.kind = peek(p)->kind == '!' ? EXPRESSION_NOT :
                    peek(p)->kind == '-' ? EXPRESSION_NEGATE : EXPRESSION_COMPLEMENT;
        ++p->cursor;
        if (!primary(p, s, nesting + 1u, &node.left)) return false;
        node.depth = 1u + s->expressions[node.left].depth;
    } else if (peek(p)->kind == TOKEN_NUMBER || peek(p)->kind == TOKEN_DECIMAL) {
        node.kind = EXPRESSION_LITERAL;
        node.value.name = (NameSpan){ .offset = peek(p)->offset, .length = peek(p)->length };
        ++p->cursor;
    } else if (spelled(p, peek(p), "true") || spelled(p, peek(p), "false")) {
        node.kind = EXPRESSION_BOOLEAN;
        node.value.integer = spelled(p, peek(p), "true") ? 1u : 0u;
        ++p->cursor;
    } else {
        const Token *identifier_token = peek(p);
        NameSpan identifier;
        if (peek(p)->kind == '&' || peek(p)->kind == '*')
            return source_error(p->source, DOE_COMPILER_UNSUPPORTED, peek(p)->offset,
                                "pointer expressions are outside the scalar owner");
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
        } else if (peek(p)->kind == '(' ||
                   (peek(p)->kind == '<' && spelled(p, identifier_token, "bitcast"))) {
            node.kind = EXPRESSION_CALL;
            node.value.call.callee = identifier;
            if (peek(p)->kind == '<') {
                ++p->cursor;
                if (!name_span(p, &node.value.call.type) || !punctuation(p, '>', ">")) return false;
            }
            if (!punctuation(p, '(', "(")) return false;
            node.right = 0u;
            uint32_t tail = UINT32_MAX;
            while (peek(p)->kind != ')') {
                uint32_t argument;
                if (!expression(p, s, 0u, nesting + 1u, &argument)) return false;
                if (s->argument_count == s->argument_capacity)
                    return source_error(p->source, DOE_COMPILER_INPUT_ERROR, node.source_offset,
                                        "call argument capacity exceeded");
                const uint32_t link = (uint32_t)s->argument_count++;
                s->arguments[link] = (ArgumentSyntax){ argument, UINT32_MAX };
                if (tail == UINT32_MAX) node.left = link;
                else s->arguments[tail].next = link;
                tail = link;
                ++node.right;
                if (node.depth <= s->expressions[argument].depth)
                    node.depth = 1u + s->expressions[argument].depth;
                if (peek(p)->kind != ',') break;
                ++p->cursor;
            }
            if (!punctuation(p, ')', ")")) return false;
        } else { node.kind = EXPRESSION_NAME; node.value.name = identifier; }
    }
    return expression_node(p, s, node, node.source_offset != p->exempt_literal, id);
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
static unsigned binary_precedence(unsigned token, ExpressionKind *kind, Comparison *comparison) {
    if (token == '*' || token == '/' || token == '%') {
        *kind = token == '*' ? EXPRESSION_MULTIPLY : token == '/' ? EXPRESSION_DIVIDE : EXPRESSION_REMAINDER;
        return 6u;
    }
    if (token == '+' || token == '-') { *kind = token == '+' ? EXPRESSION_ADD : EXPRESSION_SUBTRACT; return 5u; }
    if (token == TOKEN_SHIFT_LEFT || token == TOKEN_SHIFT_RIGHT) {
        *kind = token == TOKEN_SHIFT_LEFT ? EXPRESSION_SHIFT_LEFT : EXPRESSION_SHIFT_RIGHT; return 4u;
    }
    if (comparison_kind(token, comparison)) { *kind = EXPRESSION_COMPARE; return 3u; }
    if (token == TOKEN_AND || token == TOKEN_OR) { *kind = token == TOKEN_AND ? EXPRESSION_AND : EXPRESSION_OR; return 2u; }
    if (token == '&' || token == '|' || token == '^') {
        *kind = token == '&' ? EXPRESSION_BIT_AND : token == '|' ? EXPRESSION_BIT_OR : EXPRESSION_BIT_XOR;
        return 1u;
    }
    return 0u;
}
static bool unary_form(const Expression *node) {
    if (node->grouped) return true;
    return node->kind == EXPRESSION_LOAD || node->kind == EXPRESSION_NAME ||
        node->kind == EXPRESSION_MEMBER || node->kind == EXPRESSION_LITERAL ||
        node->kind == EXPRESSION_BOOLEAN || node->kind == EXPRESSION_NOT ||
        node->kind == EXPRESSION_NEGATE || node->kind == EXPRESSION_COMPLEMENT ||
        node->kind == EXPRESSION_CONSTRUCT || node->kind == EXPRESSION_BITCAST ||
        node->kind == EXPRESSION_MAX || node->kind == EXPRESSION_CALL;
}
static bool expression(Parser *p, Syntax *s, unsigned minimum, unsigned nesting, uint32_t *id) {
    if (!primary(p, s, nesting, id)) return false;
    for (;;) {
        ExpressionKind kind = EXPRESSION_ADD;
        Comparison comparison = COMPARE_EQUAL;
        const unsigned precedence = binary_precedence(peek(p)->kind, &kind, &comparison);
        if (precedence == 0u || precedence < minimum) return true;
        const Expression left = s->expressions[*id];
        const bool shift = kind == EXPRESSION_SHIFT_LEFT || kind == EXPRESSION_SHIFT_RIGHT;
        const bool bit = precedence == 1u;
        if ((!left.grouped && ((kind == EXPRESSION_COMPARE && left.kind == EXPRESSION_COMPARE) ||
            ((kind == EXPRESSION_AND || kind == EXPRESSION_OR) &&
             (left.kind == EXPRESSION_AND || left.kind == EXPRESSION_OR) && kind != left.kind))) ||
            (shift && !unary_form(&left)) || (bit && !unary_form(&left) && left.kind != kind))
            return reject(p, "WGSL parentheses around incompatible operator groups");
        Expression node = { .source_offset = peek(p)->offset, .kind = kind, .left = *id };
        node.value.comparison = comparison;
        const bool exempt = kind == EXPRESSION_COMPARE && node.source_offset == p->exempt_comparison;
        ++p->cursor;
        if (!expression(p, s, precedence + 1u, nesting + (exempt ? 0u : 1u), &node.right)) return false;
        const Expression *right = &s->expressions[node.right];
        if ((shift || bit) && !unary_form(right))
            return reject(p, "unary operands or parentheses for shift/bitwise expression");
        node.depth = (exempt ? 0u : 1u) + (left.depth > right->depth ? left.depth : right->depth);
        if (!expression_node(p, s, node, !exempt, id)) return false;
    }
}
static bool local_declaration(Parser *p, Syntax *s, LocalSyntax *local, unsigned terminator) {
    local->mutable = spelled(p, peek(p), "var");
    local->constant = spelled(p, peek(p), "const");
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
        else if (spelled(p, type, "bool")) local->type = IR_BOOL;
        else if (spelled(p, type, "i32")) local->type = IR_I32;
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
    /* Preserve only the old accounting exemption, never a second grammar. */
    uint32_t comparison_offset = UINT32_MAX;
    if (p->source->limits->legacy_counts && peek(p)->kind == '(') {
        unsigned depth = 0u, brackets = 0u, comparisons = 0u;
        size_t end = p->cursor;
        for (; end < p->tokens->count; ++end) {
            const Token *token = &p->tokens->tokens[end];
            if (token->kind == '(') ++depth;
            else if (token->kind == ')') { if (--depth == 0u) break; }
            else if (token->kind == '[') ++brackets;
            else if (token->kind == ']' && brackets != 0u) --brackets;
            Comparison ignored;
            if (depth == 1u && brackets == 0u && comparison_kind(token->kind, &ignored)) {
                ++comparisons; comparison_offset = token->offset;
            }
            if (depth == 1u && (token->kind == TOKEN_AND || token->kind == TOKEN_OR ||
                token->kind == '&' || token->kind == '|' || token->kind == '^')) comparisons += 2u;
        }
        if (comparisons != 1u || end + 1u >= p->tokens->count ||
            p->tokens->tokens[end + 1u].kind != '{') comparison_offset = UINT32_MAX;
    }
    if (comparison_offset == UINT32_MAX) return expression(p, s, 0u, 0u, id);
    ++p->cursor;
    p->exempt_comparison = comparison_offset;
    const bool ok = expression(p, s, 0u, 0u, id) && punctuation(p, ')', ")");
    p->exempt_comparison = UINT32_MAX;
    return ok;
}
static bool assignment(Parser *p, Syntax *s, Assignment *out, unsigned terminator) {
    if (!name_span(p, &out->target.storage)) return false;
    out->local = peek(p)->kind != '[';
    if (!out->local) {
        ++p->cursor;
        if (!expression(p, s, 0u, 0u, &out->target.index) || !punctuation(p, ']', "]")) return false;
    }
    switch (peek(p)->kind) {
        case '=': out->operation = UPDATE_SET; break;
        case TOKEN_ADD_ASSIGN: out->operation = UPDATE_ADD; break;
        case TOKEN_SUB_ASSIGN: out->operation = UPDATE_SUBTRACT; break;
        case TOKEN_MUL_ASSIGN: out->operation = UPDATE_MULTIPLY; break;
        case TOKEN_DIV_ASSIGN: out->operation = UPDATE_DIVIDE; break;
        case TOKEN_REM_ASSIGN: out->operation = UPDATE_REMAINDER; break;
        case TOKEN_BIT_AND_ASSIGN: out->operation = UPDATE_BIT_AND; break;
        case TOKEN_BIT_OR_ASSIGN: out->operation = UPDATE_BIT_OR; break;
        case TOKEN_BIT_XOR_ASSIGN: out->operation = UPDATE_BIT_XOR; break;
        case TOKEN_SHIFT_LEFT_ASSIGN: out->operation = UPDATE_SHIFT_LEFT; break;
        case TOKEN_SHIFT_RIGHT_ASSIGN: out->operation = UPDATE_SHIFT_RIGHT; break;
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
    if (declaration && (spelled(p, peek(p), "let") || spelled(p, peek(p), "var") || spelled(p, peek(p), "const"))) {
        st->kind = spelled(p, peek(p), "var") ? STATEMENT_VAR :
                   spelled(p, peek(p), "const") ? STATEMENT_CONST : STATEMENT_LET;
        if (!local_declaration(p, s, &st->value.local, terminator)) return false;
    } else if (peek(p)->kind == TOKEN_WORD && p->cursor + 1u < p->tokens->count &&
               (p->tokens->tokens[p->cursor + 1u].kind == '(' ||
                (spelled(p, peek(p), "bitcast") && p->tokens->tokens[p->cursor + 1u].kind == '<'))) {
        st->kind = STATEMENT_CALL;
        if (!primary(p, s, 0u, &st->value.condition)) return false;
        if (s->expressions[st->value.condition].kind != EXPRESSION_CALL)
            return reject(p, "function call statement");
        if (terminator != 0u && !punctuation(p, terminator, ";")) return false;
    } else {
        st->kind = STATEMENT_ASSIGN;
        if (!assignment(p, s, &st->value.assignment, terminator)) return false;
    }
    st->end = (uint32_t)s->statement_count;
    return true;
}
static bool block(Parser *p, Syntax *s, unsigned depth);
static bool statement(Parser *p, Syntax *s, unsigned depth) {
    const Token *token = peek(p);
    const bool is_if = spelled(p, token, "if");
    const bool is_for = spelled(p, token, "for");
    const bool is_while = spelled(p, token, "while");
    const bool is_loop = spelled(p, token, "loop");
    const bool is_block = token->kind == '{';
    const bool is_continuing = spelled(p, token, "continuing");
    if (is_if || is_for || is_while || is_loop || is_block) {
        if (depth >= p->source->limits->control_depth)
            return source_error(p->source, DOE_COMPILER_INPUT_ERROR, token->offset, "control depth capacity exceeded");
    }
    uint32_t id;
    if (spelled(p, token, "let") || spelled(p, token, "var") || spelled(p, token, "const"))
        return simple_statement(p, s, true, ';', &id);
    if (!is_if && !is_for && !is_while && !is_loop && !is_block && !is_continuing &&
        !spelled(p, token, "return") && !((spelled(p, token, "break") || spelled(p, token, "continue") || token->kind == ';')))
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
            if (spelled(p, peek(p), "if")) {
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
        st->value.condition = UINT32_MAX;
        if (peek(p)->kind != ';' && !expression(p, s, 0u, 0u, &st->value.condition)) return false;
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

static bool module_constant(Parser *p, Syntax *s, bool first) {
    if (s->constant_count == s->constant_capacity)
        return source_error(p->source, DOE_COMPILER_INPUT_ERROR, peek(p)->offset,
                            "module constant capacity exceeded");
    /* Exact retained declaration: const NAME : u32 = DECIMAL_U ; */
    if (first && p->tokens->count - p->cursor >= 7u) {
        const Token *t = p->tokens->tokens + p->cursor;
        bool digits = t[5].kind == TOKEN_NUMBER && t[5].length >= 2u &&
            p->source->source[t[5].offset + t[5].length - 1u] == 'u';
        for (uint32_t i = 0u; digits && i + 1u < t[5].length; ++i) {
            const char c = p->source->source[t[5].offset + i];
            digits = c >= '0' && c <= '9';
        }
        if (t[1].kind == TOKEN_WORD && t[2].kind == ':' && spelled(p, &t[3], "u32") &&
            t[4].kind == '=' && digits && t[6].kind == ';') p->exempt_literal = t[5].offset;
    }
    LocalSyntax local = {0};
    ConstantSyntax *constant = &s->constants[s->constant_count];
    memset(constant, 0, sizeof(*constant));
    constant->first_expression = (uint32_t)s->expression_count;
    if (!local_declaration(p, s, &local, ';')) return false;
    p->exempt_literal = UINT32_MAX;
    constant->name = local.name;
    constant->annotation = local.annotation;
    constant->expression = local.expression;
    ++s->constant_count;
    return true;
}
static bool function_declaration(Parser *p, Syntax *s, bool *has_entry) {
    if (s->function_count == s->function_capacity)
        return source_error(p->source, DOE_COMPILER_INPUT_ERROR, peek(p)->offset,
                            "function declaration capacity exceeded");
    FunctionRecord *function = &s->functions[s->function_count];
    memset(function, 0, sizeof(*function));
    function->result_type = IR_VOID;
    if (peek(p)->kind == '@' && p->cursor + 1u < p->tokens->count &&
        spelled(p, &p->tokens->tokens[p->cursor + 1u], "must_use")) {
        function->flags = FUNCTION_MUST_USE;
        p->cursor += 2u;
    }
    const bool entry = peek(p)->kind == '@';
    if (entry) {
        if (*has_entry)
            return source_error(p->source, DOE_COMPILER_UNSUPPORTED, peek(p)->offset,
                                "one compute entry is supported");
        if (!entry_attributes(p, s)) return false;
        function->flags |= FUNCTION_ENTRY;
        s->entry_function = (uint32_t)s->function_count;
    }
    if (!word(p, "fn") || !name_span(p, &function->name)) return false;
    function->formal_start = (uint32_t)s->formal_count;
    if (entry) {
        memcpy(s->entry.text, p->source->source + function->name.offset, function->name.length);
        s->entry.text[function->name.length] = '\0';
        s->entry.offset = function->name.offset;
        if (!entry_parameters(p, s)) return false;
    } else {
        if (!punctuation(p, '(', "(")) return false;
        while (peek(p)->kind != ')') {
            FormalRecord formal;
            if (!name_span(p, &formal.name) || !punctuation(p, ':', ":") ||
                !name_span(p, &formal.type.annotation)) return false;
            if (s->formal_count == s->formal_capacity)
                return source_error(p->source, DOE_COMPILER_INPUT_ERROR, formal.name.offset,
                                    "formal parameter capacity exceeded");
            s->formals[s->formal_count++] = formal;
            ++function->formal_count;
            if (peek(p)->kind != ',') break;
            ++p->cursor;
        }
        if (!punctuation(p, ')', ")")) return false;
    }
    if (peek(p)->kind == TOKEN_ARROW) {
        ++p->cursor;
        if (!name_span(p, &function->annotation)) return false;
    }
    if (!punctuation(p, '{', "{")) return false;
    function->statement_start = (uint32_t)s->statement_count;
    function->expression_start = (uint32_t)s->expression_count;
    if (entry && p->source->limits->legacy_counts && starts_legacy_alias(p, s)) {
        ++p->cursor;
        s->has_alias = true;
        if (!name(p, &s->alias) || !punctuation(p, '=', "=") ||
            !name(p, &s->alias_source) || !punctuation(p, '.', ".") ||
            !word(p, "x") || !punctuation(p, ';', ";")) return false;
    }
    if (!block(p, s, 0u)) return false;
    function->statement_end = (uint32_t)s->statement_count;
    function->expression_end = (uint32_t)s->expression_count;
    ++s->function_count;
    if (entry) *has_entry = true;
    return true;
}
bool parse_tokens(Source *source, const TokenList *tokens, Syntax *syntax, bool allow_uniforms) {
    Parser p = { .source = source, .tokens = tokens,
                 .exempt_comparison = UINT32_MAX, .exempt_literal = UINT32_MAX };
    syntax->resource_count = 0u; syntax->struct_count = 0u;
    syntax->parameter_count = 0u; syntax->constant_count = 0u;
    syntax->statement_count = 0u; syntax->expression_count = 0u;
    syntax->expression_budget_count = 0u; syntax->function_count = 0u;
    syntax->formal_count = 0u; syntax->argument_count = 0u; syntax->has_alias = false;
    memset(syntax->resources, 0, sizeof(syntax->resources));
    memset(syntax->structs, 0, sizeof(syntax->structs));
    memset(syntax->parameters, 0, sizeof(syntax->parameters));
    memset(syntax->callees, 0, syntax->expression_capacity * sizeof(*syntax->callees));
    bool has_entry = false, first = true;
    while (peek(&p)->kind != TOKEN_END) {
        if (peek(&p)->kind == ';') { ++p.cursor; continue; }
        if (spelled(&p, peek(&p), "const")) {
            if (!module_constant(&p, syntax, first)) return false;
        } else if (spelled(&p, peek(&p), "struct")) {
            if (!allow_uniforms)
                return source_error(source, DOE_COMPILER_UNSUPPORTED, peek(&p)->offset,
                                    "ABI1 reflection cannot represent uniform struct layouts");
            if (syntax->struct_count == DOE_COMPILER_MAX_STRUCTS)
                return source_error(source, DOE_COMPILER_INPUT_ERROR, peek(&p)->offset,
                                    "module exceeds eight flat structs");
            if (!struct_declaration(&p, &syntax->structs[syntax->struct_count++])) return false;
        } else if (starts_storage(&p)) {
            if (syntax->resource_count == DOE_COMPILER_MAX_BINDINGS)
                return source_error(source, DOE_COMPILER_UNSUPPORTED, peek(&p)->offset,
                                    "resource declaration count exceeds 8 bindings");
            if (!resource_declaration(&p, syntax, &syntax->resources[syntax->resource_count], allow_uniforms)) return false;
            ++syntax->resource_count;
        } else if (!function_declaration(&p, syntax, &has_entry)) return false;
        first = false;
    }
    return has_entry || reject(&p, "one compute entry point");
}
