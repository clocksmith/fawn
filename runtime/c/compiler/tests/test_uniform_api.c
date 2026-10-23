/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#define _GNU_SOURCE
#include "../include/doe_compiler_v2.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static const char shader[] =
    "@group(0) @binding(0) var<uniform> p:P;"
    "struct P {x:u32,y:f32,z:u32,}"
    "@group(0) @binding(1) var<storage,read_write> o:array<u32>;"
    "@compute @workgroup_size(1) fn go(@builtin(global_invocation_id) g:vec3u){"
    "let x=p.x;if(g.x<p.z){o[g.x]=x;}return;}";
static DoeCompilerWorkspaceRequirements requirements;
static DoeCompilerStatus compile(void *workspace, size_t size, const char *source,
                                 uint32_t *words, size_t capacity, DoeCompilerResultV2 *result) {
    return doe_compiler_compile_v2(2u, workspace, size, source, strlen(source), words,
                                   capacity, result, sizeof(*result));
}
static void zero_failure(const DoeCompilerResultV2 *result, const uint32_t *words) {
    assert(result->word_count == 0u);
    const unsigned char *bytes = (const unsigned char *)&result->reflection;
    for (size_t i = 0; i < sizeof(result->reflection); ++i) assert(bytes[i] == 0u);
    for (size_t i = 0; i < DOE_COMPILER_MAX_WORDS; ++i) assert(words[i] == 0xababababu);
}
static void *parallel(void *unused) {
    (void)unused;
    void *workspace = malloc(requirements.size_bytes);
    DoeCompilerResultV2 *result = malloc(sizeof(*result));
    uint32_t words[DOE_COMPILER_MAX_WORDS];
    assert(workspace != NULL && result != NULL);
    for (unsigned i = 0; i < 128u; ++i) {
        assert(compile(workspace, requirements.size_bytes, shader, words,
                       DOE_COMPILER_MAX_WORDS, result) == DOE_COMPILER_OK);
        assert(result->reflection.structs[0].members[2].offset_bytes == 8u);
    }
    free(result); free(workspace);
    return NULL;
}
int main(void) {
    _Static_assert(sizeof(DoeCompilerResult) == 1008u, "ABI1 result unchanged");
    _Static_assert(sizeof(DoeCompilerMemberV2) == 80u, "member layout");
    _Static_assert(sizeof(DoeCompilerStructV2) == 716u, "struct layout");
    _Static_assert(sizeof(DoeCompilerBindingV2) == 108u, "binding layout");
    _Static_assert(sizeof(DoeCompilerReflectionV2) == 6676u, "reflection layout");
    _Static_assert(sizeof(DoeCompilerResultV2) == 6904u, "result layout");
    assert(doe_compiler_workspace_requirements(2u, NULL, sizeof(requirements)) == DOE_COMPILER_INPUT_ERROR);
    memset(&requirements, 0xab, sizeof(requirements));
    const DoeCompilerWorkspaceRequirements before = requirements;
    assert(doe_compiler_workspace_requirements(1u, &requirements, sizeof(requirements)) == DOE_COMPILER_ABI_MISMATCH);
    assert(memcmp(&requirements, &before, sizeof(before)) == 0);
    assert(doe_compiler_workspace_requirements(2u, &requirements, sizeof(requirements) - 1u) == DOE_COMPILER_ABI_MISMATCH);
    assert(memcmp(&requirements, &before, sizeof(before)) == 0);
    assert(doe_compiler_workspace_requirements(2u, &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    assert(requirements.alignment_bytes <= _Alignof(max_align_t));
    unsigned char *workspace = malloc(requirements.size_bytes + requirements.alignment_bytes);
    unsigned char *saved_workspace = malloc(requirements.size_bytes);
    DoeCompilerResultV2 *result = malloc(sizeof(*result));
    DoeCompilerResultV2 *saved_result = malloc(sizeof(*result));
    uint32_t words[DOE_COMPILER_MAX_WORDS], expected[DOE_COMPILER_MAX_WORDS];
    assert(workspace && saved_workspace && result && saved_result);
    memset(workspace, 0xcd, requirements.size_bytes);
    memcpy(saved_workspace, workspace, requirements.size_bytes);
    memset(result, 0xef, sizeof(*result)); memcpy(saved_result, result, sizeof(*result));
    memset(words, 0xab, sizeof(words));
    for (unsigned mode = 0; mode < 3u; ++mode) {
        const DoeCompilerStatus status = doe_compiler_compile_v2(mode == 0u ? 1u : 2u,
            workspace, requirements.size_bytes, shader, strlen(shader), words,
            DOE_COMPILER_MAX_WORDS, mode == 2u ? NULL : result,
            mode == 1u ? sizeof(*result) - 1u : sizeof(*result));
        assert(status == (mode == 2u ? DOE_COMPILER_INPUT_ERROR : DOE_COMPILER_ABI_MISMATCH));
        assert(memcmp(result, saved_result, sizeof(*result)) == 0);
        assert(memcmp(workspace, saved_workspace, requirements.size_bytes) == 0);
    }
    for (unsigned mode = 0; mode < 3u; ++mode) {
        assert(compile(mode == 0u ? NULL : workspace + (mode == 1u ? 1u : 0u),
            mode == 2u ? requirements.size_bytes - 1u : requirements.size_bytes,
            shader, words, DOE_COMPILER_MAX_WORDS, result) == DOE_COMPILER_INPUT_ERROR);
        assert(result->diagnostic.offset == 0u); zero_failure(result, words);
        assert(memcmp(workspace, saved_workspace, requirements.size_bytes) == 0);
    }
    assert(compile(workspace, requirements.size_bytes, shader, expected,
                   DOE_COMPILER_MAX_WORDS, result) == DOE_COMPILER_OK);
    const size_t count = result->word_count;
    assert(result->reflection.binding_count == 2u && result->reflection.struct_count == 1u);
    assert(result->reflection.bindings[0].minimum_binding_size_bytes == 12u);
    assert(result->reflection.bindings[0].address_space_alignment_bytes == 16u);
    assert(result->reflection.bindings[1].minimum_binding_size_bytes == 4u);
    assert(result->reflection.bindings[1].struct_index == DOE_COMPILER_NO_STRUCT);
    for (size_t capacity = 0; capacity < count; ++capacity) {
        memset(words, 0xab, sizeof(words));
        assert(compile(workspace, requirements.size_bytes, shader, words, capacity, result) == DOE_COMPILER_OUTPUT_TOO_SMALL);
        zero_failure(result, words);
        assert(compile(workspace, requirements.size_bytes, shader, words, count, result) == DOE_COMPILER_OK);
        assert(memcmp(words, expected, count * 4u) == 0);
        for (size_t i = count; i < DOE_COMPILER_MAX_WORDS; ++i) assert(words[i] == 0xababababu);
    }
    assert(compile(workspace, requirements.size_bytes + requirements.alignment_bytes,
                   shader, words, DOE_COMPILER_MAX_WORDS, result) == DOE_COMPILER_OK);
    DoeCompilerResult legacy;
    assert(doe_compiler_compile(1u, shader, strlen(shader), words, DOE_COMPILER_MAX_WORDS,
                                &legacy, sizeof(legacy)) == DOE_COMPILER_UNSUPPORTED);
    char changed[sizeof(shader)];
    for (size_t i = 0; i < sizeof(shader) - 1u; ++i) {
        memcpy(changed, shader, sizeof(shader)); changed[i] = '\0';
        memset(words, 0xab, sizeof(words));
        DoeCompilerStatus status = compile(workspace, requirements.size_bytes, changed,
                                            words, DOE_COMPILER_MAX_WORDS, result);
        if (status != DOE_COMPILER_OK) zero_failure(result, words);
        memcpy(changed, shader, sizeof(shader)); changed[i] = (char)(1u + (i * 37u) % 127u);
        memset(words, 0xab, sizeof(words));
        status = compile(workspace, requirements.size_bytes, changed, words, DOE_COMPILER_MAX_WORDS, result);
        if (status != DOE_COMPILER_OK) zero_failure(result, words);
        assert(compile(workspace, requirements.size_bytes, shader, words,
                       DOE_COMPILER_MAX_WORDS, result) == DOE_COMPILER_OK);
    }
    const long page_query = sysconf(_SC_PAGESIZE); assert(page_query > 0);
    const size_t page = (size_t)page_query;
    const size_t rounded = ((requirements.size_bytes + page - 1u) / page) * page;
    unsigned char *guard = mmap(NULL, rounded + page, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(guard != MAP_FAILED && mprotect(guard + rounded, page, PROT_NONE) == 0);
    void *guarded_workspace = guard + rounded - requirements.size_bytes;
    assert(compile(guarded_workspace, requirements.size_bytes, shader, words,
                   DOE_COMPILER_MAX_WORDS, result) == DOE_COMPILER_OK);
    assert(munmap(guard, rounded + page) == 0);
    pthread_t threads[4];
    for (size_t i = 0; i < 4u; ++i) assert(pthread_create(&threads[i], NULL, parallel, NULL) == 0);
    for (size_t i = 0; i < 4u; ++i) assert(pthread_join(threads[i], NULL) == 0);
    printf("v2 workspace %zu alignment %zu; result %zu; %zu witness words; 512 parallel compiles passed\n",
           requirements.size_bytes, requirements.alignment_bytes, sizeof(*result), count);
    free(saved_result); free(result); free(saved_workspace); free(workspace);
    return 0;
}
