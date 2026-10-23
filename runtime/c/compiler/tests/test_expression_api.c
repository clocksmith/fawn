/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#define _GNU_SOURCE
#include "../include/doe_compiler.h"
#include <assert.h>
#include <fenv.h>
#include <locale.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static const char source[] =
    "@group(0) @binding(0) var<storage,read_write> data:array<f32>;"
    "@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) gid:vec3u){"
    "let lane:u32=(gid.x);let snapshot=data[lane];data[lane]=0.1;"
    "if(lane<2u){let x:f32=max(snapshot*0.2f,0.00000001);data[lane]=x-16777217.0;}"
    "else{let x=33554435.0;data[lane]=(snapshot+1.0)*x;return;}"
    "data[lane]=snapshot;return;}";

static DoeCompilerStatus compile(const char *text, size_t length, uint32_t *words,
                                  size_t capacity, DoeCompilerResult *result) {
    return doe_compiler_compile(DOE_COMPILER_ABI_VERSION, text, length, words,
                                capacity, result, sizeof(*result));
}
static void check_failure(const char *text, size_t length) {
    uint32_t words[DOE_COMPILER_MAX_WORDS];
    memset(words, 0xa5, sizeof(words));
    DoeCompilerResult result;
    const DoeCompilerStatus status = compile(text, length, words, DOE_COMPILER_MAX_WORDS, &result);
    assert(status != DOE_COMPILER_OK);
    assert(result.word_count == 0u && result.diagnostic.status == status);
    const DoeCompilerReflection empty = {0};
    assert(memcmp(&result.reflection, &empty, sizeof(empty)) == 0);
    assert(result.diagnostic.offset <= length && result.diagnostic.message[0] != '\0');
    for (size_t i = 0u; i < DOE_COMPILER_MAX_WORDS; ++i) assert(words[i] == 0xa5a5a5a5u);
}
static void ambient_numeric_state(void) {
    uint32_t baseline[DOE_COMPILER_MAX_WORDS], words[DOE_COMPILER_MAX_WORDS];
    DoeCompilerResult first, result;
    assert(fesetround(FE_TONEAREST) == 0);
    assert(setlocale(LC_NUMERIC, "C") != NULL);
    assert(compile(source, sizeof(source) - 1u, baseline, DOE_COMPILER_MAX_WORDS, &first) == DOE_COMPILER_OK);
    const char *const locales[] = {"C", "C.utf8", "de_DE.utf8", "fr_FR.utf8"};
    const int modes[] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
    for (size_t i = 0u; i < sizeof(locales) / sizeof(locales[0]); ++i) {
        if (setlocale(LC_NUMERIC, locales[i]) == NULL) {
            printf("Numeric locale unavailable: %s\n", locales[i]);
            continue;
        }
        for (size_t j = 0u; j < sizeof(modes) / sizeof(modes[0]); ++j) {
            assert(fesetround(modes[j]) == 0);
            assert(compile(source, sizeof(source) - 1u, words, DOE_COMPILER_MAX_WORDS, &result) == DOE_COMPILER_OK);
            assert(memcmp(&first, &result, sizeof(first)) == 0);
            assert(memcmp(baseline, words, first.word_count * sizeof(*words)) == 0);
            assert(fegetround() == modes[j]);
        }
    }
    assert(fesetround(FE_TONEAREST) == 0);
    assert(setlocale(LC_NUMERIC, "C") != NULL);
}
static void guarded_extent_and_capacity(void) {
    uint32_t words[DOE_COMPILER_MAX_WORDS];
    DoeCompilerResult result;
    assert(compile(source, sizeof(source) - 1u, words, DOE_COMPILER_MAX_WORDS, &result) == DOE_COMPILER_OK);
    const size_t count = result.word_count;
    const long native_page = sysconf(_SC_PAGESIZE);
    assert(native_page > 0);
    const size_t page = (size_t)native_page;
    assert(sizeof(source) < page);
    char *input = mmap(NULL, page * 2u, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(input != MAP_FAILED && mprotect(input + page, page, PROT_NONE) == 0);
    char *bounded = input + page - (sizeof(source) - 1u);
    memcpy(bounded, source, sizeof(source) - 1u);
    const size_t bytes = count * sizeof(uint32_t);
    const size_t span = ((bytes + sizeof(uint32_t) + page - 1u) / page) * page;
    char *output = mmap(NULL, span + page, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(output != MAP_FAILED && mprotect(output + span, page, PROT_NONE) == 0);
    uint32_t *guarded = (uint32_t *)(void *)(output + span - bytes);
    guarded[-1] = 0x1234abcdu;
    assert(compile(bounded, sizeof(source) - 1u, guarded, count, &result) == DOE_COMPILER_OK);
    assert(memcmp(words, guarded, bytes) == 0 && guarded[-1] == 0x1234abcdu);
    for (size_t capacity = 0u; capacity < count; ++capacity) {
        memset(words, 0xa5, sizeof(words));
        assert(compile(bounded, sizeof(source) - 1u, words, capacity, &result) == DOE_COMPILER_OUTPUT_TOO_SMALL);
        assert(result.word_count == 0u && result.reflection.binding_count == 0u);
        for (size_t i = 0u; i < DOE_COMPILER_MAX_WORDS; ++i) assert(words[i] == 0xa5a5a5a5u);
    }
    assert(memcmp(bounded, source, sizeof(source) - 1u) == 0);
    assert(munmap(input, page * 2u) == 0 && munmap(output, span + page) == 0);
}
static void mutations(void) {
    char changed[sizeof(source)];
    const unsigned char replacements[] = {0u, ' ', '.', '*', '+', '-', 'e', 'f', 'u', '(', ')', '}', 255u};
    uint32_t words[DOE_COMPILER_MAX_WORDS];
    DoeCompilerResult result;
    for (size_t i = 0u; i < sizeof(source) - 1u; ++i) {
        check_failure(source, i); /* No trailing whitespace: all prefixes are incomplete. */
        for (size_t j = 0u; j < sizeof(replacements); ++j) {
            memcpy(changed, source, sizeof(source));
            changed[i] = (char)replacements[j];
            const DoeCompilerStatus status = compile(changed, sizeof(source) - 1u, words,
                                                     DOE_COMPILER_MAX_WORDS, &result);
            if (status != DOE_COMPILER_OK) check_failure(changed, sizeof(source) - 1u);
            else assert(result.word_count > 0u && result.word_count <= DOE_COMPILER_MAX_WORDS);
        }
    }
}
static void *parallel(void *unused) {
    (void)unused;
    uint32_t baseline[DOE_COMPILER_MAX_WORDS], words[DOE_COMPILER_MAX_WORDS];
    DoeCompilerResult first, result;
    assert(compile(source, sizeof(source) - 1u, baseline, DOE_COMPILER_MAX_WORDS, &first) == DOE_COMPILER_OK);
    const int modes[] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
    for (unsigned i = 0u; i < 128u; ++i) {
        assert(fesetround(modes[i % 4u]) == 0);
        assert(compile(source, sizeof(source) - 1u, words, DOE_COMPILER_MAX_WORDS, &result) == DOE_COMPILER_OK);
        assert(memcmp(&first, &result, sizeof(first)) == 0);
        assert(memcmp(words, baseline, first.word_count * sizeof(*words)) == 0);
    }
    return NULL;
}
int main(void) {
    ambient_numeric_state();
    guarded_extent_and_capacity();
    mutations();
    pthread_t threads[8];
    for (size_t i = 0u; i < 8u; ++i) assert(pthread_create(&threads[i], NULL, parallel, NULL) == 0);
    for (size_t i = 0u; i < 8u; ++i) assert(pthread_join(threads[i], NULL) == 0);
    puts("Expression API: locale/rounding modes, guarded extents, every short capacity, byte mutations, prefixes and 1024 parallel calls passed");
    return 0;
}
