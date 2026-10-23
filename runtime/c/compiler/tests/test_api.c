/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DoeCompilerStatus compile_source(const char *source, size_t length,
                                        uint32_t *words, size_t capacity,
                                        DoeCompilerResult *result) {
    return doe_compiler_compile(DOE_COMPILER_ABI_VERSION, source, length,
                                words, capacity, result, sizeof(*result));
}

static const char seed[] =
    "@group(0) @binding(0) var<storage, read_write> values: array<u32>;"
    "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id: vec3<u32>) {"
    "values[id.x] = values[id.x] + 1u; }";
static unsigned checks;
static void expect_failure(const char *source, size_t length, DoeCompilerStatus expected) {
    uint32_t output[DOE_COMPILER_MAX_WORDS + 2u];
    uint32_t before[DOE_COMPILER_MAX_WORDS + 2u];
    DoeCompilerResult result;
    memset(output, 0xa5, sizeof(output));
    memcpy(before, output, sizeof(output));
    memset(&result, 0xbb, sizeof(result));
    const DoeCompilerStatus status = compile_source(source, length, output + 1u, DOE_COMPILER_MAX_WORDS, &result);
    assert(status != DOE_COMPILER_OK);
    if (expected != DOE_COMPILER_OK && status != expected) {
        fprintf(stderr, "expected status=%u, got=%u: %s; source=%.*s\n",
                (unsigned)expected, (unsigned)status, result.diagnostic.message,
                (int)(length < 200u ? length : 200u), source == NULL ? "" : source);
        assert(status == expected);
    }
    assert(result.word_count == 0u);
    const DoeCompilerReflection empty = {0};
    assert(memcmp(&result.reflection, &empty, sizeof(empty)) == 0);
    assert(result.diagnostic.status == status);
    assert(result.diagnostic.offset <= length);
    assert(result.diagnostic.line >= 1u && result.diagnostic.column >= 1u);
    assert(result.diagnostic.message[0] != '\0');
    assert(memcmp(output, before, sizeof(output)) == 0);
    ++checks;
}
static void replaced(char *out, size_t capacity, const char *find, const char *replacement) {
    const char *at = strstr(seed, find);
    assert(at != NULL);
    const size_t prefix = (size_t)(at - seed);
    const int count = snprintf(out, capacity, "%.*s%s%s", (int)prefix, seed, replacement, at + strlen(find));
    assert(count > 0 && (size_t)count < capacity);
}
static void invalid_replacement(const char *find, const char *replacement) {
    char source[4096];
    replaced(source, sizeof(source), find, replacement);
    expect_failure(source, strlen(source), DOE_COMPILER_OK);
}
int main(void) {
    uint32_t output[DOE_COMPILER_MAX_WORDS + 2u];
    DoeCompilerResult result;
    memset(output, 0xa5, sizeof(output));
    assert(compile_source(seed, sizeof(seed) - 1u, output + 1u, DOE_COMPILER_MAX_WORDS, &result) == DOE_COMPILER_OK);
    assert(output[0] == 0xa5a5a5a5u && output[DOE_COMPILER_MAX_WORDS + 1u] == 0xa5a5a5a5u);
    assert(output[1] == 0x07230203u && output[2] == 0x00010300u);
    assert(result.word_count > 100u && result.word_count <= DOE_COMPILER_MAX_WORDS);
    assert(strcmp(result.reflection.entry_point, "main") == 0);
    assert(strcmp(result.reflection.bindings[0].name, "values") == 0);
    assert(result.reflection.binding_count == 1u);
    assert(result.reflection.bindings[0].scalar == DOE_COMPILER_U32);
    assert(result.reflection.bindings[0].element_stride == 4u);
    assert(result.reflection.bindings[0].runtime_sized == 1u);
    assert(result.reflection.bindings[0].access == DOE_COMPILER_READ_WRITE);
    const DoeCompilerResult first_result = result;
    uint32_t first_output[DOE_COMPILER_MAX_WORDS];
    memcpy(first_output, output + 1u, sizeof(first_output));
    const size_t exact_capacity = result.word_count;
    ++checks;
    assert(compile_source(seed, sizeof(seed) - 1u, output + 1u, exact_capacity, &result) == DOE_COMPILER_OK);
    assert(memcmp(&first_result, &result, sizeof(result)) == 0);
    memset(output, 0xa5, sizeof(output));
    assert(compile_source(seed, sizeof(seed) - 1u, output + 1u, exact_capacity - 1u, &result) == DOE_COMPILER_OUTPUT_TOO_SMALL);
    for (size_t i = 0; i < DOE_COMPILER_MAX_WORDS + 2u; ++i) assert(output[i] == 0xa5a5a5a5u);
    assert(result.word_count == 0u && result.reflection.entry_point[0] == '\0');
    assert(compile_source(seed, sizeof(seed) - 1u, NULL, 0u, &result) == DOE_COMPILER_OUTPUT_TOO_SMALL);
    assert(compile_source(seed, sizeof(seed) - 1u, NULL, 1u, &result) == DOE_COMPILER_INPUT_ERROR);
    assert(compile_source(seed, sizeof(seed) - 1u, output, DOE_COMPILER_MAX_WORDS, NULL) == DOE_COMPILER_INPUT_ERROR);
    checks += 5u;
    expect_failure(NULL, 0u, DOE_COMPILER_INPUT_ERROR);
    expect_failure(NULL, 4u, DOE_COMPILER_INPUT_ERROR);
    expect_failure("", 0u, DOE_COMPILER_INPUT_ERROR);
    expect_failure(" ", 1u, DOE_COMPILER_PARSE_ERROR);
    expect_failure("/* unclosed", 11u, DOE_COMPILER_PARSE_ERROR);
    expect_failure("//comment", 9u, DOE_COMPILER_PARSE_ERROR);
    expect_failure("@", 1u, DOE_COMPILER_PARSE_ERROR);
    char source[4096];
    replaced(source, sizeof(source), "values: array", "_: array");
    expect_failure(source, strlen(source), DOE_COMPILER_SEMANTIC_ERROR);
    const char *const forbidden[] = {"__x", "class", "fn", "requires", "readonly", "override", "Self", "NULL", "const_assert"};
    for (size_t i = 0; i < sizeof(forbidden) / sizeof(forbidden[0]); ++i) {
        char name[100];
        (void)snprintf(name, sizeof(name), "%s: array", forbidden[i]);
        invalid_replacement("values: array", name);
    }
    const char *const original[] = {
        "group(0)", "binding(0)", "storage, read_write", "read_write", "array<u32>",
        "@compute", "workgroup_size(1)", "workgroup_size(1)", "workgroup_size(1)",
        "global_invocation_id", "vec3<u32>", "values[id.x] =", "= values[id.x]", "values[id.x] =",
        "+ 1u", "+ 1u", "+ 1u", "+ 1u", "+ 1u", "+ 1u", "+ 1u", "+ 1u", "+ 1u",
        "fn main", "@group(0)", "@compute", "1u; }", "values[id.x] =", "fn main", "fn main"
    };
    const char *const replacement[] = {
        "group(4)", "binding(32)", "uniform, read_write", "read", "array<f32>",
        "@fragment", "workgroup_size(0)", "workgroup_size(257)", "workgroup_size(1,1,1)",
        "local_invocation_id", "vec4<u32>", "other[id.x] =", "= other[id.x]", "values[other.x] =",
        "- 1u", "+ 1", "+ 4294967296u", "+ 01u", "+ 0x1u", "+ 1.0u", "+ -1u", "+ 1U", "+ 1uu",
        "fn values", "@group(0) @group(0)", "@compute @compute", "1u; } fn extra() {}",
        "values[id.y] =", "fn u32", "fn array"
    };
    assert(sizeof(original) == sizeof(replacement));
    for (size_t i = 0; i < sizeof(original) / sizeof(original[0]); ++i)
        invalid_replacement(original[i], replacement[i]);
    /* Bounded source does not need a terminator, and owns no result strings. */
    char bounded[sizeof(seed) - 1u];
    memcpy(bounded, seed, sizeof(bounded));
    assert(compile_source(bounded, sizeof(bounded), output, DOE_COMPILER_MAX_WORDS, &result) == DOE_COMPILER_OK);
    memset(bounded, '?', sizeof(bounded));
    assert(strcmp(result.reflection.bindings[0].name, "values") == 0);
    assert(strcmp(first_result.reflection.entry_point, "main") == 0);
    assert(first_output[0] == 0x07230203u);
    ++checks;
    const char commented[] = "/* a /* b /* c */ d */ e */\r\n@binding(0)\n@group(0) var<storage,read_write> buf:array<u32>;"
        "@workgroup_size(256) /*ok*/ @compute fn run(@builtin(global_invocation_id) run:vec3u){"
        "buf[run.x] = buf[run.x] + 4294967295u;} //tail\n";
    assert(compile_source(commented, sizeof(commented) - 1u, output, DOE_COMPILER_MAX_WORDS, &result) == DOE_COMPILER_OK);
    assert(result.reflection.workgroup_size[0] == 256u);
    assert(strcmp(result.reflection.entry_point, "run") == 0);
    ++checks;
    char nul_source[] = "// first\r\n\0rest";
    expect_failure(nul_source, sizeof(nul_source) - 1u, DOE_COMPILER_INPUT_ERROR);
    assert(compile_source(nul_source, sizeof(nul_source) - 1u, output, DOE_COMPILER_MAX_WORDS, &result) == DOE_COMPILER_INPUT_ERROR);
    assert(result.diagnostic.offset == 10u && result.diagnostic.line == 2u && result.diagnostic.column == 1u);
    ++checks;
    char *large = malloc(DOE_COMPILER_MAX_SOURCE_BYTES + 1u);
    assert(large != NULL);
    memset(large, ' ', DOE_COMPILER_MAX_SOURCE_BYTES + 1u);
    memcpy(large, seed, sizeof(seed) - 1u);
    assert(compile_source(large, DOE_COMPILER_MAX_SOURCE_BYTES, output, DOE_COMPILER_MAX_WORDS, &result) == DOE_COMPILER_OK);
    expect_failure(large, DOE_COMPILER_MAX_SOURCE_BYTES + 1u, DOE_COMPILER_INPUT_ERROR);
    free(large);
    ++checks;
    char tokens[(DOE_COMPILER_MAX_TOKENS + 1u) * 2u];
    for (size_t i = 0; i < DOE_COMPILER_MAX_TOKENS + 1u; ++i) { tokens[i * 2u] = ';'; tokens[i * 2u + 1u] = ' '; }
    expect_failure(tokens, sizeof(tokens), DOE_COMPILER_INPUT_ERROR);
    expect_failure(tokens, DOE_COMPILER_MAX_TOKENS * 2u, DOE_COMPILER_PARSE_ERROR); /* Maximum tokens pass lexing. */
    /* Every truncation is bounded and fails; prefix of a trailing comment may be valid. */
    for (size_t i = 0; i < sizeof(seed) - 2u; ++i) expect_failure(seed, i, DOE_COMPILER_OK);
    /* Deterministic arbitrary byte inputs exercise unsupported and failure paths. */
    uint32_t random = 0x12345678u;
    for (unsigned test = 0; test < 10000u; ++test) {
        const size_t size = test % 257u;
        for (size_t i = 0; i < size; ++i) {
            random = random * 1664525u + 1013904223u;
            source[i] = (char)(random >> 24u);
        }
        expect_failure(source, size, DOE_COMPILER_OK);
    }
    printf("C API: %u checks passed (including 10000 byte-input cases)\n", checks);
    return 0;
}
