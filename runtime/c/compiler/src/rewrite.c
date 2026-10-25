/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "rewrite.h"
#include "numeric.h"
#include <string.h>

static size_t token_at(const CompilerWorkspace *c, uint32_t offset) {
    size_t first = 0u, end = c->token_count;
    while (first < end) {
        const size_t middle = first + (end - first) / 2u;
        if (c->tokens[middle].offset < offset) first = middle + 1u;
        else end = middle;
    }
    return first;
}

static uint32_t member_start(const CompilerWorkspace *c, uint32_t id) {
    while (c->expressions[id].kind == EXPRESSION_MEMBER) {
        if (c->expressions[id].grouped) return NO_ID;
        if (c->expressions[id].left == NO_ID) return c->expressions[id].value.member.object.offset;
        id = c->expressions[id].left;
    }
    const Expression *root = &c->expressions[id];
    return root->kind == EXPRESSION_NAME && !root->grouped ? root->value.name.offset : NO_ID;
}

static bool candidate(Source *s, const Expression *e, WgslEdit *edit) {
    const CompilerWorkspace *c = s->compiler;
    if ((e->kind != EXPRESSION_DIVIDE && e->kind != EXPRESSION_REMAINDER) ||
        e->resolved_type != IR_U32 || c->expressions[e->left].resolved_type != IR_U32) return false;
    const uint32_t start = member_start(c, e->left);
    const Expression *right = &c->expressions[e->right];
    if (start == NO_ID || right->kind != EXPRESSION_LITERAL || right->grouped) return false;
    const NameSpan literal = right->value.name;
    if (literal.length < 2u || s->source[literal.offset + literal.length - 1u] != 'u') return false;
    NumericValue value;
    if (!numeric_parse_literal(s, literal, &value) || value.type != IR_U32 ||
        value.bits == 0u || (value.bits & (value.bits - 1u)) != 0u) return false;
    for (size_t i = token_at(c, start); c->tokens[i].offset < e->source_offset; ++i)
        if (c->tokens[i].kind != TOKEN_WORD && c->tokens[i].kind != '.') return false;
    for (size_t i = (size_t)e->source_offset + 1u; i < literal.offset; ++i)
        if (strchr(" \t\r\n", s->source[i]) == NULL) return false;
    uint32_t replacement = (uint32_t)value.bits - 1u;
    const bool division = e->kind == EXPRESSION_DIVIDE;
    if (division) {
        replacement = 0u;
        for (uint64_t n = value.bits; n > 1u; n >>= 1u) ++replacement;
    }
    *edit = (WgslEdit){start, e->source_offset, literal.offset + literal.length, replacement, division};
    return true;
}

static void sift(WgslEdit *edits, size_t count, size_t root) {
    while (root < count / 2u) {
        size_t child = 2u * root + 1u;
        if (child + 1u < count && edits[child].start < edits[child + 1u].start) ++child;
        if (edits[root].start >= edits[child].start) return;
        const WgslEdit swap = edits[root];
        edits[root] = edits[child];
        edits[child] = swap;
        root = child;
    }
}

static size_t decimal(uint32_t value, char *output) {
    char reversed[10];
    size_t length = 0u;
    do { reversed[length++] = (char)('0' + value % 10u); value /= 10u; } while (value != 0u);
    if (output != NULL)
        for (size_t i = 0u; i < length; ++i) output[i] = reversed[length - 1u - i];
    return length;
}

bool compiler_rewrite_plan(Source *s, WgslEdit *edits, size_t capacity,
                           size_t *count, size_t *output_length) {
    *count = 0u;
    *output_length = 0u;
    if (!lex_source(s, s->compiler)) return false;
    for (uint32_t i = 0u; i < s->compiler->expression_count; ++i) {
        WgslEdit edit;
        if (!candidate(s, &s->compiler->expressions[i], &edit)) continue;
        if (*count == capacity)
            return source_error(s, DOE_COMPILER_OUTPUT_TOO_SMALL, edit.start, "WGSL edit capacity exceeded");
        edits[(*count)++] = edit;
    }
    if (s->diagnostic->status != DOE_COMPILER_OK) return false;
    for (size_t i = *count / 2u; i != 0u; --i) sift(edits, *count, i - 1u);
    for (size_t i = *count; i > 1u; --i) {
        const WgslEdit swap = edits[0];
        edits[0] = edits[i - 1u];
        edits[i - 1u] = swap;
        sift(edits, i - 1u, 0u);
    }
    size_t length = s->length, end = 0u;
    for (size_t i = 0u; i < *count; ++i) {
        const WgslEdit *edit = &edits[i];
        if (edit->start < end)
            return source_error(s, DOE_COMPILER_INPUT_ERROR, edit->start, "overlapping WGSL edits");
        const size_t added = 1u + (edit->division ? 3u : 2u) + decimal(edit->replacement, NULL) + 2u;
        length -= edit->end - edit->operator_offset;
        if (length > SIZE_MAX - added)
            return source_error(s, DOE_COMPILER_OUTPUT_TOO_SMALL, edit->start, "WGSL output length overflow");
        length += added;
        end = edit->end;
    }
    *output_length = length;
    return true;
}

void compiler_rewrite_publish(const Source *s, const WgslEdit *edits, size_t count, char *output) {
    size_t read = 0u, written = 0u;
    for (size_t i = 0u; i < count; ++i) {
        const WgslEdit *edit = &edits[i];
        const size_t before = edit->start - read, left = edit->operator_offset - edit->start;
        memcpy(output + written, s->source + read, before);
        written += before;
        output[written++] = '(';
        memcpy(output + written, s->source + edit->start, left);
        written += left;
        const char *operation = edit->division ? ">> " : "& ";
        const size_t operation_length = edit->division ? 3u : 2u;
        memcpy(output + written, operation, operation_length);
        written += operation_length;
        written += decimal(edit->replacement, output + written);
        output[written++] = 'u';
        output[written++] = ')';
        read = edit->end;
    }
    memcpy(output + written, s->source + read, s->length - read);
}
