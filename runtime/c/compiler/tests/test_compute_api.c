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

static const char source[] =
    "@group(0) @binding(0) var<storage,read> s:array<u32>;"
    "@group(0) @binding(1) var<storage,read_write> d:array<u32>;"
    "@compute @workgroup_size(2,3,4,) fn run("
    "@builtin(num_workgroups) n:vec3u,@builtin(local_invocation_index) i:u32,"
    "@builtin(workgroup_id) w:vec3u,@builtin(global_invocation_id) g:vec3<u32>,"
    "@builtin(local_invocation_id) l:vec3u,){"
    "let first=d[s[g.y]*2u+g.x];"
    "if(s[s[i]]+l.z<n.x*w.y+1u){d[s[99u]]=first;}"
    "else{d[(g.z+1u)*2u]=s[max(s[w.x],l.y)];}}";
static const char noop[] = "@compute @workgroup_size(1) fn empty(){}";
static DoeCompilerWorkspaceRequirements requirements;

static DoeCompilerStatus compile(void *workspace, const char *text, size_t length,
                                 uint32_t *words, size_t capacity, DoeCompilerResultV2 *result) {
    return doe_compiler_compile_v2(2u, workspace, requirements.size_bytes, text, length,
                                   words, capacity, result, sizeof(*result));
}
static void failure_atomic(const DoeCompilerResultV2 *result, const uint32_t *words) {
    assert(result->word_count == 0u && result->diagnostic.status != DOE_COMPILER_OK);
    assert(result->diagnostic.message[0] != '\0');
    const unsigned char *reflection = (const unsigned char *)&result->reflection;
    for (size_t i = 0; i < sizeof(result->reflection); ++i) assert(reflection[i] == 0u);
    for (size_t i = 0; i < DOE_COMPILER_MAX_WORDS; ++i) assert(words[i] == 0xababababu);
}
static void *parallel(void *unused) {
    (void)unused;
    void *workspace = malloc(requirements.size_bytes);
    DoeCompilerResultV2 result, first;
    uint32_t words[DOE_COMPILER_MAX_WORDS], expected[DOE_COMPILER_MAX_WORDS];
    assert(workspace != NULL);
    assert(compile(workspace, source, sizeof(source) - 1u, expected,
                   DOE_COMPILER_MAX_WORDS, &first) == DOE_COMPILER_OK);
    for (unsigned i = 0; i < 128u; ++i) {
        assert(compile(workspace, noop, sizeof(noop) - 1u, words,
                       DOE_COMPILER_MAX_WORDS, &result) == DOE_COMPILER_OK);
        assert(result.reflection.binding_count == 0u);
        assert(compile(workspace, source, sizeof(source) - 1u, words,
                       DOE_COMPILER_MAX_WORDS, &result) == DOE_COMPILER_OK);
        assert(memcmp(&first, &result, sizeof(result)) == 0);
        assert(memcmp(expected, words, first.word_count * sizeof(*words)) == 0);
    }
    free(workspace);
    return NULL;
}
int main(void) {
    assert(doe_compiler_workspace_requirements(2u, &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    /* Match the current fixed-storage ledger, unchanged from the baseline. */
    assert(requirements.size_bytes == (UINTPTR_MAX == UINT64_MAX ? 146048u : 145920u));
    const long page_query = sysconf(_SC_PAGESIZE);
    assert(page_query > 0);
    const size_t page = (size_t)page_query;
    const size_t rounded = ((requirements.size_bytes + page - 1u) / page) * page;
    unsigned char *storage = mmap(NULL, rounded + page, PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    char *input = mmap(NULL, 2u * page, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(storage != MAP_FAILED && input != MAP_FAILED);
    assert(mprotect(storage + rounded, page, PROT_NONE) == 0);
    assert(mprotect(input + page, page, PROT_NONE) == 0);
    void *workspace = storage + rounded - requirements.size_bytes;
    char *text = input + page - (sizeof(source) - 1u);
    memcpy(text, source, sizeof(source) - 1u);
    uint32_t words[DOE_COMPILER_MAX_WORDS], expected[DOE_COMPILER_MAX_WORDS];
    DoeCompilerResultV2 result;
    assert(compile(workspace, text, sizeof(source) - 1u, expected,
                   DOE_COMPILER_MAX_WORDS, &result) == DOE_COMPILER_OK);
    const size_t count = result.word_count;
    DoeCompilerResult legacy;
    assert(doe_compiler_compile(1u, text, sizeof(source) - 1u, words,
                                DOE_COMPILER_MAX_WORDS, &legacy, sizeof(legacy)) == DOE_COMPILER_OK);
    assert(legacy.word_count == count && memcmp(words, expected, count * sizeof(*words)) == 0);
    assert(legacy.reflection.workgroup_size[0] == 2u && legacy.reflection.workgroup_size[1] == 3u &&
           legacy.reflection.workgroup_size[2] == 4u);
    for (size_t capacity = 0; capacity < count; ++capacity) {
        memset(words, 0xab, sizeof(words));
        assert(compile(workspace, text, sizeof(source) - 1u, words, capacity, &result) == DOE_COMPILER_OUTPUT_TOO_SMALL);
        failure_atomic(&result, words);
        assert(compile(workspace, noop, sizeof(noop) - 1u, words,
                       DOE_COMPILER_MAX_WORDS, &result) == DOE_COMPILER_OK);
        assert(compile(workspace, text, sizeof(source) - 1u, words, count, &result) == DOE_COMPILER_OK);
        assert(memcmp(words, expected, count * sizeof(*words)) == 0);
    }
    const unsigned char replacements[] = {0u, ' ', '.', '[', ']', '(', ')', ',', 'u', 255u};
    char changed[sizeof(source)];
    for (size_t i = 0; i < sizeof(source) - 1u; ++i) {
        memset(words, 0xab, sizeof(words));
        DoeCompilerStatus status = compile(workspace, text, i, words, DOE_COMPILER_MAX_WORDS, &result);
        if (status != DOE_COMPILER_OK) failure_atomic(&result, words);
        for (size_t j = 0; j < sizeof(replacements); ++j) {
            memcpy(changed, source, sizeof(source));
            changed[i] = (char)replacements[j];
            memset(words, 0xab, sizeof(words));
            status = compile(workspace, changed, sizeof(source) - 1u, words,
                             DOE_COMPILER_MAX_WORDS, &result);
            if (status != DOE_COMPILER_OK) failure_atomic(&result, words);
            assert(compile(workspace, text, sizeof(source) - 1u, words,
                           DOE_COMPILER_MAX_WORDS, &result) == DOE_COMPILER_OK);
            assert(memcmp(words, expected, count * sizeof(*words)) == 0);
        }
    }
    memset(text, '?', sizeof(source) - 1u);
    assert(strcmp(result.reflection.entry_point, "run") == 0);
    assert(strcmp(result.reflection.bindings[0].name, "s") == 0);
    assert(munmap(input, 2u * page) == 0 && munmap(storage, rounded + page) == 0);
    pthread_t threads[4];
    for (size_t i = 0; i < 4u; ++i) assert(pthread_create(&threads[i], NULL, parallel, NULL) == 0);
    for (size_t i = 0; i < 4u; ++i) assert(pthread_join(threads[i], NULL) == 0);
    puts("Compute API: ABI1/v2 equality, guards, all short capacities, source mutations, reuse and 1024 parallel compiles passed");
    return 0;
}
