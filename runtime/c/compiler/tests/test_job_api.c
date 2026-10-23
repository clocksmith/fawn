/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler_job.h"
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const DoeCompilerJobCapacities capacities = {
    .source_bytes = 65536u, .tokens = 4096u, .statements = 256u,
    .expressions = 1024u, .words = 16384u, .expression_depth = 32u, .control_depth = 16u
};
static const char source[] =
    "@group(0) @binding(0) var<storage,read_write> out:array<u32>;"
    "@compute @workgroup_size(1) fn run(){var n:u32;"
    "for(var i:u32=0;i<8u;i++){var zero:u32;let snap=n;"
    "if(i==2u){continue;}n=snap+i+zero;}"
    "loop{let snap=n;n++;continuing{out[0]=snap;break if n>30u;}}}";
static const char noop[] = "@compute @workgroup_size(1) fn empty(){}";
static DoeCompilerWorkspaceRequirements requirements;
static DoeCompilerStatus compile(const DoeCompilerJobCapacities *caps, void *workspace,
                                 size_t size, const char *text, size_t length,
                                 uint32_t *words, size_t count, DoeCompilerResultV2 *result) {
    return doe_compiler_compile_job(3u, caps, sizeof(*caps), workspace, size, text, length,
                                    words, count, result, sizeof(*result));
}
static bool all_bytes(const void *memory, size_t size, unsigned char value) {
    const unsigned char *bytes = memory;
    for (size_t i = 0u; i < size; ++i) if (bytes[i] != value) return false;
    return true;
}
static void atomic_failure(const DoeCompilerResultV2 *result, const uint32_t *words) {
    assert(result->word_count == 0u && result->diagnostic.status != DOE_COMPILER_OK);
    assert(result->diagnostic.message[0] != '\0');
    assert(all_bytes(&result->reflection, sizeof(result->reflection), 0u));
    assert(all_bytes(words, capacities.words * sizeof(*words), 0xabu));
}
static void *parallel(void *unused) {
    (void)unused;
    void *workspace = malloc(requirements.size_bytes);
    uint32_t *words = malloc(capacities.words * sizeof(*words));
    uint32_t *first_words = malloc(capacities.words * sizeof(*first_words));
    DoeCompilerResultV2 result, first;
    assert(workspace != NULL && words != NULL && first_words != NULL);
    assert(compile(&capacities, workspace, requirements.size_bytes, source, sizeof(source)-1u,
                   first_words, capacities.words, &first) == DOE_COMPILER_OK);
    for (unsigned i = 0u; i < 128u; ++i) {
        assert(compile(&capacities, workspace, requirements.size_bytes, noop, sizeof(noop)-1u,
                       words, capacities.words, &result) == DOE_COMPILER_OK);
        assert(compile(&capacities, workspace, requirements.size_bytes, source, sizeof(source)-1u,
                       words, capacities.words, &result) == DOE_COMPILER_OK);
        assert(memcmp(&first, &result, sizeof(result)) == 0);
        assert(memcmp(first_words, words, first.word_count * sizeof(*words)) == 0);
    }
    free(first_words); free(words); free(workspace);
    return NULL;
}
static void legacy_boundaries(void) {
    DoeCompilerWorkspaceRequirements req;
    assert(doe_compiler_workspace_requirements(2u, &req, sizeof(req)) == DOE_COMPILER_OK);
    void *workspace = malloc(req.size_bytes);
    uint32_t first[DOE_COMPILER_MAX_WORDS], second[DOE_COMPILER_MAX_WORDS];
    DoeCompilerResult a;
    DoeCompilerResultV2 b;
    char text[8192];
    const char *prefix = "@group(0) @binding(0) var<storage,read_write> out:array<u32>;"
        "@compute @workgroup_size(1) fn f(@builtin(global_invocation_id) gid:vec3u){";
    assert(workspace != NULL);
    for (unsigned shape = 0u; shape < 7u; ++shape) {
        strcpy(text, prefix);
        DoeCompilerStatus wanted = DOE_COMPILER_OK;
        if (shape <= 2u) {
            strcat(text, "let lane=gid.x;"); /* Exempt exact legacy alias. */
            for (unsigned i = 0u; i < 16u; ++i)
                strcat(text, "out[0u]=1u+1u+1u+1u+1u+1u+1u+1u+1u+1u;");
            if (shape == 1u) { strcat(text, "return;"); wanted = DOE_COMPILER_INPUT_ERROR; }
            if (shape == 2u) {
                /* A 321st scalar node without another statement. */
                char *literal = strstr(text, "=1u+");
                assert(literal != NULL);
                const size_t tail = strlen(literal + 3u);
                memmove(literal + 8u, literal + 3u, tail + 1u);
                memcpy(literal, "=out[0u]", 8u);
                wanted = DOE_COMPILER_INPUT_ERROR;
            }
        } else if (shape == 3u) {
            /* Sixteen comparisons add sixteen physical wrapper nodes but no
             * source-node charge: 320 scalar nodes plus 16 wrappers. */
            for (unsigned group = 0u; group < 4u; ++group) {
                for (unsigned depth = 0u; depth < 4u; ++depth)
                    strcat(text, "if(1u+1u+1u+1u+1u+1u+1u+1u+1u+1u==10u){");
                strcat(text, "}}}}");
            }
        } else {
            strcat(text, "out[0u]=");
            const unsigned depth = shape == 4u ? 18u : 19u;
            for (unsigned i = 1u; i < depth; ++i) strcat(text, "(");
            strcat(text, "1u");
            for (unsigned i = 1u; i < depth; ++i) strcat(text, ")");
            strcat(text, ";");
            if (depth == 19u) wanted = DOE_COMPILER_INPUT_ERROR;
        }
        strcat(text, "}");
        assert(doe_compiler_compile(1u, text, strlen(text), first, DOE_COMPILER_MAX_WORDS,
                                    &a, sizeof(a)) == wanted);
        assert(doe_compiler_compile_v2(2u, workspace, req.size_bytes, text, strlen(text), second,
                                       DOE_COMPILER_MAX_WORDS, &b, sizeof(b)) == wanted);
        if (wanted == DOE_COMPILER_OK) {
            assert(a.word_count == b.word_count);
            assert(memcmp(first, second, a.word_count * sizeof(*first)) == 0);
        }
    }
    /* Amendment2: empty module declarations lift consistently in every API. */
    const char *empty = ";;@compute @workgroup_size(1) fn f(){};;";
    assert(doe_compiler_compile(1u, empty, strlen(empty), first, DOE_COMPILER_MAX_WORDS,
                                &a, sizeof(a)) == DOE_COMPILER_OK);
    assert(doe_compiler_compile_v2(2u, workspace, req.size_bytes, empty, strlen(empty), second,
                                   DOE_COMPILER_MAX_WORDS, &b, sizeof(b)) == DOE_COMPILER_OK);
    assert(a.word_count == b.word_count && memcmp(first, second, a.word_count*sizeof(*first)) == 0);
    free(workspace);
}

int main(void) {
    legacy_boundaries();
    assert(doe_compiler_job_requirements(3u, &capacities, sizeof(capacities), &requirements,
                                         sizeof(requirements)) == DOE_COMPILER_OK);
    assert(requirements.alignment_bytes <= _Alignof(max_align_t));
    DoeCompilerWorkspaceRequirements legacy;
    assert(doe_compiler_workspace_requirements(2u, &legacy, sizeof(legacy)) == DOE_COMPILER_OK);
    assert(legacy.size_bytes <= 77760u);
    void *workspace = malloc(requirements.size_bytes + 16u);
    uint32_t *words = malloc(capacities.words * sizeof(*words));
    uint32_t *expected = malloc(capacities.words * sizeof(*expected));
    DoeCompilerResultV2 result;
    assert(workspace != NULL && words != NULL && expected != NULL);
    memset(workspace, 0xabu, requirements.size_bytes + 16u);
    assert(compile(&capacities, workspace, requirements.size_bytes, source, sizeof(source)-1u,
                   expected, capacities.words, &result) == DOE_COMPILER_OK);
    const size_t output_count = result.word_count;
    assert(all_bytes((unsigned char *)workspace + requirements.size_bytes, 16u, 0xabu));
    for (size_t count = 0u; count < output_count; ++count) {
        memset(words, 0xab, capacities.words * sizeof(*words));
        assert(compile(&capacities, workspace, requirements.size_bytes, source, sizeof(source)-1u,
                       words, count, &result) == DOE_COMPILER_OUTPUT_TOO_SMALL);
        atomic_failure(&result, words);
    }
    DoeCompilerJobCapacities changed = capacities;
    const size_t tiny[] = {0u,1u,4u,output_count-1u};
    for (size_t i = 0u; i < sizeof(tiny)/sizeof(tiny[0]); ++i) {
        changed.words = tiny[i];
        memset(words, 0xab, capacities.words * sizeof(*words));
        assert(compile(&changed, workspace, requirements.size_bytes, source, sizeof(source)-1u,
                       words, capacities.words, &result) == DOE_COMPILER_OUTPUT_TOO_SMALL);
        atomic_failure(&result, words);
    }
    changed = capacities; changed.source_bytes = 4u;
    assert(compile(&changed, workspace, requirements.size_bytes, source, sizeof(source)-1u,
                   words, capacities.words, &result) == DOE_COMPILER_INPUT_ERROR);
    assert(result.diagnostic.offset == 4u);
    for (unsigned dimension = 0u; dimension < 5u; ++dimension) {
        changed = capacities;
        if (dimension == 0u) changed.tokens = 0u;
        if (dimension == 1u) changed.statements = 0u;
        if (dimension == 2u) changed.expressions = 0u;
        if (dimension == 3u) changed.expression_depth = 1u;
        if (dimension == 4u) changed.control_depth = 1u;
        memset(words, 0xab, capacities.words * sizeof(*words));
        assert(compile(&changed, workspace, requirements.size_bytes, source, sizeof(source)-1u,
                       words, capacities.words, &result) == DOE_COMPILER_INPUT_ERROR);
        atomic_failure(&result, words);
    }
    for (unsigned kind = 0u; kind < 5u; ++kind) {
        changed = capacities;
        if (kind == 0u) changed.source_bytes = UINT32_MAX;
        if (kind == 1u) changed.words = SIZE_MAX;
        if (kind == 2u) changed.expressions = UINT32_MAX;
        if (kind == 3u) changed.expression_depth = 65u;
        if (kind == 4u) changed.control_depth = 33u;
        DoeCompilerWorkspaceRequirements invalid;
        memset(&invalid, 0xab, sizeof(invalid));
        assert(doe_compiler_job_requirements(3u, &changed, sizeof(changed), &invalid,
                                             sizeof(invalid)) == DOE_COMPILER_INPUT_ERROR);
        assert(all_bytes(&invalid, sizeof(invalid), 0xabu));
        memset(workspace, 0xab, requirements.size_bytes);
        memset(words, 0xab, capacities.words * sizeof(*words));
        assert(compile(&changed, workspace, requirements.size_bytes, source, sizeof(source)-1u,
                       words, capacities.words, &result) == DOE_COMPILER_INPUT_ERROR);
        atomic_failure(&result, words);
        assert(all_bytes(workspace, requirements.size_bytes, 0xabu));
    }
    memset(&result, 0xab, sizeof(result));
    assert(doe_compiler_compile_job(99u, &capacities, sizeof(capacities), workspace,
        requirements.size_bytes, source, sizeof(source)-1u, words, capacities.words,
        &result, sizeof(result)) == DOE_COMPILER_ABI_MISMATCH);
    assert(all_bytes(&result, sizeof(result), 0xabu));
    memset(workspace, 0xab, requirements.size_bytes);
    for (unsigned bad = 0u; bad < 3u; ++bad) {
        void *pointer = bad == 0u ? NULL : bad == 1u ? (unsigned char *)workspace+1u : workspace;
        const size_t size = bad == 2u ? requirements.size_bytes-1u : requirements.size_bytes;
        assert(compile(&capacities, pointer, size, source, sizeof(source)-1u,
                       words, capacities.words, &result) == DOE_COMPILER_INPUT_ERROR);
        atomic_failure(&result, words);
        assert(all_bytes(workspace, requirements.size_bytes, 0xabu));
    }
    /* Source capacity is caller-owned, not the old 65536-byte public macro. */
    changed = capacities; changed.source_bytes = 70000u;
    char *large_source = malloc(changed.source_bytes);
    assert(large_source != NULL);
    memset(large_source, ' ', changed.source_bytes);
    memcpy(large_source, noop, sizeof(noop)-1u);
    assert(compile(&changed, workspace, requirements.size_bytes, large_source, changed.source_bytes,
                   words, capacities.words, &result) == DOE_COMPILER_OK);
    free(large_source);
    /* Maximum configured recursion remains independent of storage capacities. */
    changed = capacities; changed.expression_depth = 64u; changed.control_depth = 32u;
    char nested[4096] = "@compute @workgroup_size(1) fn nested(){";
    for (unsigned i = 0u; i < 31u; ++i) strcat(nested, "while false{");
    strcat(nested, "if ");
    for (unsigned i = 0u; i < 63u; ++i) strcat(nested, "(");
    strcat(nested, "true");
    for (unsigned i = 0u; i < 63u; ++i) strcat(nested, ")");
    strcat(nested, "{}");
    for (unsigned i = 0u; i < 32u; ++i) strcat(nested, "}");
    assert(compile(&changed, workspace, requirements.size_bytes, nested, strlen(nested),
                   words, capacities.words, &result) == DOE_COMPILER_OK);
    const unsigned char replacements[] = {0u,' ',';','{','}','(',')','u',255u};
    char mutated[sizeof(source)];
    for (size_t i = 0u; i < sizeof(source)-1u; ++i) {
        memset(words, 0xab, capacities.words * sizeof(*words));
        DoeCompilerStatus status = compile(&capacities, workspace, requirements.size_bytes,
            source, i, words, capacities.words, &result);
        if (status != DOE_COMPILER_OK) atomic_failure(&result, words);
        for (size_t j = 0u; j < sizeof(replacements); ++j) {
            memcpy(mutated, source, sizeof(source)); mutated[i] = (char)replacements[j];
            memset(words, 0xab, capacities.words * sizeof(*words));
            status = compile(&capacities, workspace, requirements.size_bytes, mutated,
                sizeof(source)-1u, words, capacities.words, &result);
            if (status != DOE_COMPILER_OK) atomic_failure(&result, words);
        }
    }
    assert(compile(&capacities, workspace, requirements.size_bytes, source, sizeof(source)-1u,
                   words, capacities.words, &result) == DOE_COMPILER_OK);
    assert(memcmp(words, expected, output_count * sizeof(*words)) == 0);
    free(expected); free(words); free(workspace);
    pthread_t threads[4];
    for (size_t i = 0u; i < 4u; ++i) assert(pthread_create(&threads[i], NULL, parallel, NULL) == 0);
    for (size_t i = 0u; i < 4u; ++i) assert(pthread_join(threads[i], NULL) == 0);
    printf("Job API: %zu workspace bytes, %zu output words; capacity/failure/mutation/reuse/1024 parallel calls passed\n",
           requirements.size_bytes, output_count);
    return 0;
}
