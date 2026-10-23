/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#define _GNU_SOURCE
#include "../include/doe_compiler.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static const char header[] =
    "const STEP:u32=1u;"
    "@group(0) @binding(0) var<storage,read> src:array<u32>;"
    "@group(0) @binding(1) var<storage,read_write> out:array<u32>;"
    "@compute @workgroup_size(64) fn run(@builtin(global_invocation_id) gid:vec3u){";
static const char control[] =
    "let lane=gid.x;if(lane==0u){out[lane]=src[lane];return;}"
    "if(lane>3u){if(lane<=9u){if(lane!=4u){out[0u]=src[lane-STEP]+1u;}"
    "else{return;}}else{return;}}else{out[lane]=src[lane-STEP]+src[lane];}"
    "out[1u]=out[0u]+src[1u];return;";

static size_t shader(char *source, size_t capacity, const char *body) {
    const int count = snprintf(source, capacity, "%s%s}", header, body);
    assert(count > 0 && (size_t)count < capacity);
    return (size_t)count;
}
static DoeCompilerStatus compile(const char *source, size_t length, uint32_t *words,
                                  size_t capacity, DoeCompilerResult *result) {
    return doe_compiler_compile(DOE_COMPILER_ABI_VERSION, source, length, words,
                                capacity, result, sizeof(*result));
}
static void failure(const char *source, size_t length, DoeCompilerStatus expected,
                     size_t offset) {
    uint32_t words[DOE_COMPILER_MAX_WORDS + 2u];
    memset(words, 0xa5, sizeof(words));
    DoeCompilerResult result;
    assert(compile(source, length, words + 1u, DOE_COMPILER_MAX_WORDS, &result) == expected);
    assert(result.word_count == 0u && result.diagnostic.status == expected);
    const DoeCompilerReflection empty = {0};
    assert(memcmp(&result.reflection, &empty, sizeof(empty)) == 0);
    assert(result.diagnostic.offset == offset && result.diagnostic.message[0] != '\0');
    for (size_t i = 0; i < DOE_COMPILER_MAX_WORDS + 2u; ++i) assert(words[i] == 0xa5a5a5a5u);
}
static void limits(void) {
    char source[4096], body[2048];
    uint32_t words[DOE_COMPILER_MAX_WORDS];
    DoeCompilerResult result;
    const char statement[] = "out[0u+1u]=src[1u-1u]+src[4294967295u+1u];";
    body[0] = '\0';
    for (unsigned count = 1u; count <= 17u; ++count) {
        strcat(body, statement);
        const size_t length = shader(source, sizeof(source), body);
        if (count == 17u) {
            failure(source, length, DOE_COMPILER_INPUT_ERROR,
                    sizeof(header) - 1u + 16u * (sizeof(statement) - 1u));
        } else assert(compile(source, length, words, DOE_COMPILER_MAX_WORDS, &result) == DOE_COMPILER_OK);
    }
    const char branch[] = "if(gid.x==0u){";
    for (unsigned depth = 1u; depth <= 5u; ++depth) {
        body[0] = '\0';
        for (unsigned i = 0u; i < depth; ++i) strcat(body, branch);
        strcat(body, "return;");
        for (unsigned i = 0u; i < depth; ++i) strcat(body, "}");
        const size_t length = shader(source, sizeof(source), body);
        if (depth == 5u) failure(source, length, DOE_COMPILER_INPUT_ERROR,
            sizeof(header) - 1u + 4u * (sizeof(branch) - 1u));
        else assert(compile(source, length, words, DOE_COMPILER_MAX_WORDS, &result) == DOE_COMPILER_OK);
    }
    const size_t length = shader(source, sizeof(source), "return;\r\nmissing[0u]=1u;");
    const size_t offset = (size_t)(strstr(source, "missing") - source);
    failure(source, length, DOE_COMPILER_SEMANTIC_ERROR, offset);
    assert(compile(source, length, words, DOE_COMPILER_MAX_WORDS, &result) == DOE_COMPILER_SEMANTIC_ERROR);
    assert(result.diagnostic.line == 2u && result.diagnostic.column == 1u);
}
static void publication_and_guard_pages(void) {
    char source[2048];
    const size_t length = shader(source, sizeof(source), control);
    char original[sizeof(source)];
    memcpy(original, source, length);
    uint32_t words[DOE_COMPILER_MAX_WORDS + 2u];
    memset(words, 0xa5, sizeof(words));
    DoeCompilerResult result;
    assert(compile(source, length, words + 1u, DOE_COMPILER_MAX_WORDS, &result) == DOE_COMPILER_OK);
    assert(memcmp(original, source, length) == 0);
    assert(words[0] == 0xa5a5a5a5u);
    for (size_t i = result.word_count + 1u; i < DOE_COMPILER_MAX_WORDS + 2u; ++i)
        assert(words[i] == 0xa5a5a5a5u);
    const size_t exact = result.word_count;
    memset(words, 0xa5, sizeof(words));
    assert(compile(source, length, words + 1u, exact - 1u, &result) == DOE_COMPILER_OUTPUT_TOO_SMALL);
    assert(result.word_count == 0u && result.reflection.binding_count == 0u);
    for (size_t i = 0; i < DOE_COMPILER_MAX_WORDS + 2u; ++i) assert(words[i] == 0xa5a5a5a5u);
    const long native_page = sysconf(_SC_PAGESIZE);
    assert(native_page > 0);
    const size_t page = (size_t)native_page;
    assert(length < page);
    char *source_pages = mmap(NULL, page * 2u, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(source_pages != MAP_FAILED);
    assert(mprotect(source_pages + page, page, PROT_NONE) == 0);
    char *bounded = source_pages + page - length;
    memcpy(bounded, source, length); /* Deliberately no terminator before the guard. */
    const size_t bytes = exact * sizeof(uint32_t);
    const size_t available = ((bytes + sizeof(uint32_t) + page - 1u) / page) * page;
    char *output_pages = mmap(NULL, available + page, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(output_pages != MAP_FAILED);
    assert(mprotect(output_pages + available, page, PROT_NONE) == 0);
    uint32_t *bounded_output = (uint32_t *)(void *)(output_pages + available - bytes);
    bounded_output[-1] = 0xabcddcbau;
    assert(compile(bounded, length, bounded_output, exact, &result) == DOE_COMPILER_OK);
    assert(result.word_count == exact && bounded_output[-1] == 0xabcddcbau);
    assert(memcmp(bounded, original, length) == 0);
    assert(munmap(source_pages, page * 2u) == 0);
    assert(munmap(output_pages, available + page) == 0);
}
static void *parallel(void *unused) {
    (void)unused;
    char source[2048];
    const size_t length = shader(source, sizeof(source), control);
    uint32_t initial[DOE_COMPILER_MAX_WORDS], words[DOE_COMPILER_MAX_WORDS];
    DoeCompilerResult first, result;
    assert(compile(source, length, initial, DOE_COMPILER_MAX_WORDS, &first) == DOE_COMPILER_OK);
    for (unsigned i = 0u; i < 128u; ++i) {
        assert(compile(source, length, words, DOE_COMPILER_MAX_WORDS, &result) == DOE_COMPILER_OK);
        assert(memcmp(&first, &result, sizeof(first)) == 0);
        assert(memcmp(initial, words, first.word_count * sizeof(*words)) == 0);
    }
    return NULL;
}
static void mutations(void) {
    char source[2048], mutated[2048];
    const size_t length = shader(source, sizeof(source), control);
    const unsigned char replacements[] = {0u, ' ', ';', '=', '<', '!', '-', '+', '}', '0', 'x', 255u};
    uint32_t words[DOE_COMPILER_MAX_WORDS];
    DoeCompilerResult result;
    for (size_t offset = sizeof(header) - 1u; offset < length; ++offset) {
        for (size_t j = 0u; j < sizeof(replacements); ++j) {
            memcpy(mutated, source, length);
            mutated[offset] = (char)replacements[j];
            memset(words, 0xa5, sizeof(words));
            const DoeCompilerStatus status = compile(mutated, length, words, DOE_COMPILER_MAX_WORDS, &result);
            assert(result.diagnostic.status == status);
            if (status != DOE_COMPILER_OK) {
                assert(result.word_count == 0u && result.reflection.binding_count == 0u);
                assert(result.diagnostic.offset <= length);
                for (size_t i = 0; i < DOE_COMPILER_MAX_WORDS; ++i) assert(words[i] == 0xa5a5a5a5u);
            } else {
                assert(result.word_count > 0u && result.word_count <= DOE_COMPILER_MAX_WORDS);
                for (size_t i = result.word_count; i < DOE_COMPILER_MAX_WORDS; ++i)
                    assert(words[i] == 0xa5a5a5a5u);
            }
        }
    }
}
int main(void) {
    limits();
    publication_and_guard_pages();
    mutations();
    pthread_t threads[8];
    for (size_t i = 0; i < 8u; ++i) assert(pthread_create(&threads[i], NULL, parallel, NULL) == 0);
    for (size_t i = 0; i < 8u; ++i) assert(pthread_join(threads[i], NULL) == 0);
    puts("Control API: 16/17 statements, 4/5 nesting, original offsets, atomic publication, guard pages, mutations and 1024 parallel calls passed");
    return 0;
}
