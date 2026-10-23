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
        if (tokens->count == DOE_COMPILER_MAX_TOKENS)
            return source_error(source, DOE_COMPILER_INPUT_ERROR, cursor, "input exceeds 2048 tokens");
        Token token = { .kind = c, .offset = cursor, .length = 1u };
        if (is_letter(c) || is_digit(c)) {
            token.kind = is_letter(c) ? TOKEN_WORD : TOKEN_NUMBER;
            ++cursor;
            while (cursor < source->length) {
                const unsigned char next = (unsigned char)source->source[cursor];
                if (!is_letter(next) && !is_digit(next)) break;
                ++cursor;
            }
            token.length = cursor - token.offset;
        } else {
            if (strchr("@()<>:,;{}[].=+", (int)c) == NULL)
                return source_error(source, DOE_COMPILER_UNSUPPORTED, cursor,
                                    "unsupported character or operator in bounded WGSL grammar");
            ++cursor;
        }
        tokens->tokens[tokens->count++] = token;
    }
    tokens->tokens[tokens->count] = (Token){ .kind = TOKEN_END, .offset = source->length };
    return true;
}
