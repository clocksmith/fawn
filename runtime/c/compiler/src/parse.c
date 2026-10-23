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
static bool expression_node(Parser *p, Syntax *s, Expression expression, uint32_t *id) {
    if (expression.depth > MAX_EXPRESSION_DEPTH)
        return source_error(p->source, DOE_COMPILER_INPUT_ERROR, expression.source_offset,
                            "expression depth exceeds 18 levels");
    if (s->expression_count == MAX_EXPRESSIONS)
        return source_error(p->source, DOE_COMPILER_INPUT_ERROR, expression.source_offset,
                            "function exceeds 320 expression nodes");
    *id = (uint32_t)s->expression_count;
    s->expressions[s->expression_count++] = expression;
    return true;
}
static bool expression(Parser *p, Syntax *s, unsigned precedence, unsigned nesting,
                       uint32_t *id);
static bool primary(Parser *p, Syntax *s, unsigned nesting, uint32_t *id) {
    if (nesting == MAX_EXPRESSION_DEPTH)
        return source_error(p->source, DOE_COMPILER_INPUT_ERROR, peek(p)->offset,
                            "expression depth exceeds 18 levels");
    Expression node = { .source_offset = peek(p)->offset, .depth = 1u };
    if (peek(p)->kind == '(') {
        ++p->cursor;
        if (!expression(p, s, 0u, nesting + 1u, id) || !punctuation(p, ')', ")")) return false;
        if (s->expressions[*id].depth == MAX_EXPRESSION_DEPTH)
            return source_error(p->source, DOE_COMPILER_INPUT_ERROR, node.source_offset,
                                "expression depth exceeds 18 levels");
        ++s->expressions[*id].depth;
        return true;
    }
    if (peek(p)->kind == TOKEN_NUMBER) {
        node.kind = EXPRESSION_INTEGER;
        Integer value;
        if (!integer(p, true, &value)) return false;
        node.value.integer = value.value;
    } else if (peek(p)->kind == TOKEN_DECIMAL) {
        node.kind = EXPRESSION_DECIMAL;
        Decimal value;
        if (!parse_decimal(p->source, peek(p), &value)) return false;
        node.value.decimal.numerator = value.numerator;
        node.value.decimal.denominator = value.denominator;
        ++p->cursor;
    } else {
        const Token *identifier_token = peek(p);
        NameSpan identifier;
        if (!name_span(p, &identifier)) return false;
        if (peek(p)->kind == '[') {
            node.kind = EXPRESSION_LOAD;
            node.value.reference.storage = identifier;
            ++p->cursor;
            if (!expression(p, s, 0u, nesting + 1u, &node.value.reference.index) ||
                !punctuation(p, ']', "]")) return false;
            node.depth = 1u + s->expressions[node.value.reference.index].depth;
        } else if (peek(p)->kind == '.') {
            ++p->cursor;
            node.kind = EXPRESSION_MEMBER;
            node.value.member.object = identifier;
            if (!name_span(p, &node.value.member.member)) return false;
        } else if (peek(p)->kind == '(') {
            if (!spelled(p, identifier_token, "max"))
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
static bool condition(Parser *p, Syntax *s, Condition *out) {
    if (!punctuation(p, '(', "(") || !expression(p, s, 0u, 0u, &out->left)) return false;
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
    return expression(p, s, 0u, 0u, &out->right) && punctuation(p, ')', ")");
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
            if (!condition(p, s, &statement->value.condition) || !punctuation(p, '{', "{") ||
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
            if (!name_span(p, &assignment->target.storage) ||
                !punctuation(p, '[', "[") ||
                !expression(p, s, 0u, 0u, &assignment->target.index) ||
                !punctuation(p, ']', "]") || !punctuation(p, '=', "=") ||
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
    memset(syntax, 0, sizeof(*syntax));
    if (peek(&p)->kind == TOKEN_WORD && spelled(&p, peek(&p), "const")) {
        ++p.cursor;
        syntax->has_constant = true;
        if (!name(&p, &syntax->constant_name) || !punctuation(&p, ':', ":") ||
            !word(&p, "u32") || !punctuation(&p, '=', "=") ||
            !integer(&p, true, &syntax->constant_value) || !punctuation(&p, ';', ";")) return false;
    }
    while (starts_storage(&p) || spelled(&p, peek(&p), "struct")) {
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
    if (starts_legacy_alias(&p, syntax)) {
        ++p.cursor;
        syntax->has_alias = true;
        if (!name(&p, &syntax->alias) || !punctuation(&p, '=', "=") ||
            !name(&p, &syntax->alias_source) || !punctuation(&p, '.', ".") ||
            !word(&p, "x") || !punctuation(&p, ';', ";")) return false;
    }
    if (!block(&p, syntax, 0u)) return false;
    if (peek(&p)->kind != TOKEN_END)
        return source_error(source, DOE_COMPILER_UNSUPPORTED, peek(&p)->offset,
                            "trailing declarations, statements or tokens are outside the subset");
    return true;
}
