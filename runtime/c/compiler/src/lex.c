/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "internal.h"
#include <string.h>

static bool is_letter(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static bool is_digit(unsigned char c) { return c >= '0' && c <= '9'; }
static bool is_space(unsigned char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

bool lex_source(Source *source, CompilerWorkspace *tokens) {
    size_t cursor = 0;
    tokens->token_count = 0;
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
        if (tokens->token_count == source->limits->tokens)
            return source_pool_error(source, DOE_COMPILER_POOL_TOKENS, (size_t)tokens->token_count+1u, cursor, "token capacity exceeded");
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
        } else if (is_digit(c) || (c == '.' && cursor + 1u < source->length &&
                                      is_digit((unsigned char)source->source[cursor + 1u]))) {
            token.kind = TOKEN_NUMBER;
            const bool hex = c == '0' && cursor + 1u < source->length &&
                (source->source[cursor + 1u] == 'x' || source->source[cursor + 1u] == 'X');
            ++cursor;
            while (cursor < source->length) {
                const unsigned char next = (unsigned char)source->source[cursor];
                const char previous = source->source[cursor - 1u];
                const bool exponent = hex ? (previous == 'p' || previous == 'P') :
                                            (previous == 'e' || previous == 'E');
                if (!is_letter(next) && !is_digit(next) && next != '.' &&
                    !((next == '+' || next == '-') && exponent)) break;
                ++cursor;
            }
            token.length = (uint32_t)(cursor - token.offset);
        } else {
            const unsigned char next = cursor + 1u < source->length ?
                (unsigned char)source->source[cursor + 1u] : 0u;
            if (c == '-' && next == '>') { token.kind = TOKEN_ARROW; token.length = 2u;
            } else if ((c == '<' || c == '>') && next == c) {
                token.kind = c == '<' ? TOKEN_SHIFT_LEFT : TOKEN_SHIFT_RIGHT;
                token.length = 2u;
                if (cursor + 2u < source->length && source->source[cursor + 2u] == '=') {
                    token.kind = c == '<' ? TOKEN_SHIFT_LEFT_ASSIGN : TOKEN_SHIFT_RIGHT_ASSIGN;
                    token.length = 3u;
                }
            } else if ((c == '&' || c == '|') && next == c) {
                token.kind = c == '&' ? TOKEN_AND : TOKEN_OR; token.length = 2u;
            } else if ((c == '+' || c == '-') && next == c) {
                token.kind = c == '+' ? TOKEN_INCREMENT : TOKEN_DECREMENT; token.length = 2u;
            } else if (next == '=' && strchr("=!<>+-*/%&|^", (int)c) != NULL) {
                switch (c) {
                    case '=': token.kind = TOKEN_EQUAL; break;
                    case '!': token.kind = TOKEN_NOT_EQUAL; break;
                    case '<': token.kind = TOKEN_LESS_EQUAL; break;
                    case '>': token.kind = TOKEN_GREATER_EQUAL; break;
                    case '+': token.kind = TOKEN_ADD_ASSIGN; break;
                    case '-': token.kind = TOKEN_SUB_ASSIGN; break;
                    case '*': token.kind = TOKEN_MUL_ASSIGN; break;
                    case '/': token.kind = TOKEN_DIV_ASSIGN; break;
                    case '%': token.kind = TOKEN_REM_ASSIGN; break;
                    case '&': token.kind = TOKEN_BIT_AND_ASSIGN; break;
                    case '|': token.kind = TOKEN_BIT_OR_ASSIGN; break;
                    case '^': token.kind = TOKEN_BIT_XOR_ASSIGN; break;
                    default: break;
                }
                token.length = 2u;
            } else if (strchr("@()<>:,;{}[].=+-*!/%&|^~", (int)c) == NULL) {
                return source_error(source, DOE_COMPILER_UNSUPPORTED, cursor,
                                    "unsupported character in ASCII WGSL envelope");
            }
            cursor += token.length;
        }
        tokens->tokens[tokens->token_count++] = token;
    }
    tokens->tokens[tokens->token_count] = (Token){ .kind = TOKEN_END, .offset = (uint32_t)source->length };
    return true;
}
