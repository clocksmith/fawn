/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler_job.h"
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void run_case(const char *source, size_t tokens, size_t expressions, size_t statements,
                     DoeCompilerStatus expected, const char *diagnostic) {
    const DoeCompilerJobCapacities capacities = { .source_bytes = strlen(source), .tokens = tokens,
        .expressions = expressions, .statements = statements, .words = 1000000u,
        .expression_depth = 8u, .control_depth = 4u };
    DoeCompilerWorkspaceRequirements requirements;
    errno = EDOM;
    assert(doe_compiler_job_requirements(3u, &capacities, sizeof(capacities), &requirements,
                                        sizeof(requirements)) == DOE_COMPILER_OK);
    assert(errno == EDOM);
    unsigned char *workspace = malloc(requirements.size_bytes + 32u);
    uint32_t *words = malloc(capacities.words * sizeof(*words));
    DoeCompilerResultV2 *result = malloc(sizeof(*result));
    assert(workspace != NULL && words != NULL && result != NULL);
    memset(workspace, 0xab, requirements.size_bytes + 32u);
    memset(words, 0xcd, capacities.words * sizeof(*words));
    errno = E2BIG;
    const DoeCompilerStatus status = doe_compiler_compile_job(3u, &capacities, sizeof(capacities),
        workspace, requirements.size_bytes, source, strlen(source), words, capacities.words, result, sizeof(*result));
    assert(errno == E2BIG);
    if (status != expected) fprintf(stderr, "expected=%u actual=%u diagnostic=%s\n",
                                   (unsigned)expected, (unsigned)status, result->diagnostic.message);
    assert(status == expected);
    for (size_t i = 0u; i < 32u; ++i) assert(workspace[requirements.size_bytes + i] == 0xabu);
    if (expected != DOE_COMPILER_OK) {
        assert(result->word_count == 0u);
        for (size_t i = 0u; i < capacities.words; ++i) assert(words[i] == UINT32_C(0xcdcdcdcd));
        assert(strstr(result->diagnostic.message, diagnostic) != NULL);
    } else {
        assert(result->word_count > 0u);
        for (size_t i = 5u; i < result->word_count;) {
            const uint32_t count = words[i] >> 16u;
            assert(count > 0u && count <= result->word_count - i);
            i += count;
        }
    }
    printf("tokens=%zu E=%zu S=%zu status=%u words=%zu workspace=%zu %s\n",
           tokens, expressions, statements, (unsigned)status, result->word_count,
           requirements.size_bytes, result->diagnostic.message);
    free(result); free(words); free(workspace);
}
static void formals(size_t count, bool call, DoeCompilerStatus expected) {
    const size_t capacity = 24u * count + 128u;
    char *source = malloc(capacity);
    assert(source != NULL);
    size_t length = (size_t)snprintf(source, capacity, "fn many(");
    for (size_t i = 0u; i < count; ++i)
        length += (size_t)snprintf(source + length, capacity - length, "%sp%zu:u32", i == 0u ? "" : ",", i);
    length += (size_t)snprintf(source + length, capacity - length,
                             "){}@compute @workgroup_size(1) fn main(){");
    if (call) {
        length += (size_t)snprintf(source + length, capacity - length, "many(");
        for (size_t i = 0u; i < count; ++i)
            length += (size_t)snprintf(source + length, capacity - length, "%s0u", i == 0u ? "" : ",");
        length += (size_t)snprintf(source + length, capacity - length, ");");
    }
    length += (size_t)snprintf(source + length, capacity - length, "}");
    assert(length < capacity);
    run_case(source, count * (call ? 6u : 4u) + 32u, call ? count + 1u : 0u,
             call ? 1u : 0u, expected, "SPIR-V instruction word-count");
    free(source);
}
int main(void) {
    DoeCompilerWorkspaceRequirements fixed;
    assert(doe_compiler_workspace_requirements(2u, &fixed, sizeof(fixed)) == DOE_COMPILER_OK);
    assert(fixed.size_bytes <= 122880u);
    printf("fixed workspace=%zu alignment=%zu\n", fixed.size_bytes, fixed.alignment_bytes);
    char source[24000];
    size_t length = 0u;
    for (unsigned i = 0u; i < 1000u; ++i)
        length += (size_t)snprintf(source + length, sizeof(source) - length, "fn f%u(){}", i);
    (void)snprintf(source + length, sizeof(source) - length, "@compute @workgroup_size(1) fn main(){}");
    run_case(source, 6013u, 0u, 0u, DOE_COMPILER_OK, "");
    run_case("const X=exp(-1000.0);@compute @workgroup_size(1) fn main(){}", 32u, 3u, 0u, DOE_COMPILER_OK, "");
    run_case("const X=exp(1000.0);@compute @workgroup_size(1) fn main(){}", 32u, 2u, 0u, DOE_COMPILER_SEMANTIC_ERROR, "finite");
    formals(65532u, false, DOE_COMPILER_OK);
    formals(65533u, false, DOE_COMPILER_UNSUPPORTED);
    formals(65531u, true, DOE_COMPILER_OK);
    formals(65532u, true, DOE_COMPILER_UNSUPPORTED);
    puts("M9 direct job metadata and SPIR-V format boundaries passed");
    return 0;
}
