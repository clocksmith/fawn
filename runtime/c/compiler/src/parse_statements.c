/* SPDX-License-Identifier: MIT
 * Structured statement grammar over the canonical parser and syntax pools. */
#include "parse_internal.h"
#include <string.h>

static bool statement_slot(Parser *p, Syntax *s, uint32_t *id) {
    if (s->statement_count == s->limits.statements)
        return source_pool_error(p->source, DOE_COMPILER_POOL_STATEMENTS, (size_t)s->statement_count+1u, parser_peek(p)->offset, "statement capacity exceeded");
    *id = (uint32_t)s->statement_count++;
    Statement *statement = &s->statements[*id];
    memset(statement, 0, sizeof(*statement));
    statement->source_offset = parser_peek(p)->offset;
    statement->initializer = UINT32_MAX;
    statement->update = UINT32_MAX;
    return true;
}
static bool simple_statement(Parser *p, Syntax *s, bool declaration, unsigned terminator, uint32_t *id) {
    if (!statement_slot(p, s, id)) return false;
    Statement *st = &s->statements[*id];
    if (declaration && (parser_spelled(p, parser_peek(p), "let") || parser_spelled(p, parser_peek(p), "var") || parser_spelled(p, parser_peek(p), "const"))) {
        st->kind = parser_spelled(p, parser_peek(p), "var") ? STATEMENT_VAR :
                   parser_spelled(p, parser_peek(p), "const") ? STATEMENT_CONST : STATEMENT_LET;
        if (!parser_local_declaration(p, s, &st->value.local, terminator)) return false;
    } else if (parser_peek(p)->kind == TOKEN_WORD && p->cursor + 1u < p->tokens->token_count &&
               (p->tokens->tokens[p->cursor + 1u].kind == '(' ||
                (parser_spelled(p, parser_peek(p), "bitcast") && p->tokens->tokens[p->cursor + 1u].kind == '<') ||
                parser_single_type_call(p, p->cursor + 1u))) {
        st->kind = STATEMENT_CALL;
        if (!parser_primary(p, s, 0u, &st->value.condition)) return false;
        if (s->expressions[st->value.condition].kind != EXPRESSION_CALL)
            return parser_reject(p, "function call statement");
        if (terminator != 0u && !parser_punctuation(p, terminator, ";")) return false;
    } else {
        st->kind = STATEMENT_ASSIGN;
        if (!parser_assignment(p, s, &st->value.assignment, terminator)) return false;
    }
    st->end = (uint32_t)s->statement_count;
    return true;
}
bool parser_block(Parser *p, Syntax *s, unsigned depth);
/* Each selector occupies an ordinary statement slot before its clause body.
 * Clause headers share body_start/end; walking by end visits each clause once. */
static bool switch_body(Parser *p, Syntax *s, unsigned depth) {
    if (!parser_punctuation(p, '{', "{")) return false;
    while (parser_peek(p)->kind != '}') {
        const bool standalone = parser_spelled(p, parser_peek(p), "default");
        if (!standalone && !parser_spelled(p, parser_peek(p), "case"))
            return parser_reject(p, "case or default clause");
        if (!standalone) ++p->cursor;
        const uint32_t first = (uint32_t)s->statement_count;
        for (;;) {
            uint32_t id;
            if (!statement_slot(p, s, &id)) return false;
            Statement *selector = &s->statements[id];
            selector->kind = STATEMENT_CASE;
            selector->value.condition = NO_ID;
            if (parser_spelled(p, parser_peek(p), "default")) ++p->cursor;
            else if (!expression(p, s, 0u, 0u, &selector->value.condition)) return false;
            if (standalone || parser_peek(p)->kind != ',') break;
            ++p->cursor;
            if (parser_peek(p)->kind == ':' || parser_peek(p)->kind == '{') break;
        }
        if (parser_peek(p)->kind == ':') ++p->cursor;
        if (!parser_punctuation(p, '{', "{")) return false;
        const uint32_t body = (uint32_t)s->statement_count;
        if (!parser_block(p, s, depth + 1u)) return false;
        for (uint32_t i = first; i < body; ++i) {
            s->statements[i].body_start = body;
            s->statements[i].end = (uint32_t)s->statement_count;
        }
    }
    return parser_punctuation(p, '}', "}");
}
static bool statement(Parser *p, Syntax *s, unsigned depth) {
    const Token *token = parser_peek(p);
    const bool is_if = parser_spelled(p, token, "if");
    const bool is_switch = parser_spelled(p, token, "switch");
    const bool is_for = parser_spelled(p, token, "for");
    const bool is_while = parser_spelled(p, token, "while");
    const bool is_loop = parser_spelled(p, token, "loop");
    const bool is_block = token->kind == '{';
    const bool is_continuing = parser_spelled(p, token, "continuing");
    if (is_if || is_switch || is_for || is_while || is_loop || is_block) {
        if (depth >= p->source->limits->control_depth)
            return source_pool_error(p->source, DOE_COMPILER_POOL_CONTROL_DEPTH, (size_t)depth+1u, token->offset, "control depth capacity exceeded");
    }
    uint32_t id;
    if (parser_spelled(p, token, "let") || parser_spelled(p, token, "var") || parser_spelled(p, token, "const"))
        return simple_statement(p, s, true, ';', &id);
    if (!is_if && !is_switch && !is_for && !is_while && !is_loop && !is_block && !is_continuing &&
        !parser_spelled(p, token, "return") && !parser_spelled(p, token, "discard") && !((parser_spelled(p, token, "break") || parser_spelled(p, token, "continue") || token->kind == ';')))
        return simple_statement(p, s, false, ';', &id);
    if (!statement_slot(p, s, &id)) return false;
    Statement *st = &s->statements[id];
    ++p->cursor;
    if (is_if) {
        st->kind = STATEMENT_IF;
        if (!parser_condition(p, s, &st->value.condition) || !parser_punctuation(p, '{', "{")) return false;
        st->body_start = (uint32_t)s->statement_count;
        if (!parser_block(p, s, depth + 1u)) return false;
        st->else_start = (uint32_t)s->statement_count;
        if (parser_spelled(p, parser_peek(p), "else")) {
            ++p->cursor;
            if (parser_spelled(p, parser_peek(p), "if")) {
                if (!statement(p, s, depth + 1u)) return false;
            } else if (!parser_punctuation(p, '{', "{") || !parser_block(p, s, depth + 1u)) return false;
        }
    } else if (is_switch) {
        st->kind = STATEMENT_SWITCH;
        if (!expression(p, s, 0u, 0u, &st->value.condition)) return false;
        st->body_start = (uint32_t)s->statement_count;
        if (!switch_body(p, s, depth)) return false;
    } else if (is_for) {
        st->kind = STATEMENT_FOR;
        st->value.condition = UINT32_MAX;
        if (!parser_punctuation(p, '(', "(")) return false;
        if (parser_peek(p)->kind == ';') ++p->cursor;
        else if (!simple_statement(p, s, true, ';', &st->initializer)) return false;
        if (parser_peek(p)->kind != ';' && !expression(p, s, 0u, 0u, &st->value.condition)) return false;
        if (!parser_punctuation(p, ';', ";")) return false;
        if (parser_peek(p)->kind != ')' && !simple_statement(p, s, false, 0u, &st->update)) return false;
        if (!parser_punctuation(p, ')', ")") || !parser_punctuation(p, '{', "{")) return false;
        st->body_start = (uint32_t)s->statement_count;
        if (!parser_block(p, s, depth + 1u)) return false;
    } else if (is_while || is_loop || is_block || is_continuing) {
        st->kind = is_while ? STATEMENT_WHILE : is_loop ? STATEMENT_LOOP :
                   is_block ? STATEMENT_BLOCK : STATEMENT_CONTINUING;
        st->value.condition = UINT32_MAX;
        if (is_while && !expression(p, s, 0u, 0u, &st->value.condition)) return false;
        if (!is_block && !parser_punctuation(p, '{', "{")) return false;
        st->body_start = (uint32_t)s->statement_count;
        if (!parser_block(p, s, depth + (is_continuing ? 0u : 1u))) return false;
    } else if (parser_spelled(p, token, "return")) {
        st->kind = STATEMENT_RETURN;
        st->value.condition = UINT32_MAX;
        if (parser_peek(p)->kind != ';' && !expression(p, s, 0u, 0u, &st->value.condition)) return false;
        if (!parser_punctuation(p, ';', ";")) return false;
    } else if (parser_spelled(p, token, "break")) {
        st->kind = STATEMENT_BREAK;
        if (parser_spelled(p, parser_peek(p), "if")) {
            ++p->cursor;
            st->kind = STATEMENT_BREAK_IF;
            if (!expression(p, s, 0u, 0u, &st->value.condition)) return false;
        }
        if (!parser_punctuation(p, ';', ";")) return false;
    } else if (parser_spelled(p, token, "discard")) {
        st->kind = STATEMENT_DISCARD;
        if (!parser_punctuation(p, ';', ";")) return false;
    } else if (parser_spelled(p, token, "continue")) {
        st->kind = STATEMENT_CONTINUE;
        if (!parser_punctuation(p, ';', ";")) return false;
    } else st->kind = STATEMENT_EMPTY;
    st->end = (uint32_t)s->statement_count;
    return true;
}
bool parser_block(Parser *p, Syntax *s, unsigned depth) {
    /* Required loop/continuing braces may share one language depth, but even
     * malformed nested continuing blocks must have a bounded C call path. */
    if (p->block_nesting >= 2u * p->source->limits->control_depth + 1u)
        return source_pool_error(p->source, DOE_COMPILER_POOL_CONTROL_DEPTH, (size_t)p->source->limits->control_depth+1u, parser_peek(p)->offset,
                            "control recursion capacity exceeded");
    ++p->block_nesting;
    while (parser_peek(p)->kind != '}') {
        if (parser_peek(p)->kind == TOKEN_END) return parser_reject(p, "statement or }");
        if (!statement(p, s, depth)) return false;
    }
    --p->block_nesting;
    return parser_punctuation(p, '}', "}");
}
