/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "internal.h"
#include <string.h>

typedef struct { Source *source; const TokenList *tokens; size_t cursor; } Parser;
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
static bool resource_attributes(Parser *p, StorageSyntax *s) {
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
static bool entry_attributes(Parser *p, Syntax *s) {
    unsigned seen = 0u;
    while (peek(p)->kind == '@') {
        ++p->cursor;
        const Token *t = peek(p);
        unsigned flag;
        if (spelled(p, t, "compute")) flag = 1u;
        else if (spelled(p, t, "workgroup_size")) flag = 2u;
        else return source_error(p->source, DOE_COMPILER_UNSUPPORTED, t->offset,
                                 "only @compute and @workgroup_size are supported on the entry point");
        if ((seen & flag) != 0u)
            return source_error(p->source, DOE_COMPILER_SEMANTIC_ERROR, t->offset, "duplicate entry-point attribute");
        seen |= flag;
        ++p->cursor;
        if (flag == 2u && !parenthesized_integer(p, &s->local_size)) return false;
    }
    return seen == 3u || reject(p, "one @compute and one @workgroup_size(N)");
}
static bool storage_declaration(Parser *p, StorageSyntax *s) {
    if (!resource_attributes(p, s) || !word(p, "var") ||
        !punctuation(p, '<', "<storage, access>") || !word(p, "storage") ||
        !punctuation(p, ',', ",")) return false;
    if (spelled(p, peek(p), "read")) s->access = DOE_COMPILER_READ;
    else if (spelled(p, peek(p), "read_write")) s->access = DOE_COMPILER_READ_WRITE;
    else return reject(p, "read or read_write storage access");
    ++p->cursor;
    if (!punctuation(p, '>', ">") || !name(p, &s->name) || !punctuation(p, ':', ":") ||
        !word(p, "array") || !punctuation(p, '<', "<u32 or f32>")) return false;
    if (spelled(p, peek(p), "u32")) s->scalar = DOE_COMPILER_U32;
    else if (spelled(p, peek(p), "f32")) s->scalar = DOE_COMPILER_F32;
    else return reject(p, "u32 or f32 runtime array element");
    ++p->cursor;
    return punctuation(p, '>', ">") && punctuation(p, ';', ";");
}
static bool atom(Parser *p, bool member, Atom *out) {
    if (peek(p)->kind == TOKEN_NUMBER) {
        out->kind = ATOM_LITERAL;
        return integer(p, true, &out->value.literal);
    }
    out->kind = ATOM_NAME;
    if (!name(p, &out->value.name)) return false;
    if (member && peek(p)->kind == '.') {
        ++p->cursor;
        out->kind = ATOM_MEMBER_X;
        return word(p, "x");
    }
    return true;
}
static bool index_expression(Parser *p, Index *out) {
    if (!atom(p, true, &out->base)) return false;
    if (peek(p)->kind == '+' || peek(p)->kind == '-') {
        out->operation = peek(p)->kind == '+' ? INDEX_ADD : INDEX_SUBTRACT;
        ++p->cursor;
        return atom(p, false, &out->offset);
    }
    out->operation = INDEX_BASE;
    return true;
}
static bool reference_suffix(Parser *p, Reference *reference) {
    return punctuation(p, '[', "[") && index_expression(p, &reference->index) &&
           punctuation(p, ']', "]");
}
static bool expression_node(Parser *p, Syntax *s, Expression expression, uint32_t *id) {
    if (expression.depth > MAX_EXPRESSION_DEPTH)
        return source_error(p->source, DOE_COMPILER_INPUT_ERROR, expression.source_offset,
                            "expression depth exceeds 16 levels");
    if (s->expression_count == MAX_EXPRESSIONS)
        return source_error(p->source, DOE_COMPILER_INPUT_ERROR, expression.source_offset,
                            "function exceeds 64 expression nodes");
    *id = (uint32_t)s->expression_count;
    s->expressions[s->expression_count++] = expression;
    return true;
}
static bool expression(Parser *p, Syntax *s, unsigned precedence, unsigned nesting,
                       uint32_t *id);
static bool primary(Parser *p, Syntax *s, unsigned nesting, uint32_t *id) {
    if (nesting == MAX_EXPRESSION_DEPTH)
        return source_error(p->source, DOE_COMPILER_INPUT_ERROR, peek(p)->offset,
                            "expression depth exceeds 16 levels");
    Expression node = { .source_offset = peek(p)->offset, .depth = 1u };
    if (peek(p)->kind == '(') {
        ++p->cursor;
        if (!expression(p, s, 0u, nesting + 1u, id) || !punctuation(p, ')', ")")) return false;
        if (s->expressions[*id].depth == MAX_EXPRESSION_DEPTH)
            return source_error(p->source, DOE_COMPILER_INPUT_ERROR, node.source_offset,
                                "expression depth exceeds 16 levels");
        ++s->expressions[*id].depth;
        return true;
    }
    if (peek(p)->kind == TOKEN_NUMBER) {
        node.kind = EXPRESSION_INTEGER;
        if (!integer(p, true, &node.value.integer)) return false;
    } else if (peek(p)->kind == TOKEN_DECIMAL) {
        node.kind = EXPRESSION_DECIMAL;
        if (!parse_decimal(p->source, peek(p), &node.value.decimal)) return false;
        ++p->cursor;
    } else {
        Name identifier;
        if (!name(p, &identifier)) return false;
        if (peek(p)->kind == '[') {
            node.kind = EXPRESSION_LOAD;
            node.value.reference.storage = identifier;
            if (!reference_suffix(p, &node.value.reference)) return false;
        } else if (peek(p)->kind == '.') {
            ++p->cursor;
            node.kind = EXPRESSION_MEMBER_X;
            node.value.name = identifier;
            if (!word(p, "x")) return false;
        } else if (peek(p)->kind == '(') {
            if (strcmp(identifier.text, "max") != 0)
                return source_error(p->source, DOE_COMPILER_UNSUPPORTED, identifier.offset,
                                    "only scalar max calls are supported");
            ++p->cursor;
            node.kind = EXPRESSION_MAX;
            node.value.name = identifier;
            if (!expression(p, s, 0u, nesting + 1u, &node.left) ||
                !punctuation(p, ',', ",") ||
                !expression(p, s, 0u, nesting + 1u, &node.right) ||
                !punctuation(p, ')', ")")) return false;
            const unsigned left = s->expressions[node.left].depth;
            const unsigned right = s->expressions[node.right].depth;
            node.depth = 1u + (left > right ? left : right);
        } else {
            node.kind = EXPRESSION_NAME;
            node.value.name = identifier;
        }
    }
    return expression_node(p, s, node, id);
}
static bool expression(Parser *p, Syntax *s, unsigned precedence, unsigned nesting,
                       uint32_t *id) {
    if (!primary(p, s, nesting, id)) return false;
    for (;;) {
        const unsigned token = peek(p)->kind;
        const unsigned next = token == '*' ? 2u : token == '+' || token == '-' ? 1u : 0u;
        if (next == 0u || next <= precedence) return true;
        Expression node = { .kind = token == '*' ? EXPRESSION_MULTIPLY :
                                     token == '+' ? EXPRESSION_ADD : EXPRESSION_SUBTRACT,
                            .source_offset = peek(p)->offset, .left = *id };
        ++p->cursor;
        if (!expression(p, s, next, nesting + 1u, &node.right)) return false;
        const unsigned left = s->expressions[node.left].depth;
        const unsigned right = s->expressions[node.right].depth;
        node.depth = 1u + (left > right ? left : right);
        if (!expression_node(p, s, node, id)) return false;
    }
}
static bool local_declaration(Parser *p, Syntax *s, LocalSyntax *local) {
    if (!word(p, "let") || !name(p, &local->name)) return false;
    if (peek(p)->kind == ':') {
        ++p->cursor;
        local->annotated = true;
        if (!name(p, &local->annotation)) return false;
        if (strcmp(local->annotation.text, "u32") == 0) local->scalar = DOE_COMPILER_U32;
        else if (strcmp(local->annotation.text, "f32") == 0) local->scalar = DOE_COMPILER_F32;
        else return source_error(p->source, DOE_COMPILER_UNSUPPORTED, local->annotation.offset,
                                 "local annotation requires u32 or f32");
    }
    return punctuation(p, '=', "=") && expression(p, s, 0u, 0u, &local->expression) &&
           punctuation(p, ';', ";");
}
static bool condition(Parser *p, Condition *out) {
    if (!punctuation(p, '(', "(") || !index_expression(p, &out->left)) return false;
    switch (peek(p)->kind) {
        case TOKEN_EQUAL: out->comparison = COMPARE_EQUAL; break;
        case TOKEN_NOT_EQUAL: out->comparison = COMPARE_NOT_EQUAL; break;
        case '<': out->comparison = COMPARE_LESS; break;
        case TOKEN_LESS_EQUAL: out->comparison = COMPARE_LESS_EQUAL; break;
        case '>': out->comparison = COMPARE_GREATER; break;
        case TOKEN_GREATER_EQUAL: out->comparison = COMPARE_GREATER_EQUAL; break;
        default: return reject(p, "unsigned comparison operator");
    }
    ++p->cursor;
    return atom(p, false, &out->right) && punctuation(p, ')', ")");
}
static bool block(Parser *p, Syntax *s, unsigned depth) {
    while (peek(p)->kind != '}') {
        if (peek(p)->kind == TOKEN_END) return reject(p, "statement or }");
        if (s->statement_count == MAX_STATEMENTS)
            return source_error(p->source, DOE_COMPILER_INPUT_ERROR, peek(p)->offset,
                                "function exceeds 16 statements");
        Statement *statement = &s->statements[s->statement_count++];
        statement->source_offset = peek(p)->offset;
        if (spelled(p, peek(p), "if")) {
            if (depth == MAX_IF_DEPTH)
                return source_error(p->source, DOE_COMPILER_INPUT_ERROR, peek(p)->offset,
                                    "if nesting exceeds four levels");
            ++p->cursor;
            statement->kind = STATEMENT_IF;
            if (!condition(p, &statement->value.condition) || !punctuation(p, '{', "{") ||
                !block(p, s, depth + 1u)) return false;
            statement->else_start = s->statement_count;
            if (spelled(p, peek(p), "else")) {
                ++p->cursor;
                if (!punctuation(p, '{', "{") || !block(p, s, depth + 1u)) return false;
            }
            statement->end = s->statement_count;
        } else if (spelled(p, peek(p), "return")) {
            ++p->cursor;
            statement->kind = STATEMENT_RETURN;
            if (!punctuation(p, ';', ";")) return false;
        } else if (spelled(p, peek(p), "let")) {
            statement->kind = STATEMENT_LET;
            if (!local_declaration(p, s, &statement->value.local)) return false;
        } else {
            statement->kind = STATEMENT_ASSIGN;
            Assignment *assignment = &statement->value.assignment;
            if (!name(p, &assignment->target.storage) ||
                !reference_suffix(p, &assignment->target) || !punctuation(p, '=', "=") ||
                !expression(p, s, 0u, 0u, &assignment->expression) ||
                !punctuation(p, ';', ";")) return false;
        }
    }
    return punctuation(p, '}', "}");
}
static bool starts_storage(const Parser *p) {
    if (peek(p)->kind != '@' || p->cursor + 1u >= p->tokens->count) return false;
    const Token *attribute = &p->tokens->tokens[p->cursor + 1u];
    return spelled(p, attribute, "group") || spelled(p, attribute, "binding");
}

/* Recognize only the retained unannotated leading alias grammar. Other lets
 * belong to the general expression/statement budgets, without speculative parse. */
static bool starts_legacy_alias(const Parser *p) {
    if (p->tokens->count - p->cursor < 7u) return false;
    const Token *t = p->tokens->tokens + p->cursor;
    return t[0].kind == TOKEN_WORD && spelled(p, &t[0], "let") &&
           t[1].kind == TOKEN_WORD && t[2].kind == '=' && t[3].kind == TOKEN_WORD &&
           t[4].kind == '.' && t[5].kind == TOKEN_WORD && spelled(p, &t[5], "x") &&
           t[6].kind == ';';
}

bool parse_tokens(Source *source, const TokenList *tokens, Syntax *syntax) {
    Parser p = { .source = source, .tokens = tokens };
    memset(syntax, 0, sizeof(*syntax));
    if (peek(&p)->kind == TOKEN_WORD && spelled(&p, peek(&p), "const")) {
        ++p.cursor;
        syntax->has_constant = true;
        if (!name(&p, &syntax->constant_name) || !punctuation(&p, ':', ":") ||
            !word(&p, "u32") || !punctuation(&p, '=', "=") ||
            !integer(&p, true, &syntax->constant_value) || !punctuation(&p, ';', ";")) return false;
    }
    while (starts_storage(&p)) {
        if (syntax->storage_count == DOE_COMPILER_MAX_BINDINGS)
            return source_error(source, DOE_COMPILER_UNSUPPORTED, peek(&p)->offset,
                                "storage declaration count exceeds 8 bindings");
        if (!storage_declaration(&p, &syntax->storage[syntax->storage_count])) return false;
        ++syntax->storage_count;
    }
    if (syntax->storage_count == 0u) return reject(&p, "at least one storage declaration");
    if (!entry_attributes(&p, syntax) || !word(&p, "fn") || !name(&p, &syntax->entry) ||
        !punctuation(&p, '(', "(") || !punctuation(&p, '@', "@builtin(global_invocation_id)") ||
        !word(&p, "builtin") || !punctuation(&p, '(', "(") || !word(&p, "global_invocation_id") ||
        !punctuation(&p, ')', ")") || !name(&p, &syntax->invocation) ||
        !punctuation(&p, ':', ":")) return false;
    if (spelled(&p, peek(&p), "vec3u")) { syntax->vector_alias = true; ++p.cursor; }
    else if (!word(&p, "vec3") || !punctuation(&p, '<', "<u32>") ||
             !word(&p, "u32") || !punctuation(&p, '>', ">")) return false;
    if (!punctuation(&p, ')', ")") || !punctuation(&p, '{', "{")) return false;
    if (starts_legacy_alias(&p)) {
        ++p.cursor;
        syntax->has_alias = true;
        if (!name(&p, &syntax->alias) || !punctuation(&p, '=', "=") ||
            !name(&p, &syntax->alias_source) || !punctuation(&p, '.', ".") ||
            !word(&p, "x") || !punctuation(&p, ';', ";")) return false;
    }
    if (!block(&p, syntax, 0u)) return false;
    if (syntax->statement_count == 0u) return reject(&p, "at least one statement");
    if (peek(&p)->kind != TOKEN_END)
        return source_error(source, DOE_COMPILER_UNSUPPORTED, peek(&p)->offset,
                            "trailing declarations, statements or tokens are outside the subset");
    return true;
}
