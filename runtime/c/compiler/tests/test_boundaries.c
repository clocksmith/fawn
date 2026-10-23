/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../include/doe_compiler.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static const char seed[] =
    "@group(0) @binding(0) var<storage,read_write> data:array<u32>;"
    "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) gid:vec3u){"
    "data[gid.x]=data[gid.x]+1u;}";

static DoeCompilerStatus compile_source(const char *source, size_t length,
                                        uint32_t *words, DoeCompilerResult *result) {
    return doe_compiler_compile(DOE_COMPILER_ABI_VERSION, source, length,
                                words, DOE_COMPILER_MAX_WORDS, result, sizeof(*result));
}

static void check_result_failure(const DoeCompilerResult *result,
                                 const uint32_t *words, DoeCompilerStatus status) {
    const DoeCompilerReflection empty = {0};
    assert(status != DOE_COMPILER_OK);
    assert(result->word_count == 0u);
    assert(memcmp(&result->reflection, &empty, sizeof(empty)) == 0);
    assert(result->diagnostic.status == status);
    assert(result->diagnostic.message[0] != '\0');
    for (size_t i = 0; i < DOE_COMPILER_MAX_WORDS; ++i)
        assert(words[i] == 0xa5a5a5a5u);
}

static void check_abi_mismatch(void) {
    uint32_t words[DOE_COMPILER_MAX_WORDS];
    DoeCompilerResult result, original;
    memset(words, 0xa5, sizeof(words));
    memset(&result, 0xbb, sizeof(result));
    memcpy(&original, &result, sizeof(result));
    const uint32_t versions[] = {0u, DOE_COMPILER_ABI_VERSION + 1u, UINT32_MAX};
    for (size_t i = 0; i < sizeof(versions) / sizeof(versions[0]); ++i) {
        assert(doe_compiler_compile(versions[i], NULL, SIZE_MAX, words,
            DOE_COMPILER_MAX_WORDS, &result, sizeof(result)) == DOE_COMPILER_ABI_MISMATCH);
        assert(memcmp(&result, &original, sizeof(result)) == 0);
    }
    const size_t sizes[] = {0u, sizeof(result) - 1u, sizeof(result) + 1u, SIZE_MAX};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        assert(doe_compiler_compile(DOE_COMPILER_ABI_VERSION, NULL, SIZE_MAX, words,
            DOE_COMPILER_MAX_WORDS, &result, sizes[i]) == DOE_COMPILER_ABI_MISMATCH);
        assert(memcmp(&result, &original, sizeof(result)) == 0);
    }
    assert(doe_compiler_compile(DOE_COMPILER_ABI_VERSION, seed, sizeof(seed) - 1u,
        words, DOE_COMPILER_MAX_WORDS, NULL, sizeof(result)) == DOE_COMPILER_INPUT_ERROR);
    for (size_t i = 0; i < DOE_COMPILER_MAX_WORDS; ++i)
        assert(words[i] == 0xa5a5a5a5u);
}

static void check_names(void) {
    char name[DOE_COMPILER_MAX_NAME_BYTES + 2u];
    char source[2048];
    uint32_t words[DOE_COMPILER_MAX_WORDS];
    DoeCompilerResult result;
    for (size_t length = DOE_COMPILER_MAX_NAME_BYTES;
         length <= DOE_COMPILER_MAX_NAME_BYTES + 1u; ++length) {
        memset(name, 'v', length);
        name[length] = '\0';
        const int count = snprintf(source, sizeof(source),
            "@group(3) @binding(31) var<storage,read_write> %s:array<u32>;"
            "@compute @workgroup_size(256) fn main(@builtin(global_invocation_id) gid:vec3u){"
            "%s[gid.x]=%s[gid.x]+1u;}", name, name, name);
        assert(count > 0 && (size_t)count < sizeof(source));
        memset(words, 0xa5, sizeof(words));
        const DoeCompilerStatus status = compile_source(source, (size_t)count, words, &result);
        if (length == DOE_COMPILER_MAX_NAME_BYTES) {
            assert(status == DOE_COMPILER_OK);
            assert(strcmp(result.reflection.bindings[0].name, name) == 0);
        } else check_result_failure(&result, words, status);
    }
}

static void check_bindings(void) {
    char source[4096];
    uint32_t words[DOE_COMPILER_MAX_WORDS];
    DoeCompilerResult result;
    for (unsigned count = 0; count <= DOE_COMPILER_MAX_BINDINGS + 1u; ++count) {
        size_t length = 0;
        for (unsigned i = 0; i < count; ++i) {
            const int written = snprintf(source + length, sizeof(source) - length,
                "@binding(%u) @group(%u) var<storage,read_write> buffer%u:array<u32>;",
                31u - i, i % 4u, i);
            assert(written > 0 && (size_t)written < sizeof(source) - length);
            length += (size_t)written;
        }
        const int written = snprintf(source + length, sizeof(source) - length,
            "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) gid:vec3u){"
            "buffer0[gid.x]=buffer0[gid.x]+1u;}");
        assert(written > 0 && (size_t)written < sizeof(source) - length);
        length += (size_t)written;
        memset(words, 0xa5, sizeof(words));
        const DoeCompilerStatus status = compile_source(source, length, words, &result);
        if (count == 0u || count > DOE_COMPILER_MAX_BINDINGS)
            check_result_failure(&result, words, status);
        else {
            assert(status == DOE_COMPILER_OK);
            assert(result.reflection.binding_count == count);
            for (unsigned i = 0; i < count; ++i) {
                char name[32];
                (void)snprintf(name, sizeof(name), "buffer%u", i);
                const DoeCompilerBinding *binding = &result.reflection.bindings[i];
                assert(strcmp(binding->name, name) == 0);
                assert(binding->group == i % 4u && binding->binding == 31u - i);
                assert(binding->scalar == DOE_COMPILER_U32);
                assert(binding->access == DOE_COMPILER_READ_WRITE);
                assert(binding->element_stride == 4u && binding->runtime_sized == 1u);
            }
        }
    }
}

typedef struct { unsigned index; } Worker;
static void *parallel_compile(void *context) {
    const Worker *worker = context;
    char source[1024];
    const int length = snprintf(source, sizeof(source),
        "@group(%u) @binding(%u) var<storage,read_write> data%u:array<u32>;"
        "@compute @workgroup_size(%u) fn task%u(@builtin(global_invocation_id) gid:vec3u){"
        "data%u[gid.x]=data%u[gid.x]+%uu;}", worker->index % 4u, worker->index,
        worker->index, worker->index + 1u, worker->index, worker->index,
        worker->index, worker->index);
    assert(length > 0 && (size_t)length < sizeof(source));
    uint32_t first[DOE_COMPILER_MAX_WORDS], words[DOE_COMPILER_MAX_WORDS];
    DoeCompilerResult first_result, result;
    assert(compile_source(source, (size_t)length, first, &first_result) == DOE_COMPILER_OK);
    for (unsigned i = 0; i < 256u; ++i) {
        assert(compile_source(source, (size_t)length, words, &result) == DOE_COMPILER_OK);
        assert(memcmp(&result, &first_result, sizeof(result)) == 0);
        assert(memcmp(words, first, result.word_count * sizeof(*words)) == 0);
        assert(result.reflection.bindings[0].binding == worker->index);
        assert(result.reflection.workgroup_size[0] == worker->index + 1u);
    }
    return NULL;
}

static void check_mutations(void) {
    char source[sizeof(seed)];
    uint32_t words[DOE_COMPILER_MAX_WORDS];
    DoeCompilerResult result;
    const unsigned char replacements[] = {0u, 1u, '\n', '\r', ' ', '0', 'u', '@', '+', ';', 127u, 255u};
    for (size_t offset = 0; offset < sizeof(seed) - 1u; ++offset) {
        for (size_t j = 0; j < sizeof(replacements); ++j) {
            memcpy(source, seed, sizeof(seed));
            source[offset] = (char)replacements[j];
            memset(words, 0xa5, sizeof(words));
            const DoeCompilerStatus status = compile_source(source, sizeof(seed) - 1u, words, &result);
            if (status != DOE_COMPILER_OK) {
                check_result_failure(&result, words, status);
                assert(result.diagnostic.offset <= sizeof(seed) - 1u);
            } else {
                assert(result.word_count > 5u && result.word_count <= DOE_COMPILER_MAX_WORDS);
                for (size_t i = result.word_count; i < DOE_COMPILER_MAX_WORDS; ++i)
                    assert(words[i] == 0xa5a5a5a5u);
            }
        }
    }
}

int main(void) {
    check_abi_mismatch();
    check_names();
    check_bindings();
    check_mutations();
    pthread_t threads[8];
    Worker workers[8];
    for (unsigned i = 0; i < 8u; ++i) {
        workers[i].index = i;
        assert(pthread_create(&threads[i], NULL, parallel_compile, &workers[i]) == 0);
    }
    for (unsigned i = 0; i < 8u; ++i)
        assert(pthread_join(threads[i], NULL) == 0);
    puts("API boundaries: ABI preservation, name/binding limits, seed mutations and 2048 parallel compiles passed");
    return 0;
}
