/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Adapted copy of the named-constant maintenance developer regression. */
#include "../include/doe_compiler.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *declaration, *parameter, *rhs;
    bool succeeds;
    const char *bad_location;
} Case;

static void check(const Case *test) {
    char source[2048];
    const int length = snprintf(source, sizeof(source),
        "%s\n@group(0) @binding(0) var<storage, read_write> values: array<u32>;\n"
        "@compute @workgroup_size(256) fn main(@builtin(global_invocation_id) %s: vec3u) {"
        " values[%s.x] = values[%s.x] + %s; }", test->declaration, test->parameter,
        test->parameter, test->parameter, test->rhs);
    assert(length > 0 && (size_t)length < sizeof(source));
    uint32_t words[DOE_COMPILER_MAX_WORDS];
    memset(words, 0xaa, sizeof(words));
    DoeCompilerResult result;
    const DoeCompilerStatus status = doe_compiler_compile(
        DOE_COMPILER_ABI_VERSION, source, (size_t)length, words,
        DOE_COMPILER_MAX_WORDS, &result, sizeof(result));
    if (test->succeeds) {
        assert(status == DOE_COMPILER_OK);
        assert(result.reflection.bindings[0].scalar == DOE_COMPILER_U32);
        assert(result.word_count > 0u);
    } else {
        assert(status != DOE_COMPILER_OK);
        assert(result.word_count == 0u);
        for (size_t i = 0; i < DOE_COMPILER_MAX_WORDS; ++i)
            assert(words[i] == 0xaaaaaaaau);
        if (test->bad_location != NULL) {
            const char *location = strstr(source, test->bad_location);
            assert(location != NULL);
            assert(result.diagnostic.offset == (size_t)(location - source));
        }
    }
}

int main(void) {
    const Case cases[] = {
        {"const INC: u32 = 0u;", "id", "INC", true, NULL},
        {"const /*n*/ INC : u32 = 4294967295u;", "id", "INC", true, NULL},
        {"const INC: u32 = 1u;", "id", "42u", true, NULL},
        {"const INC: u32 = 1u;", "INC", "42u", true, NULL},
        {"const INC: u32 = 1u;", "INC", "INC", false, NULL},
        {"", "id", "MISSING", false, "MISSING"},
        {"const INC: u32 = 4294967296u;", "id", "INC", false, "4294967296u"},
        {"const INC: u32 = 01u;", "id", "INC", false, "01u"},
        {"const INC: i32 = 1u;", "id", "INC", false, NULL},
        {"const INC: u32 = 1u; const TWO: u32 = 2u;", "id", "INC", false, NULL},
        {"const values: u32 = 1u;", "id", "1u", false, "values: array"},
        {"const main: u32 = 1u;", "id", "1u", false, "main(@"},
        {"const u32: u32 = 1u;", "id", "1u", false, NULL},
        {"const __bad: u32 = 1u;", "id", "1u", false, "__bad"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) check(&cases[i]);
    printf("Maintainer named-constant checks: %zu passed\n", sizeof(cases) / sizeof(cases[0]));
    return 0;
}
