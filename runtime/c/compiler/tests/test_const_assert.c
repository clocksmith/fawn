/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * WGSL const_assert at module and function scope: a true assertion leaves the
 * compiled module unchanged, every rejection names the assertion's location. */
#include "../include/doe_compiler.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char body[] =
    "@group(0) @binding(0) var<storage, read_write> values: array<u32>;\n"
    "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id: vec3u) {%s"
    " values[id.x] = values[id.x] + N; }";

static DoeCompilerStatus compile(const char *module, const char *statement, uint32_t *words,
                                 DoeCompilerResult *result, char *source, size_t capacity, int *length) {
    char tail[1024];
    const int tail_length = snprintf(tail, sizeof(tail), body, statement);
    assert(tail_length > 0 && (size_t)tail_length < sizeof(tail));
    *length = snprintf(source, capacity, "const N: u32 = 4u;\n%s\n%s", module, tail);
    assert(*length > 0 && (size_t)*length < capacity);
    return doe_compiler_compile(DOE_COMPILER_ABI_VERSION, source, (size_t)*length, words,
                                DOE_COMPILER_MAX_WORDS, result, sizeof(*result));
}

/* An assertion is compile-time only: the module equals the same source with
 * the assertion removed (plain_module/plain_statement), word for word. */
static void accepts(const char *module, const char *statement, const char *plain_module, const char *plain_statement) {
    char source[2048], plain_source[2048];
    int length = 0, plain_length = 0;
    uint32_t words[DOE_COMPILER_MAX_WORDS], plain[DOE_COMPILER_MAX_WORDS];
    DoeCompilerResult result, plain_result;
    const DoeCompilerStatus status = compile(module, statement, words, &result, source, sizeof(source), &length);
    if (status != DOE_COMPILER_OK) fprintf(stderr, "rejected: %s\n%s\n", result.diagnostic.message, source);
    assert(status == DOE_COMPILER_OK);
    assert(compile(plain_module, plain_statement, plain, &plain_result, plain_source, sizeof(plain_source),
                   &plain_length) == DOE_COMPILER_OK);
    assert(result.word_count > 0u && result.word_count == plain_result.word_count);
    assert(memcmp(words, plain, result.word_count * sizeof(uint32_t)) == 0);
    assert(memcmp(&result.reflection, &plain_result.reflection, sizeof(result.reflection)) == 0);
}

static void rejects(const char *module, const char *statement, const char *message) {
    char source[2048];
    int length = 0;
    uint32_t words[DOE_COMPILER_MAX_WORDS];
    memset(words, 0xaa, sizeof(words));
    DoeCompilerResult result;
    const DoeCompilerStatus status = compile(module, statement, words, &result, source, sizeof(source), &length);
    if (status == DOE_COMPILER_OK) fprintf(stderr, "accepted:\n%s\n", source);
    assert(status != DOE_COMPILER_OK);
    assert(result.word_count == 0u);
    for (size_t i = 0u; i < DOE_COMPILER_MAX_WORDS; ++i) assert(words[i] == 0xaaaaaaaau);
    if (message != NULL && strstr(result.diagnostic.message, message) == NULL) {
        fprintf(stderr, "expected '%s', got '%s'\n%s\n", message, result.diagnostic.message, source);
        assert(false);
    }
    /* Every semantic rejection points into the source text. */
    assert(result.diagnostic.offset <= (size_t)length);
}

int main(void) {
    /* Module scope, including references declared before and after the assertion. */
    accepts("const_assert true;", "", "", "");
    accepts("const_assert N == 4u;", "", "", "");
    accepts("const_assert M > N; const M: u32 = 5u;", "", "const M: u32 = 5u;", "");
    accepts("const_assert N == 4u; const_assert N != 5u; const_assert (N & 1u) == 0u;", "", "", "");
    /* Function scope, including nested blocks and function-local constants. */
    accepts("", " const_assert N < 8u;", "", "");
    accepts("", " { const L = 3u; const_assert L + N == 7u; }", "", " { const L = 3u; }");
    accepts("", " if (id.x == 0u) { const_assert true; }", "", " if (id.x == 0u) { }");

    rejects("const_assert false;", "", "const_assert failed");
    rejects("const_assert N == 5u;", "", "const_assert failed");
    rejects("const_assert N;", "", "scalar bool");
    rejects("const_assert vec2<bool>(true, true);", "", "scalar bool");
    rejects("override O: u32 = 1u; const_assert O == 1u;", "", NULL);
    rejects("", " const_assert N == 3u;", "const_assert failed");
    rejects("", " const_assert id.x == 0u;", NULL);
    rejects("", " let v = 1u; const_assert v == 1u;", NULL);
    rejects("const_assert MISSING;", "", NULL);
    rejects("const_assert true", "", NULL);
    /* const_assert is a statement, not a simple statement inside a for header. */
    rejects("", " for (const_assert true; false;) {}", NULL);
    printf("const_assert checks: 7 accepted, 11 rejected\n");
    return 0;
}
