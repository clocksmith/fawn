/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "internal.h"
#include <string.h>

static bool is_letter(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static bool is_digit(unsigned char c) { return c >= '0' && c <= '9'; }
static bool is_space(unsigned char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

bool lex_source(Source *source, TokenList *tokens) {
    size_t cursor = 0;
    tokens->count = 0;
    while (cursor < source->length) {
        const unsigned char c = (unsigned char)source->source[cursor];
        if (is_space(c)) { ++cursor; continue; }
        if (c == '/' && cursor + 1u < source->length) {
            if (source->source[cursor + 1u] == '/') {
                cursor += 2u;
                while (cursor < source->length && source->source[cursor] != '\n' &&
                       source->source[cursor] != '\r') ++cursor;
                continue;
            }
            if (source->source[cursor + 1u] == '*') {
                const size_t start = cursor;
                size_t depth = 1u;
                cursor += 2u;
                while (cursor < source->length && depth != 0u) {
                    if (cursor + 1u < source->length && source->source[cursor] == '/' &&
                        source->source[cursor + 1u] == '*') { ++depth; cursor += 2u; }
                    else if (cursor + 1u < source->length && source->source[cursor] == '*' &&
                             source->source[cursor + 1u] == '/') { --depth; cursor += 2u; }
                    else ++cursor;
                }
                if (depth != 0u) return source_error(source, DOE_COMPILER_PARSE_ERROR, start,
                                                    "unterminated block comment");
                continue;
            }
        }
        if (tokens->count == tokens->capacity)
            return source_error(source, DOE_COMPILER_INPUT_ERROR, cursor, "token capacity exceeded");
        Token token = { .kind = c, .offset = (uint32_t)cursor, .length = 1u };
        if (is_letter(c)) {
            token.kind = TOKEN_WORD;
            ++cursor;
            while (cursor < source->length) {
                const unsigned char next = (unsigned char)source->source[cursor];
                if (!is_letter(next) && !is_digit(next)) break;
                ++cursor;
            }
            token.length = (uint32_t)(cursor - token.offset);
        } else if (is_digit(c)) {
            token.kind = TOKEN_NUMBER;
            ++cursor;
            while (cursor < source->length) {
                const unsigned char next = (unsigned char)source->source[cursor];
                const char previous = source->source[cursor - 1u];
                if (next == '.') token.kind = TOKEN_DECIMAL;
                else if (!is_letter(next) && !is_digit(next) &&
                         !((next == '+' || next == '-') &&
                           (previous == 'e' || previous == 'E' ||
                            previous == 'p' || previous == 'P'))) break;
                ++cursor;
            }
            token.length = (uint32_t)(cursor - token.offset);
        } else if (cursor + 1u < source->length &&
                   ((c == '&' && source->source[cursor + 1u] == '&') ||
                    (c == '|' && source->source[cursor + 1u] == '|') ||
                    ((c == '+' || c == '-' || c == '*') && source->source[cursor + 1u] == '=') ||
                    ((c == '+' || c == '-') && source->source[cursor + 1u] == (char)c))) {
            if (!source->limits->extended)
                return source_error(source, DOE_COMPILER_UNSUPPORTED, cursor, "operator requires job API");
            const char second = source->source[cursor + 1u];
            token.kind = c == '&' ? TOKEN_AND : c == '|' ? TOKEN_OR :
                second == '=' ? (c == '+' ? TOKEN_ADD_ASSIGN : c == '-' ? TOKEN_SUB_ASSIGN : TOKEN_MUL_ASSIGN) :
                c == '+' ? TOKEN_INCREMENT : TOKEN_DECREMENT;
            token.length = 2u;
            cursor += 2u;
        } else if (cursor + 1u < source->length && source->source[cursor + 1u] == '=' &&
                   (c == '=' || c == '!' || c == '<' || c == '>')) {
            token.kind = c == '=' ? TOKEN_EQUAL : c == '!' ? TOKEN_NOT_EQUAL :
                         c == '<' ? TOKEN_LESS_EQUAL : TOKEN_GREATER_EQUAL;
            token.length = 2u;
            cursor += 2u;
        } else {
            if (strchr(source->limits->extended ? "@()<>:,;{}[].=+-*!" : "@()<>:,;{}[].=+-*", (int)c) == NULL)
                return source_error(source, DOE_COMPILER_UNSUPPORTED, cursor,
                                    "unsupported character or operator in bounded WGSL grammar");
            ++cursor;
        }
        tokens->tokens[tokens->count++] = token;
    }
    tokens->tokens[tokens->count] = (Token){ .kind = TOKEN_END, .offset = (uint32_t)source->length };
    return true;
}
