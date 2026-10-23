/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "internal.h"
#include <string.h>

typedef struct {
    Source *source; CompilerWorkspace *tokens; size_t cursor;
    unsigned block_nesting, active_expression, active_type, template_count;
    uint32_t exempt_comparison, exempt_literal;
} Parser;
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
static bool resource_attributes(Parser *p, Syntax *syntax, ResourceRecord *s) {
    unsigned seen=0u;
    while (peek(p)->kind=='@') {
        ++p->cursor; const Token *t=peek(p); unsigned flag;
        if (spelled(p,t,"group")) flag=1u;
        else if (spelled(p,t,"binding")) flag=2u;
        else return source_error(p->source,DOE_COMPILER_UNSUPPORTED,t->offset,"resource requires group and binding attributes");
        if ((seen&flag)!=0u) return source_error(p->source,DOE_COMPILER_SEMANTIC_ERROR,t->offset,"duplicate resource attribute");
        seen|=flag; ++p->cursor; Integer value={0};
        if (!parenthesized_integer(p,syntax,&value)) return false;
        if (flag==1u) {s->group=value.value;s->group_expression=value.expression;}
        else {s->binding=value.value;s->binding_expression=value.expression;}
    }
    return seen==3u || reject(p,"one @group(G) and one @binding(B)");
}
static bool workgroup_dimensions(Parser *p, Syntax *s) {
    if (!punctuation(p, '(', "(")) return false;
    for (size_t i = 0; i < 3u; ++i) s->entries[s->entry_count].state.dimensions[i] = (Integer){ .value = 1u, .expression = UINT32_MAX };
    size_t count = 0u;
    do {
        if (count == 3u) return reject(p, "at most three workgroup dimensions");
        if (!attribute_value(p, s, &s->entries[s->entry_count].state.dimensions[count++])) return false;
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
static bool type_reference(Parser *,Syntax *,TypeRef *);
static bool template_close(Parser *p) {
    Token *t=&p->tokens->tokens[p->cursor];
    if (t->kind==TOKEN_SHIFT_RIGHT || t->kind==TOKEN_GREATER_EQUAL || t->kind==TOKEN_SHIFT_RIGHT_ASSIGN) {
        t->kind=t->kind==TOKEN_SHIFT_RIGHT ? '>' : t->kind==TOKEN_GREATER_EQUAL ? '=' : TOKEN_GREATER_EQUAL;
        ++t->offset; --t->length; return true;
    }
    return punctuation(p,'>',">");
}
static bool generic_reference(Parser *p,Syntax *s,NameSpan name,TypeRef *out) {
    if (p->active_type >= p->source->limits->type_depth)
        return source_pool_error(p->source,DOE_COMPILER_POOL_TYPE_SYNTAX_DEPTH,(size_t)p->active_type+1u,name.offset,"type syntax depth capacity exceeded");
    ++p->active_type;
    if (s->generic_count==s->limits.generic_syntax)
        return source_pool_error(p->source,DOE_COMPILER_POOL_TYPE_SYNTAX,(size_t)s->generic_count+1u,name.offset,"generic type syntax capacity exceeded");
    const uint32_t id=s->generic_count++;
    GenericSyntax *g=&s->generic_syntax[id];
    *g=(GenericSyntax){.name=name,.element={NO_ID,NO_ID},.count_expression=NO_ID};
    ++p->cursor;
    if (!type_reference(p,s,&g->element)) return false;
    if (peek(p)->kind==',') {
        ++p->cursor; ++p->template_count;
        const bool ok=expression(p,s,0u,0u,&g->count_expression);
        --p->template_count; if (!ok) return false;
        if (peek(p)->kind==',') ++p->cursor;
    }
    if (!template_close(p)) return false;
    --p->active_type; *out=(TypeRef){name.offset,id}; return true;
}
static bool type_reference(Parser *p,Syntax *s,TypeRef *out) {
    NameSpan name;
    if (!name_span(p,&name)) return false;
    *out=(TypeRef){name.offset,NO_ID};
    return peek(p)->kind!='<' || generic_reference(p,s,name,out);
}
static bool struct_declaration(Parser *p,Syntax *s) {
    NameSpan name;
    if (!word(p,"struct") || !name_span(p,&name) || !punctuation(p,'{',"{")) return false;
    if (s->type_count==s->limits.types)
        return source_pool_error(p->source,DOE_COMPILER_POOL_TYPES,(size_t)s->type_count+1u,name.offset,"type capacity exceeded");
    TypeRecord *t=&s->types[s->type_count++];
    *t=(TypeRecord){.kind=TYPE_STRUCT,.name=name,.element=s->member_count,.target=NO_ID,.aux=NO_ID};
    t->state.dependency.first_expression=s->expression_count;
    do {
        if (s->member_count==s->limits.members)
            return source_pool_error(p->source,DOE_COMPILER_POOL_MEMBERS,(size_t)s->member_count+1u,peek(p)->offset,"member capacity exceeded");
        MemberRecord *m=&s->members[s->member_count];
        *m=(MemberRecord){.align_expression=NO_ID,.size_expression=NO_ID};
        while (peek(p)->kind=='@') {
            ++p->cursor; uint32_t flag; uint32_t *value;
            if (spelled(p,peek(p),"align")) {flag=1u;value=&m->align_expression;}
            else if (spelled(p,peek(p),"size")) {flag=2u;value=&m->size_expression;}
            else return source_error(p->source,DOE_COMPILER_UNSUPPORTED,peek(p)->offset,"unsupported struct member attribute");
            if ((m->flags&flag)!=0u) return source_error(p->source,DOE_COMPILER_SEMANTIC_ERROR,peek(p)->offset,"duplicate member attribute");
            m->flags|=flag; ++p->cursor;
            if (!punctuation(p,'(',"(") || !expression(p,s,0u,0u,value) || !punctuation(p,')',")")) return false;
        }
        if (!name_span(p,&m->name) || !punctuation(p,':',":") || !type_reference(p,s,&m->annotation)) return false;
        ++s->member_count; ++t->count;
        if (peek(p)->kind!=',') break;
        ++p->cursor;
    } while (peek(p)->kind!='}');
    t->state.dependency.end_expression=s->expression_count;
    return punctuation(p,'}',"}");
}
static bool resource_declaration(Parser *p,Syntax *s,ResourceRecord *r) {
    *r=(ResourceRecord){.source_offset=peek(p)->offset,.tail_type=NO_ID};
    if (!resource_attributes(p,s,r) || !word(p,"var")) return false;
    if (peek(p)->kind != '<')
        return name_span(p,&r->name) && punctuation(p,':',":") &&
            type_reference(p,s,&r->annotation) && punctuation(p,';',";");
    ++p->cursor;
    if (spelled(p,peek(p),"uniform")) {r->kind=DOE_COMPILER_UNIFORM_BUFFER;r->access=DOE_COMPILER_READ;++p->cursor;}
    else {
        r->kind=DOE_COMPILER_STORAGE_BUFFER;
        if (!word(p,"storage")) return false;
        r->access=DOE_COMPILER_READ;
        if (peek(p)->kind==',') {
            ++p->cursor;
            if (spelled(p,peek(p),"read")) r->access=DOE_COMPILER_READ;
            else if (spelled(p,peek(p),"read_write")) r->access=DOE_COMPILER_READ_WRITE;
            else return reject(p,"read or read_write storage access");
            ++p->cursor;
        }
    }
    return template_close(p) && name_span(p,&r->name) && punctuation(p,':',":") &&
        type_reference(p,s,&r->annotation) && punctuation(p,';',";");
}
static bool workgroup_declaration(Parser *p, Syntax *s) {
    const uint32_t offset=peek(p)->offset;
    WorkgroupRoot root={0};
    if (!word(p,"var")) return false;
    if (peek(p)->kind!='<')
        return source_error(p->source,DOE_COMPILER_UNSUPPORTED,offset,"private module variables are not supported");
    ++p->cursor;
    if (!spelled(p,peek(p),"workgroup"))
        return source_error(p->source,DOE_COMPILER_UNSUPPORTED,offset,"unsupported module variable address space");
    ++p->cursor;
    if (peek(p)->kind==',')
        return source_error(p->source,DOE_COMPILER_SEMANTIC_ERROR,peek(p)->offset,"workgroup variables cannot specify an access mode");
    if (!template_close(p) || !name_span(p,&root.name) || !punctuation(p,':',":") ||
        !type_reference(p,s,&root.annotation)) return false;
    if (peek(p)->kind=='=')
        return source_error(p->source,DOE_COMPILER_SEMANTIC_ERROR,peek(p)->offset,"workgroup variables cannot have an initializer");
    if (!punctuation(p,';',";")) return false;
    if (s->workgroup_count==s->limits.tokens/8u)
        return source_pool_error(p->source,DOE_COMPILER_POOL_TOKENS,8u*((size_t)s->workgroup_count+1u),offset,"workgroup declaration token capacity exceeded");
    s->workgroups[s->workgroup_count++]=root;
    return true;
}
static bool expression_node(Parser *p, Syntax *s, const Expression *node, bool charged, uint32_t *id) {
    if (node->depth > p->source->limits->expression_depth)
        return source_pool_error(p->source, DOE_COMPILER_POOL_EXPRESSION_DEPTH, node->depth, node->source_offset, "expression depth capacity exceeded");
    if (s->expression_count == s->limits.physical_expressions ||
        (charged && s->expression_budget_count == p->source->limits->expressions))
        return source_pool_error(p->source, DOE_COMPILER_POOL_EXPRESSIONS, (size_t)s->expression_budget_count+1u, node->source_offset, "expression node capacity exceeded");
    *id = (uint32_t)s->expression_count;
    s->expressions[s->expression_count++] = *node;
    if (charged) ++s->expression_budget_count;
    return true;
}
static bool expression(Parser *p, Syntax *s, unsigned minimum, unsigned nesting, uint32_t *id);
/* Recognize only name<single_type>(...), without reserving syntax or changing
 * tokens. Each candidate examines at most three tokens per allowed type depth
 * plus one over-depth level, so the existing parser can diagnose that boundary.
 * Commas, value operators and comparison delimiters return to ordinary parsing.
 * No recursive lookahead or speculative type/constant graph is constructed. */
static bool single_type_call(const Parser *p, size_t cursor) {
    if (cursor >= p->tokens->token_count || p->tokens->tokens[cursor].kind != '<') return false;
    const size_t maximum_depth = (size_t)p->source->limits->type_depth + 1u;
    const size_t maximum_tokens = 3u * maximum_depth + 2u;
    size_t depth = 1u;
    bool need_name = true;
    for (size_t examined = 0u; examined < maximum_tokens && ++cursor < p->tokens->token_count; ++examined) {
        const unsigned kind = p->tokens->tokens[cursor].kind;
        if (need_name) {
            if (kind != TOKEN_WORD) return false;
            need_name = false;
        } else if (kind == '<') {
            if (++depth > maximum_depth) return false;
            need_name = true;
        } else if (kind == '>' || kind == TOKEN_SHIFT_RIGHT) {
            const size_t closed = kind == '>' ? 1u : 2u;
            if (closed > depth) return false;
            depth -= closed;
            if (depth == 0u)
                return cursor + 1u < p->tokens->token_count && p->tokens->tokens[cursor + 1u].kind == '(';
        } else return false;
    }
    return false;
}

static bool primary(Parser *p,Syntax *s,unsigned nesting,uint32_t *id) {
    if (nesting>=p->source->limits->expression_depth || p->active_expression>p->source->limits->expression_depth)
        return source_pool_error(p->source,DOE_COMPILER_POOL_EXPRESSION_DEPTH,(size_t)p->active_expression+1u,peek(p)->offset,"expression depth capacity exceeded");
    Expression node={.source_offset=peek(p)->offset,.depth=1u,.left=NO_ID,.right=NO_ID};
    bool stored=false;
    if (peek(p)->kind=='(') {
        ++p->cursor; const unsigned saved=p->template_count; p->template_count=0u;
        const bool ok=expression(p,s,0u,nesting+1u,id) && punctuation(p,')',")");
        p->template_count=saved; if (!ok) return false;
        if (s->expressions[*id].depth>=p->source->limits->expression_depth)
            return source_pool_error(p->source,DOE_COMPILER_POOL_EXPRESSION_DEPTH,(size_t)s->expressions[*id].depth+1u,node.source_offset,"expression depth capacity exceeded");
        ++s->expressions[*id].depth;s->expressions[*id].grouped=true;stored=true;
    } else if (peek(p)->kind=='!' || peek(p)->kind=='-' || peek(p)->kind=='~') {
        node.kind=peek(p)->kind=='!'?EXPRESSION_NOT:peek(p)->kind=='-'?EXPRESSION_NEGATE:EXPRESSION_COMPLEMENT;
        ++p->cursor; ++p->active_expression;
        const bool ok=primary(p,s,nesting+1u,&node.left); --p->active_expression; if (!ok) return false;
        node.depth=1u+s->expressions[node.left].depth;
    } else if (peek(p)->kind==TOKEN_NUMBER || peek(p)->kind==TOKEN_DECIMAL) {
        node.kind=EXPRESSION_LITERAL;node.value.name=(NameSpan){peek(p)->offset,peek(p)->length};++p->cursor;
    } else if (spelled(p,peek(p),"true") || spelled(p,peek(p),"false")) {
        node.kind=EXPRESSION_BOOLEAN;node.value.integer=spelled(p,peek(p),"true")?1u:0u;++p->cursor;
    } else {
        if (peek(p)->kind=='&' || peek(p)->kind=='*')
            return source_error(p->source,DOE_COMPILER_UNSUPPORTED,peek(p)->offset,"pointer expressions are outside the typed owner");
        NameSpan name;
        if (!name_span(p,&name)) return false;
        node.kind=EXPRESSION_NAME;node.value.name=name;
        TypeRef annotation={NO_ID,NO_ID};
        const bool generator=source_span_is(p->source,name,"array") || source_span_is(p->source,name,"vec2") ||
            source_span_is(p->source,name,"vec3") || source_span_is(p->source,name,"vec4");
        if (peek(p)->kind=='<' && (generator || source_span_is(p->source,name,"bitcast") || single_type_call(p, p->cursor))) {
            if (source_span_is(p->source,name,"bitcast")) {
                ++p->cursor;if (!type_reference(p,s,&annotation) || !template_close(p)) return false;
            } else if (!generic_reference(p,s,name,&annotation)) return false;
        }
        if (peek(p)->kind=='(') {
            node.kind=EXPRESSION_CALL;node.value.call.callee=name;node.value.call.type=annotation;
            ++p->cursor;node.right=0u;uint32_t tail=NO_ID;
            while (peek(p)->kind!=')') {
                uint32_t argument;
                if (!expression(p,s,0u,nesting+1u,&argument)) return false;
                if (s->argument_count==s->limits.expressions)
                    return source_pool_error(p->source,DOE_COMPILER_POOL_EXPRESSIONS,(size_t)s->argument_count+1u,node.source_offset,"call argument capacity exceeded");
                const uint32_t link=s->argument_count++;s->arguments[link]=(ArgumentSyntax){argument,NO_ID};
                if (tail==NO_ID) node.left=link;else s->arguments[tail].next=link;
                tail=link;++node.right;
                if (node.depth<=s->expressions[argument].depth) node.depth=1u+s->expressions[argument].depth;
                if (peek(p)->kind!=',') break;
                ++p->cursor;
            }
            if (!punctuation(p,')',")")) return false;
        } else if (annotation.offset!=NO_ID) return reject(p,"type constructor argument list");
    }
    while (peek(p)->kind=='.' || peek(p)->kind=='[') {
        Expression next={.source_offset=stored?s->expressions[*id].source_offset:node.source_offset,.depth=1u,.left=NO_ID,.right=NO_ID};
        const bool root=!stored && node.kind==EXPRESSION_NAME;
        if (!root) {
            if (!stored && !expression_node(p,s,&node,node.source_offset!=p->exempt_literal,id)) return false;
            next.left=*id;next.depth=1u+s->expressions[*id].depth;
        }
        if (peek(p)->kind=='.') {
            next.kind=EXPRESSION_MEMBER;if (root) next.value.member.object=node.value.name;
            ++p->cursor;if (!name_span(p,&next.value.member.member)) return false;
        } else {
            next.kind=EXPRESSION_LOAD;if (root) next.value.reference.storage=node.value.name;
            ++p->cursor;
            if (!expression(p,s,0u,nesting+1u,&next.value.reference.index) || !punctuation(p,']',"]")) return false;
            if (next.depth<=s->expressions[next.value.reference.index].depth) next.depth=1u+s->expressions[next.value.reference.index].depth;
        }
        node=next;stored=false;
    }
    return stored || expression_node(p,s,&node,node.source_offset!=p->exempt_literal,id);
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
static bool expression_inner(Parser *p, Syntax *s, unsigned minimum, unsigned nesting, uint32_t *id) {
    if (!primary(p, s, nesting, id)) return false;
    for (;;) {
        ExpressionKind kind = EXPRESSION_ADD;
        Comparison comparison = COMPARE_EQUAL;
        if (p->template_count!=0u && (peek(p)->kind=='>' || peek(p)->kind==TOKEN_SHIFT_RIGHT || peek(p)->kind==TOKEN_GREATER_EQUAL)) return true;
        const unsigned precedence = binary_precedence(peek(p)->kind, &kind, &comparison);
        if (precedence == 0u || precedence < minimum) return true;
        const Expression *left = &s->expressions[*id];
        const bool shift = kind == EXPRESSION_SHIFT_LEFT || kind == EXPRESSION_SHIFT_RIGHT;
        const bool bit = precedence == 1u;
        if ((!left->grouped && ((kind == EXPRESSION_COMPARE && left->kind == EXPRESSION_COMPARE) ||
            ((kind == EXPRESSION_AND || kind == EXPRESSION_OR) &&
             (left->kind == EXPRESSION_AND || left->kind == EXPRESSION_OR) && kind != left->kind))) ||
            (shift && !unary_form(left)) || (bit && !unary_form(left) && left->kind != kind))
            return reject(p, "WGSL parentheses around incompatible operator groups");
        Expression node = { .source_offset = peek(p)->offset, .kind = kind, .left = *id };
        node.value.comparison = comparison;
        const bool exempt = kind == EXPRESSION_COMPARE && node.source_offset == p->exempt_comparison;
        ++p->cursor;
        if (!expression(p, s, precedence + 1u, nesting + (exempt ? 0u : 1u), &node.right)) return false;
        const Expression *right = &s->expressions[node.right];
        if ((shift || bit) && !unary_form(right))
            return reject(p, "unary operands or parentheses for shift/bitwise expression");
        node.depth = (exempt ? 0u : 1u) + (left->depth > right->depth ? left->depth : right->depth);
        if (!expression_node(p, s, &node, !exempt, id)) return false;
    }
}
static bool expression(Parser *p,Syntax *s,unsigned minimum,unsigned nesting,uint32_t *id) {
    if (p->active_expression>=p->source->limits->expression_depth)
        return source_pool_error(p->source,DOE_COMPILER_POOL_EXPRESSION_DEPTH,(size_t)p->active_expression+1u,peek(p)->offset,"expression depth capacity exceeded");
    ++p->active_expression;
    const bool ok=expression_inner(p,s,minimum,nesting,id);
    --p->active_expression;return ok;
}
static bool local_declaration(Parser *p,Syntax *s,LocalSyntax *l,unsigned terminator) {
    l->annotation=(TypeRef){NO_ID,NO_ID};l->expression=NO_ID;
    if (spelled(p,peek(p),"var")) l->flags|=LOCAL_MUTABLE;
    if (spelled(p,peek(p),"const")) l->flags|=LOCAL_CONSTANT;
    ++p->cursor;
    if ((l->flags&LOCAL_MUTABLE)!=0u && peek(p)->kind=='<') {
        ++p->cursor;if (!word(p,"function") || !template_close(p)) return false;
    }
    if (!name_span(p,&l->name)) return false;
    if (peek(p)->kind==':') {++p->cursor;l->flags|=LOCAL_ANNOTATED;if (!type_reference(p,s,&l->annotation)) return false;}
    if (peek(p)->kind=='=') {++p->cursor;l->flags|=LOCAL_INITIALIZED;if (!expression(p,s,0u,0u,&l->expression)) return false;}
    else if ((l->flags&(LOCAL_MUTABLE|LOCAL_ANNOTATED))!=(LOCAL_MUTABLE|LOCAL_ANNOTATED)) return reject(p,"local initializer or explicit var type");
    return terminator==0u || punctuation(p,terminator,";");
}
static bool condition(Parser *p, Syntax *s, uint32_t *id) {
    /* Preserve only the old accounting exemption, never a second grammar. */
    uint32_t comparison_offset = UINT32_MAX;
    if ((p->source->limits->flags & LIMIT_LEGACY) != 0u && peek(p)->kind == '(') {
        unsigned depth = 0u, brackets = 0u, comparisons = 0u;
        size_t end = p->cursor;
        for (; end < p->tokens->token_count; ++end) {
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
        if (comparisons != 1u || end + 1u >= p->tokens->token_count ||
            p->tokens->tokens[end + 1u].kind != '{') comparison_offset = UINT32_MAX;
    }
    if (comparison_offset == UINT32_MAX) return expression(p, s, 0u, 0u, id);
    ++p->cursor;
    p->exempt_comparison = comparison_offset;
    const bool ok = expression(p, s, 0u, 0u, id) && punctuation(p, ')', ")");
    p->exempt_comparison = UINT32_MAX;
    return ok;
}
static bool assignment(Parser *p,Syntax *s,Assignment *out,unsigned terminator) {
    *out=(Assignment){.path=NO_ID,.index=NO_ID,.expression=NO_ID};
    if (!name_span(p,&out->root)) return false;
    Expression previous={0};bool has_previous=false;
    while (peek(p)->kind=='.' || peek(p)->kind=='[') {
        if (has_previous && !expression_node(p,s,&previous,true,&out->path)) return false;
        previous=(Expression){.source_offset=out->root.offset,.depth=1u,.left=out->path,.right=NO_ID};
        if (out->path!=NO_ID) previous.depth=1u+s->expressions[out->path].depth;
        if (peek(p)->kind=='.') {
            previous.kind=EXPRESSION_MEMBER;
            if (out->path==NO_ID) previous.value.member.object=out->root;
            ++p->cursor;if (!name_span(p,&previous.value.member.member)) return false;
            out->kind=TARGET_MEMBER;out->index=previous.value.member.member.offset;
        } else {
            previous.kind=EXPRESSION_LOAD;if (out->path==NO_ID) previous.value.reference.storage=out->root;
            ++p->cursor;if (!expression(p,s,0u,0u,&previous.value.reference.index) || !punctuation(p,']',"]")) return false;
            if (previous.depth<=s->expressions[previous.value.reference.index].depth) previous.depth=1u+s->expressions[previous.value.reference.index].depth;
            out->kind=TARGET_INDEX;out->index=previous.value.reference.index;
        }
        has_previous=true;
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
    if (s->statement_count == s->limits.statements)
        return source_pool_error(p->source, DOE_COMPILER_POOL_STATEMENTS, (size_t)s->statement_count+1u, peek(p)->offset, "statement capacity exceeded");
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
    } else if (peek(p)->kind == TOKEN_WORD && p->cursor + 1u < p->tokens->token_count &&
               (p->tokens->tokens[p->cursor + 1u].kind == '(' ||
                (spelled(p, peek(p), "bitcast") && p->tokens->tokens[p->cursor + 1u].kind == '<') ||
                single_type_call(p, p->cursor + 1u))) {
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
            return source_pool_error(p->source, DOE_COMPILER_POOL_CONTROL_DEPTH, (size_t)depth+1u, token->offset, "control depth capacity exceeded");
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
        return source_pool_error(p->source, DOE_COMPILER_POOL_CONTROL_DEPTH, (size_t)p->source->limits->control_depth+1u, peek(p)->offset,
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
    if (peek(p)->kind != '@' || p->cursor + 1u >= p->tokens->token_count) return false;
    const Token *attribute = &p->tokens->tokens[p->cursor + 1u];
    return spelled(p, attribute, "group") || spelled(p, attribute, "binding");
}

/* Recognize only the retained unannotated leading alias grammar. Other lets
 * belong to the general expression/statement budgets, without speculative parse. */
static bool starts_legacy_alias(const Parser *p, const Syntax *s) {
    if (p->tokens->token_count - p->cursor < 7u) return false;
    const Token *t = p->tokens->tokens + p->cursor;
    bool global = false;
    for (size_t i = 0; i < s->entry_input_count; ++i)
        if (s->entry_inputs[i].kind == INPUT_GLOBAL_INVOCATION &&
            t[3].length == s->entry_inputs[i].name.length && memcmp(p->source->source+t[3].offset,p->source->source+s->entry_inputs[i].name.offset,t[3].length)==0) global = true;
    return global && t[0].kind == TOKEN_WORD && spelled(p, &t[0], "let") &&
           t[1].kind == TOKEN_WORD && t[2].kind == '=' && t[3].kind == TOKEN_WORD &&
           t[4].kind == '.' && t[5].kind == TOKEN_WORD && spelled(p, &t[5], "x") &&
           t[6].kind == ';';
}

static bool entry_parameter(Parser *p, EntryInput *parameter) {
    if (!punctuation(p, '@', "@builtin") || !word(p, "builtin") ||
        !punctuation(p, '(', "(")) return false;
    const Token *builtin = peek(p);
    if (spelled(p, builtin, "global_invocation_id")) parameter->kind = INPUT_GLOBAL_INVOCATION;
    else if (spelled(p, builtin, "local_invocation_id")) parameter->kind = INPUT_LOCAL_INVOCATION;
    else if (spelled(p, builtin, "workgroup_id")) parameter->kind = INPUT_WORKGROUP;
    else if (spelled(p, builtin, "num_workgroups")) parameter->kind = INPUT_NUM_WORKGROUPS;
    else if (spelled(p, builtin, "local_invocation_index")) parameter->kind = INPUT_LOCAL_INDEX;
    else if (spelled(p, builtin, "subgroup_size")) parameter->kind = INPUT_SUBGROUP_SIZE;
    else if (spelled(p, builtin, "subgroup_invocation_id")) parameter->kind = INPUT_SUBGROUP_INVOCATION;
    else if (spelled(p, builtin, "num_subgroups")) parameter->kind = INPUT_NUM_SUBGROUPS;
    else if (spelled(p, builtin, "subgroup_id")) parameter->kind = INPUT_SUBGROUP_ID;
    else return source_error(p->source, DOE_COMPILER_UNSUPPORTED, builtin->offset,
                             "unsupported compute input builtin");
    if (input_subgroup(parameter->kind) && (p->tokens->module_flags & MODULE_SUBGROUPS) == 0u)
        return source_error(p->source, DOE_COMPILER_SEMANTIC_ERROR, builtin->offset,
                            "subgroup inputs require enable subgroups");
    ++p->cursor;
    if (!punctuation(p, ')', ")") || !name_span(p, &parameter->name) ||
        !punctuation(p, ':', ":")) return false;
    if (input_scalar(parameter->kind)) return word(p, "u32");
    if (spelled(p, peek(p), "vec3u")) {
        parameter->flags = 1u;
        ++p->cursor;
        return true;
    }
    return word(p, "vec3") && punctuation(p, '<', "<u32>") &&
           word(p, "u32") && punctuation(p, '>', ">");
}
static bool entry_parameters(Parser *p, Syntax *s) {
    if (!punctuation(p, '(', "(")) return false;
    while (peek(p)->kind != ')') {
        if (s->entry_input_count == s->limits.entry_inputs)
            return source_pool_error(p->source, DOE_COMPILER_POOL_ENTRY_INPUTS,
                (size_t)s->entry_input_count+1u, peek(p)->offset, "entry input capacity exceeded");
        EntryInput *input=&s->entry_inputs[s->entry_input_count];
        memset(input,0,sizeof(*input));
        if (!entry_parameter(p, input)) return false;
        ++s->entry_input_count;
        ++s->entries[s->entry_count].input_count;
        if (peek(p)->kind != ',') break;
        ++p->cursor;
    }
    return punctuation(p, ')', ")");
}

static bool module_constant(Parser *p, Syntax *s, bool first) {
    if (s->constant_decl_count == s->limits.expressions + 1u)
        return source_pool_error(p->source, DOE_COMPILER_POOL_EXPRESSIONS, (size_t)s->constant_decl_count, peek(p)->offset,
                            "module constant capacity exceeded");
    /* Exact retained declaration: const NAME : u32 = DECIMAL_U ; */
    if (first && p->tokens->token_count - p->cursor >= 7u) {
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
    ConstantSyntax *constant = &s->constant_decls[s->constant_decl_count];
    memset(constant, 0, sizeof(*constant));
    constant->first_expression = (uint32_t)s->expression_count;
    if (!local_declaration(p, s, &local, ';')) return false;
    p->exempt_literal = UINT32_MAX;
    constant->name = local.name;
    constant->annotation = local.annotation;
    constant->expression = local.expression;
    ++s->constant_decl_count;
    return true;
}
static bool function_declaration(Parser *p, Syntax *s) {
    if (s->function_count == s->limits.tokens / 6u)
        return source_pool_error(p->source, DOE_COMPILER_POOL_TOKENS, 6u*((size_t)s->function_count+1u), peek(p)->offset,
                            "function declaration capacity exceeded");
    FunctionRecord *function = &s->functions[s->function_count];
    memset(function, 0, sizeof(*function));
    function->result_type = IR_VOID;
    function->annotation=(TypeRef){NO_ID,NO_ID};
    if (peek(p)->kind == '@' && p->cursor + 1u < p->tokens->token_count &&
        spelled(p, &p->tokens->tokens[p->cursor + 1u], "must_use")) {
        function->flags = FUNCTION_MUST_USE;
        p->cursor += 2u;
    }
    const bool entry = peek(p)->kind == '@';
    if (entry) {
        if (s->entry_count == s->limits.entries) {
            if ((s->limits.flags&LIMIT_SINGULAR)==0u)
                return source_pool_error(p->source,DOE_COMPILER_POOL_ENTRIES,
                    (size_t)s->entry_count+1u,peek(p)->offset,"entry capacity exceeded");
            /* A singular adapter can reject at the second declaration name
             * without storing or accepting its unrepresentable remainder. */
            while (peek(p)->kind!=TOKEN_END && !spelled(p,peek(p),"fn")) ++p->cursor;
            if (!word(p,"fn") || !name_span(p,&function->name)) return false;
            return source_error(p->source,DOE_COMPILER_UNSUPPORTED,function->name.offset,
                "multiple compute entries cannot use a singular reflection projection");
        }
        EntryRecord *record=&s->entries[s->entry_count];
        memset(record,0,sizeof(*record));
        record->function=s->function_count;
        record->input_first=s->entry_input_count;
        if (!entry_attributes(p, s)) return false;
        function->flags |= FUNCTION_ENTRY;
    }
    if (!word(p, "fn") || !name_span(p, &function->name)) return false;
    if (entry) function->entry_index=s->entry_count;
    else function->formal_start = (uint32_t)s->formal_count;
    if (entry) {
        if (!entry_parameters(p, s)) return false;
    } else {
        if (!punctuation(p, '(', "(")) return false;
        while (peek(p)->kind != ')') {
            FormalRecord formal;
            if (!name_span(p, &formal.name) || !punctuation(p, ':', ":") ||
                !type_reference(p,s,&formal.type.annotation)) return false;
            if (s->formal_count == s->limits.tokens / 4u)
                return source_pool_error(p->source, DOE_COMPILER_POOL_TOKENS, 4u*((size_t)s->formal_count+1u), formal.name.offset,
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
        if (!type_reference(p,s,&function->annotation)) return false;
    }
    if (!punctuation(p, '{', "{")) return false;
    function->statement_start = (uint32_t)s->statement_count;
    function->expression_start = (uint32_t)s->expression_count;
    if (entry && (p->source->limits->flags & LIMIT_LEGACY) != 0u && starts_legacy_alias(p, s)) {
        ++p->cursor;

        if (!name_span(p, &s->alias) || !punctuation(p, '=', "=") ||
            !name_span(p, &s->alias_source) || !punctuation(p, '.', ".") ||
            !word(p, "x") || !punctuation(p, ';', ";")) return false;
    }
    if (!block(p, s, 0u)) return false;
    function->statement_end = (uint32_t)s->statement_count;
    function->expression_end = (uint32_t)s->expression_count;
    ++s->function_count;
    if (entry) ++s->entry_count;
    return true;
}
/* Directives precede declarations and never consume a source declaration pool. */
static bool module_directive(Parser *p, bool enable) {
    ++p->cursor;
    do {
        const Token *extension = peek(p);
        if (extension->kind != TOKEN_WORD) return reject(p, "extension name");
        if (enable && spelled(p, extension, "subgroups"))
            p->tokens->module_flags |= MODULE_SUBGROUPS;
        else if (!enable && spelled(p, extension, "subgroup_uniformity")) {
            /* This implementation analyzes both workgroup and subgroup scopes. */
        } else return source_error(p->source, DOE_COMPILER_UNSUPPORTED, extension->offset,
                                    "unsupported enable or language extension");
        ++p->cursor;
        if (peek(p)->kind != ',') break;
        ++p->cursor;
    } while (peek(p)->kind != ';');
    return punctuation(p, ';', ";");
}
bool parse_tokens(Source *source,CompilerWorkspace *s) {
    Parser p={.source=source,.tokens=s,.exempt_comparison=NO_ID,.exempt_literal=NO_ID};
    bool first=true;
    while (peek(&p)->kind!=TOKEN_END) {
        if (peek(&p)->kind==';') {++p.cursor;continue;}
        if (spelled(&p, peek(&p), "enable") || spelled(&p, peek(&p), "requires")) {
            if (!first) return source_error(source, DOE_COMPILER_PARSE_ERROR, peek(&p)->offset,
                                           "directives must precede declarations");
            if (!module_directive(&p, spelled(&p, peek(&p), "enable"))) return false;
            continue;
        }
        if (spelled(&p,peek(&p),"const")) {if (!module_constant(&p,s,first)) return false;}
        else if (spelled(&p,peek(&p),"struct")) {if (!struct_declaration(&p,s)) return false;}
        else if (spelled(&p,peek(&p),"var")) {if (!workgroup_declaration(&p,s)) return false;}
        else if (starts_storage(&p)) {
            if (s->resource_count==s->limits.resources)
                return source_pool_error(source,DOE_COMPILER_POOL_RESOURCES,(size_t)s->resource_count+1u,peek(&p)->offset,"resource capacity exceeded");
            if (!resource_declaration(&p,s,&s->resources[s->resource_count])) return false;
            ++s->resource_count;
        } else if (!function_declaration(&p,s)) return false;
        first=false;
    }
    return s->entry_count!=0u || reject(&p,"one compute entry point");
}
