/* SPDX-License-Identifier: MIT
 * Independent frozen-contract M5 native boundary evaluator. */
#define _POSIX_C_SOURCE 200809L
#include "doe_compiler_v2.h"
#include <errno.h>
#include <pthread.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define WORDS DOE_COMPILER_MAX_WORDS
#define SENTINEL UINT32_C(0xa55a36c9)
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL native_m5:%d: %s\n", __LINE__, #condition); \
    exit(1); } } while (0)

_Static_assert(sizeof(DoeCompilerBinding) == 88u, "ABI1 binding layout");
_Static_assert(sizeof(DoeCompilerReflection) == 784u, "ABI1 reflection layout");
_Static_assert(offsetof(DoeCompilerBinding, scalar) == 72u, "ABI1 scalar offset");
_Static_assert(offsetof(DoeCompilerBinding, access) == 76u, "ABI1 access offset");
_Static_assert(offsetof(DoeCompilerReflection, bindings) == 80u, "ABI1 bindings offset");
_Static_assert(sizeof(DoeCompilerMemberV2) == 80u, "v2 member layout");
_Static_assert(sizeof(DoeCompilerStructV2) == 716u, "v2 struct layout");
_Static_assert(sizeof(DoeCompilerBindingV2) == 108u, "v2 binding layout");
_Static_assert(sizeof(DoeCompilerReflectionV2) == 6676u, "v2 reflection layout");
_Static_assert(offsetof(DoeCompilerBindingV2, scalar) == 80u, "v2 scalar offset");
_Static_assert(offsetof(DoeCompilerBindingV2, struct_index) == 104u, "v2 struct offset");
_Static_assert(offsetof(DoeCompilerReflectionV2, structs) == 948u, "v2 structs offset");
#if SIZE_MAX == UINT64_MAX
_Static_assert(sizeof(DoeCompilerResult) == 1008u, "ABI1 result layout");
_Static_assert(sizeof(DoeCompilerResultV2) == 6904u, "v2 result layout");
_Static_assert(offsetof(DoeCompilerResultV2, diagnostic) == 6688u, "v2 diagnostic offset");
#endif

static _Thread_local unsigned allocation_gate;
static atomic_uint allocation_calls;
static atomic_uint free_calls;
#ifdef WRAP_ALLOCATORS
void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);
void *__real_aligned_alloc(size_t alignment, size_t size);
int __real_posix_memalign(void **pointer, size_t alignment, size_t size);
void __real_free(void *pointer);
void *__wrap_malloc(size_t size);
void *__wrap_calloc(size_t count, size_t size);
void *__wrap_realloc(void *pointer, size_t size);
void *__wrap_aligned_alloc(size_t alignment, size_t size);
int __wrap_posix_memalign(void **pointer, size_t alignment, size_t size);
void __wrap_free(void *pointer);
void *__wrap_malloc(size_t size) {
    if (allocation_gate != 0u) (void)atomic_fetch_add(&allocation_calls, 1u);
    return __real_malloc(size);
}
void *__wrap_calloc(size_t count, size_t size) {
    if (allocation_gate != 0u) (void)atomic_fetch_add(&allocation_calls, 1u);
    return __real_calloc(count, size);
}
void *__wrap_realloc(void *pointer, size_t size) {
    if (allocation_gate != 0u) (void)atomic_fetch_add(&allocation_calls, 1u);
    return __real_realloc(pointer, size);
}
void *__wrap_aligned_alloc(size_t alignment, size_t size) {
    if (allocation_gate != 0u) (void)atomic_fetch_add(&allocation_calls, 1u);
    return __real_aligned_alloc(alignment, size);
}
int __wrap_posix_memalign(void **pointer, size_t alignment, size_t size) {
    if (allocation_gate != 0u) (void)atomic_fetch_add(&allocation_calls, 1u);
    return __real_posix_memalign(pointer, alignment, size);
}
void __wrap_free(void *pointer) {
    if (allocation_gate != 0u) (void)atomic_fetch_add(&free_calls, 1u);
    __real_free(pointer);
}
#endif

static const char storage_source[] =
    "@group(0) @binding(0) var<storage,read_write> data: array<u32>;\n"
    "@compute @workgroup_size(4) fn main(@builtin(global_invocation_id) "
    "gid: vec3u) { let saved=data[gid.x]; data[gid.x]=saved+1u; }";
static const char uniform_source[] =
    "struct Block { count:u32, scale:f32, unused:u32, }\n"
    "struct Spare { count:f32 }\n"
    "@group(2) @binding(5) var<uniform> params:Block;\n"
    "@group(0) @binding(1) var<storage,read_write> data:array<f32>;\n"
    "@compute @workgroup_size(4) fn main(@builtin(global_invocation_id) "
    "gid:vec3u) { if(gid.x<params.count) { let saved=params.scale; "
    "data[gid.x]=data[gid.x]*saved; } }";
static const char unused_struct_source[] =
    "struct Unused { count:u32 }\n"
    "@group(0) @binding(0) var<storage,read_write> data:array<u32>;\n"
    "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) "
    "gid:vec3u) { data[gid.x]=1u; }";
static DoeCompilerWorkspaceRequirements requirements;

static DoeCompilerStatus query(uint32_t version,
        DoeCompilerWorkspaceRequirements *output, size_t size) {
    allocation_gate = 1u;
    DoeCompilerStatus status = doe_compiler_workspace_requirements(
        version, output, size);
    allocation_gate = 0u;
    return status;
}
static DoeCompilerStatus call_v2(uint32_t version, void *workspace,
        size_t workspace_size, const char *source, size_t length,
        uint32_t *words, size_t capacity, DoeCompilerResultV2 *result,
        size_t result_size) {
    allocation_gate = 1u;
    DoeCompilerStatus status = doe_compiler_compile_v2(version, workspace,
        workspace_size, source, length, words, capacity, result, result_size);
    allocation_gate = 0u;
    return status;
}
static DoeCompilerStatus compile_v2(void *workspace, const char *source,
        size_t length, uint32_t *words, size_t capacity,
        DoeCompilerResultV2 *result) {
    return call_v2(2u, workspace, requirements.size_bytes, source, length,
        words, capacity, result, sizeof(*result));
}
static DoeCompilerStatus compile_v1(uint32_t version, const char *source,
        size_t length, uint32_t *words, size_t capacity,
        DoeCompilerResult *result, size_t result_size) {
    allocation_gate = 1u;
    DoeCompilerStatus status = doe_compiler_compile(version, source, length,
        words, capacity, result, result_size);
    allocation_gate = 0u;
    return status;
}
static void fill_words(uint32_t *words, size_t count) {
    for (size_t i = 0; i < count; ++i) words[i] = SENTINEL;
}
static void unchanged_words(const uint32_t *words, size_t count) {
    for (size_t i = 0; i < count; ++i) CHECK(words[i] == SENTINEL);
}
static void zero_bytes(const void *record, size_t size) {
    const unsigned char *bytes = record;
    for (size_t i = 0; i < size; ++i) CHECK(bytes[i] == 0u);
}
static void diagnostic_check(const DoeCompilerDiagnostic *diagnostic,
        DoeCompilerStatus status, const char *source, size_t length) {
    CHECK(diagnostic->status == status);
    CHECK(diagnostic->offset <= length);
    CHECK(memchr(diagnostic->message, '\0', sizeof(diagnostic->message)) != NULL);
    uint32_t line = 1u, column = 1u;
    if (source != NULL) {
        for (size_t i = 0; i < diagnostic->offset; ++i) {
            if (source[i] == '\r') { ++line; column = 1u; }
            else if (source[i] == '\n') {
                if (i == 0u || source[i-1u] != '\r') ++line;
                column = 1u;
            } else { ++column; }
        }
    }
    CHECK(diagnostic->line == line && diagnostic->column == column);
}
static void success(void *workspace) {
    uint32_t words[WORDS]; DoeCompilerResultV2 result;
    CHECK(compile_v2(workspace, uniform_source, sizeof(uniform_source)-1u,
        words, WORDS, &result) == DOE_COMPILER_OK);
    CHECK(result.word_count > 5u && result.word_count <= WORDS);
    CHECK(words[0] == UINT32_C(0x07230203));
}
static void failure(void *workspace, const char *source, size_t length,
        size_t capacity, DoeCompilerStatus expected) {
    uint32_t words[WORDS + 2u]; DoeCompilerResultV2 result;
    fill_words(words, WORDS + 2u); memset(&result, 0xcd, sizeof(result));
    DoeCompilerStatus status = compile_v2(workspace, source, length,
        words + 1, capacity, &result);
    CHECK(status == expected);
    CHECK(result.word_count == 0u);
    zero_bytes(&result.reflection, sizeof(result.reflection));
    unchanged_words(words, WORDS + 2u);
    diagnostic_check(&result.diagnostic, status, source, length);
    success(workspace);
}
static void query_checks(void) {
    CHECK(query(2u, &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    CHECK(requirements.size_bytes > 0u && requirements.alignment_bytes > 0u);
    CHECK(requirements.alignment_bytes <= alignof(max_align_t));
    CHECK((requirements.alignment_bytes & (requirements.alignment_bytes-1u)) == 0u);
    const uint32_t versions[] = {0u, 1u, 3u, UINT32_MAX};
    const size_t sizes[] = {0u, sizeof(requirements)-1u,
        sizeof(requirements)+1u, SIZE_MAX};
    DoeCompilerWorkspaceRequirements output, saved;
    memset(&output, 0xa7, sizeof(output)); memcpy(&saved, &output, sizeof(saved));
    for (size_t i = 0; i < sizeof(versions)/sizeof(versions[0]); ++i) {
        CHECK(query(versions[i], &output, sizeof(output)) == DOE_COMPILER_ABI_MISMATCH);
        CHECK(memcmp(&output, &saved, sizeof(output)) == 0);
    }
    for (size_t i = 0; i < sizeof(sizes)/sizeof(sizes[0]); ++i) {
        CHECK(query(2u, &output, sizes[i]) == DOE_COMPILER_ABI_MISMATCH);
        CHECK(memcmp(&output, &saved, sizeof(output)) == 0);
    }
    CHECK(query(2u, NULL, sizeof(output)) == DOE_COMPILER_INPUT_ERROR);
    printf("workspace_size=%zu workspace_alignment=%zu result_v1=%zu result_v2=%zu\n",
        requirements.size_bytes, requirements.alignment_bytes,
        sizeof(DoeCompilerResult), sizeof(DoeCompilerResultV2));
}
static void preflight_checks(void *workspace) {
    uint32_t words[WORDS]; DoeCompilerResultV2 result, saved_result;
    unsigned char *saved = malloc(requirements.size_bytes); CHECK(saved != NULL);
    const uint32_t versions[] = {0u, 1u, 3u, UINT32_MAX};
    const size_t sizes[] = {0u, sizeof(result)-1u, sizeof(result)+1u, SIZE_MAX};
    for (size_t i = 0; i < 8u; ++i) {
        memset(workspace, 0xb6, requirements.size_bytes);
        memcpy(saved, workspace, requirements.size_bytes);
        memset(&result, 0xcd, sizeof(result)); memcpy(&saved_result, &result, sizeof(result));
        fill_words(words, WORDS);
        CHECK(call_v2(i < 4u ? versions[i] : 2u, workspace,
            requirements.size_bytes, uniform_source, sizeof(uniform_source)-1u,
            words, WORDS, &result, i < 4u ? sizeof(result) : sizes[i-4u])
            == DOE_COMPILER_ABI_MISMATCH);
        CHECK(memcmp(workspace, saved, requirements.size_bytes) == 0);
        CHECK(memcmp(&result, &saved_result, sizeof(result)) == 0);
        unchanged_words(words, WORDS); success(workspace);
    }
    /* Result preflight wins over invalid workspace, without accessing either. */
    memset(workspace, 0xb6, requirements.size_bytes);
    memcpy(saved, workspace, requirements.size_bytes); fill_words(words, WORDS);
    CHECK(call_v2(2u, workspace, requirements.size_bytes, uniform_source,
        sizeof(uniform_source)-1u, words, WORDS, NULL, sizeof(result)) == DOE_COMPILER_INPUT_ERROR);
    CHECK(memcmp(workspace, saved, requirements.size_bytes) == 0);
    unchanged_words(words, WORDS); success(workspace);
    for (size_t i = 0; i < 4u; ++i) {
        memset(workspace, 0xb6, requirements.size_bytes);
        memcpy(saved, workspace, requirements.size_bytes); fill_words(words, WORDS);
        memset(&result, 0xcd, sizeof(result));
        void *chosen = i == 0u ? NULL : workspace;
        size_t size = requirements.size_bytes;
        if (i == 1u) size = 0u;
        if (i == 2u) size -= 1u;
        if (i == 3u) {
            if (requirements.alignment_bytes == 1u) continue;
            chosen = (unsigned char *)workspace + 1u;
        }
        CHECK(call_v2(2u, chosen, size, uniform_source, sizeof(uniform_source)-1u,
            words, WORDS, &result, sizeof(result)) == DOE_COMPILER_INPUT_ERROR);
        CHECK(memcmp(workspace, saved, requirements.size_bytes) == 0);
        CHECK(result.word_count == 0u && result.diagnostic.offset == 0u);
        zero_bytes(&result.reflection, sizeof(result.reflection));
        diagnostic_check(&result.diagnostic, DOE_COMPILER_INPUT_ERROR, uniform_source,
            sizeof(uniform_source)-1u);
        unchanged_words(words, WORDS); success(workspace);
    }
    free(saved);
}
static void reflection_checks(void *workspace) {
    uint32_t words[WORDS + 2u]; DoeCompilerResultV2 result;
    DoeCompilerReflectionV2 expected; memset(&expected, 0, sizeof(expected));
    strcpy(expected.entry_point, "main");
    expected.workgroup_size[0] = 4u; expected.workgroup_size[1] = 1u;
    expected.workgroup_size[2] = 1u; expected.binding_count = 2u;
    DoeCompilerBindingV2 *binding = &expected.bindings[0];
    strcpy(binding->name, "params"); binding->group = 2u; binding->binding = 5u;
    binding->kind = DOE_COMPILER_UNIFORM_BUFFER; binding->access = DOE_COMPILER_READ;
    binding->minimum_binding_size_bytes = 12u;
    binding->type_alignment_bytes = 4u; binding->address_space_alignment_bytes = 16u;
    binding = &expected.bindings[1]; strcpy(binding->name, "data");
    binding->binding = 1u; binding->kind = DOE_COMPILER_STORAGE_BUFFER;
    binding->access = DOE_COMPILER_READ_WRITE; binding->scalar = DOE_COMPILER_F32;
    binding->element_stride = 4u; binding->runtime_sized = 1u;
    binding->minimum_binding_size_bytes = 4u;
    binding->type_alignment_bytes = 4u; binding->address_space_alignment_bytes = 4u;
    binding->struct_index = DOE_COMPILER_NO_STRUCT;
    expected.struct_count = 2u;
    const char *type_names[] = {"Block", "Spare"};
    const char *member_names[] = {"count", "scale", "unused"};
    for (size_t i = 0; i < 2u; ++i) {
        DoeCompilerStructV2 *type = &expected.structs[i];
        strcpy(type->name, type_names[i]); type->member_count = i == 0u ? 3u : 1u;
        type->size_bytes = 4u * type->member_count; type->alignment_bytes = 4u;
        for (size_t j = 0; j < type->member_count; ++j) {
            DoeCompilerMemberV2 *member = &type->members[j];
            strcpy(member->name, member_names[j]);
            member->scalar = (i == 1u || j == 1u) ? DOE_COMPILER_F32 : DOE_COMPILER_U32;
            member->offset_bytes = 4u * (uint32_t)j;
            member->size_bytes = 4u; member->alignment_bytes = 4u;
        }
    }
    memset(&result, 0xcd, sizeof(result)); fill_words(words, WORDS+2u);
    CHECK(compile_v2(workspace, uniform_source, sizeof(uniform_source)-1u,
        words+1, WORDS, &result) == DOE_COMPILER_OK);
    CHECK(memcmp(&result.reflection, &expected, sizeof(expected)) == 0);
    CHECK(words[0] == SENTINEL); unchanged_words(words+1u+result.word_count,
        WORDS+1u-result.word_count);
    size_t exact = result.word_count;
    CHECK(compile_v2(workspace, uniform_source, sizeof(uniform_source)-1u,
        words+1, exact, &result) == DOE_COMPILER_OK);
    failure(workspace, uniform_source, sizeof(uniform_source)-1u, exact-1u,
        DOE_COMPILER_OUTPUT_TOO_SMALL);
    failure(workspace, uniform_source, sizeof(uniform_source)-1u, 0u,
        DOE_COMPILER_OUTPUT_TOO_SMALL);
    memset(&result, 0xcd, sizeof(result));
    CHECK(compile_v2(workspace, uniform_source, sizeof(uniform_source)-1u,
        NULL, 0u, &result) == DOE_COMPILER_OUTPUT_TOO_SMALL);
    zero_bytes(&result.reflection, sizeof(result.reflection)); success(workspace);
    CHECK(compile_v2(workspace, uniform_source, sizeof(uniform_source)-1u,
        NULL, 1u, &result) == DOE_COMPILER_INPUT_ERROR);
    zero_bytes(&result.reflection, sizeof(result.reflection)); success(workspace);
}
static void abi1_checks(void *workspace) {
    uint32_t first[WORDS], second[WORDS]; DoeCompilerResult old_result;
    DoeCompilerResultV2 new_result;
    fill_words(first, WORDS); fill_words(second, WORDS);
    CHECK(compile_v1(1u, storage_source, sizeof(storage_source)-1u,
        first, WORDS, &old_result, sizeof(old_result)) == DOE_COMPILER_OK);
    CHECK(compile_v2(workspace, storage_source, sizeof(storage_source)-1u,
        second, WORDS, &new_result) == DOE_COMPILER_OK);
    CHECK(old_result.word_count == new_result.word_count);
    CHECK(memcmp(first, second, old_result.word_count * sizeof(uint32_t)) == 0);
    const char *sources[] = {uniform_source, unused_struct_source};
    for (size_t i = 0; i < 2u; ++i) {
        fill_words(first, WORDS); memset(&old_result, 0xcd, sizeof(old_result));
        CHECK(compile_v1(1u, sources[i], strlen(sources[i]), first, WORDS,
            &old_result, sizeof(old_result)) == DOE_COMPILER_UNSUPPORTED);
        CHECK(old_result.word_count == 0u);
        zero_bytes(&old_result.reflection, sizeof(old_result.reflection));
        unchanged_words(first, WORDS);
        diagnostic_check(&old_result.diagnostic, DOE_COMPILER_UNSUPPORTED,
            sources[i], strlen(sources[i]));
        CHECK(compile_v1(1u, storage_source, sizeof(storage_source)-1u,
            first, WORDS, &old_result, sizeof(old_result)) == DOE_COMPILER_OK);
    }
    memset(&old_result, 0xcd, sizeof(old_result));
    DoeCompilerResult saved; memcpy(&saved, &old_result, sizeof(saved));
    fill_words(first, WORDS);
    CHECK(compile_v1(2u, storage_source, sizeof(storage_source)-1u,
        first, WORDS, &old_result, sizeof(old_result)) == DOE_COMPILER_ABI_MISMATCH);
    CHECK(memcmp(&saved, &old_result, sizeof(saved)) == 0);
    unchanged_words(first, WORDS);
}

typedef struct {
    void *allocation;
    size_t allocation_size;
    size_t page_size;
    unsigned char *pointer;
    unsigned char *forbidden;
} Guarded;
static Guarded guarded(size_t bytes) {
    long page_value = sysconf(_SC_PAGESIZE); CHECK(page_value > 0);
    size_t page = (size_t)page_value;
    size_t rounded = ((bytes+page-1u)/page)*page;
    if (rounded == 0u) rounded = page;
    Guarded span = {NULL, rounded+2u*page, page, NULL, NULL};
    CHECK(posix_memalign(&span.allocation, page, span.allocation_size) == 0);
    unsigned char *allocation = span.allocation;
    CHECK(mprotect(allocation, page, PROT_NONE) == 0);
    span.forbidden = allocation+page+rounded;
    CHECK(mprotect(span.forbidden, page, PROT_NONE) == 0);
    span.pointer = span.forbidden-bytes;
    return span;
}
static void release_guard(Guarded *span) {
    CHECK(mprotect(span->allocation, span->allocation_size,
        PROT_READ|PROT_WRITE) == 0);
    free(span->allocation);
}
static void guard_checks(void *workspace) {
    const size_t length = sizeof(uniform_source)-1u;
    Guarded source = guarded(DOE_COMPILER_MAX_SOURCE_BYTES+1u);
    unsigned char *source_end = source.forbidden;
    memcpy(source_end-length, uniform_source, length);
    uint32_t words[WORDS]; DoeCompilerResultV2 result;
    CHECK(compile_v2(workspace, (char *)source_end-length, length,
        words, WORDS, &result) == DOE_COMPILER_OK);
    size_t count = result.word_count;
    Guarded output = guarded(count*sizeof(uint32_t));
    Guarded result_span = guarded(sizeof(result));
    Guarded work = guarded(requirements.size_bytes);
    CHECK((uintptr_t)work.pointer % requirements.alignment_bytes == 0u);
    CHECK(compile_v2(work.pointer, (char *)source_end-length, length,
        (uint32_t *)output.pointer, count, (DoeCompilerResultV2 *)result_span.pointer)
        == DOE_COMPILER_OK);
    /* An exact-size result/output/workspace ends at an inaccessible page. */
    memset(output.pointer, 0xa5, count*sizeof(uint32_t));
    CHECK(compile_v2(work.pointer, (char *)source_end-length, length,
        (uint32_t *)output.pointer+1, count-1u, &result) == DOE_COMPILER_OUTPUT_TOO_SMALL);
    for (size_t i = 0; i < count*sizeof(uint32_t); ++i) CHECK(output.pointer[i] == 0xa5u);
    success(work.pointer);
    for (size_t n = 0; n < length; ++n) {
        memcpy(source_end-n, uniform_source, n);
        fill_words(words, WORDS);
        DoeCompilerStatus status = compile_v2(work.pointer, (char *)source_end-n,
            n, words, WORDS, &result);
        CHECK(status != DOE_COMPILER_OK && result.word_count == 0u);
        zero_bytes(&result.reflection, sizeof(result.reflection));
        unchanged_words(words, WORDS);
        diagnostic_check(&result.diagnostic, status, (char *)source_end-n, n);
        success(work.pointer);
    }
    for (size_t n = DOE_COMPILER_MAX_SOURCE_BYTES-1u;
            n <= DOE_COMPILER_MAX_SOURCE_BYTES+1u; ++n) {
        memset(source_end-n, ' ', n); memcpy(source_end-n, uniform_source, length);
        fill_words(words, WORDS);
        DoeCompilerStatus status = compile_v2(work.pointer, (char *)source_end-n,
            n, words, WORDS, &result);
        if (n <= DOE_COMPILER_MAX_SOURCE_BYTES) CHECK(status == DOE_COMPILER_OK);
        else {
            CHECK(status == DOE_COMPILER_INPUT_ERROR);
            CHECK(result.word_count == 0u); unchanged_words(words, WORDS);
            zero_bytes(&result.reflection, sizeof(result.reflection));
        }
        success(work.pointer);
    }
    CHECK(call_v2(1u, work.forbidden, requirements.size_bytes,
        (char *)source.forbidden, 1u, (uint32_t *)output.forbidden, 1u,
        (DoeCompilerResultV2 *)result_span.forbidden, sizeof(result)) == DOE_COMPILER_ABI_MISMATCH);
    CHECK(call_v2(2u, work.forbidden, requirements.size_bytes,
        (char *)source.forbidden, 1u, (uint32_t *)output.forbidden, 1u,
        (DoeCompilerResultV2 *)result_span.forbidden, sizeof(result)-1u) == DOE_COMPILER_ABI_MISMATCH);
    CHECK(query(1u, (DoeCompilerWorkspaceRequirements *)result_span.forbidden,
        sizeof(requirements)) == DOE_COMPILER_ABI_MISMATCH);
    CHECK(query(2u, (DoeCompilerWorkspaceRequirements *)result_span.forbidden,
        sizeof(requirements)-1u) == DOE_COMPILER_ABI_MISMATCH);
    /* A short workspace must be rejected before any workspace read/write. */
    fill_words(words, WORDS); memset(&result, 0xcd, sizeof(result));
    CHECK(call_v2(2u, work.forbidden, requirements.size_bytes-1u,
        uniform_source, length, words, WORDS, &result, sizeof(result)) == DOE_COMPILER_INPUT_ERROR);
    unchanged_words(words, WORDS);
    zero_bytes(&result.reflection, sizeof(result.reflection)); success(work.pointer);
    release_guard(&work); release_guard(&result_span);
    release_guard(&output); release_guard(&source);
}
static void failure_checks(void *workspace) {
    static const char duplicate[] =
        "struct P{n:u32} @group(0) @binding(0) var<uniform> p:P; "
        "@group(0) @binding(0) var<storage,read_write> d:array<u32>; "
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) "
        "g:vec3u){d[g.x]=p.n;}";
    static const char unsupported[] =
        "struct P{n:array<u32,4>} @group(0) @binding(0) var<uniform> p:P; "
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) "
        "g:vec3u){}";
    failure(workspace, NULL, 1u, WORDS, DOE_COMPILER_INPUT_ERROR);
    failure(workspace, "", 0u, WORDS, DOE_COMPILER_INPUT_ERROR);
    failure(workspace, duplicate, sizeof(duplicate)-1u, WORDS, DOE_COMPILER_SEMANTIC_ERROR);
    failure(workspace, unsupported, sizeof(unsupported)-1u, WORDS, DOE_COMPILER_UNSUPPORTED);
    unsigned char *oversized = malloc(requirements.size_bytes+96u); CHECK(oversized != NULL);
    memset(oversized, 0x9b, requirements.size_bytes+96u);
    uint32_t words[WORDS+32u]; DoeCompilerResultV2 result;
    fill_words(words, WORDS+32u);
    CHECK(call_v2(2u, oversized, requirements.size_bytes+64u,
        uniform_source, sizeof(uniform_source)-1u, words, WORDS+32u,
        &result, sizeof(result)) == DOE_COMPILER_OK);
    for (size_t i = requirements.size_bytes+64u; i < requirements.size_bytes+96u; ++i)
        CHECK(oversized[i] == 0x9bu);
    unchanged_words(words+result.word_count, WORDS+32u-result.word_count);
    success(oversized); free(oversized);
}
static uint32_t random_next(uint32_t *state) {
    *state ^= *state << 13; *state ^= *state >> 17; *state ^= *state << 5;
    return *state;
}
static void fuzz_checks(void *workspace) {
    uint32_t random_state = UINT32_C(0xd0e5005);
    const size_t seed_length = sizeof(uniform_source)-1u;
    for (unsigned iteration = 0u; iteration < 12000u; ++iteration) {
        size_t length = iteration % 3u == 0u ?
            random_next(&random_state) % 512u : seed_length;
        char *source = malloc(length == 0u ? 1u : length); CHECK(source != NULL);
        if (iteration % 3u == 0u) {
            for (size_t i = 0; i < length; ++i) source[i] = (char)random_next(&random_state);
        } else {
            memcpy(source, uniform_source, length);
            unsigned mutations = 1u+random_next(&random_state)%8u;
            for (unsigned i = 0u; i < mutations; ++i)
                source[random_next(&random_state)%length] = (char)random_next(&random_state);
        }
        size_t capacity = random_next(&random_state) % (WORDS+1u);
        uint32_t words[WORDS+2u]; DoeCompilerResultV2 result;
        fill_words(words, WORDS+2u); memset(&result, 0xcd, sizeof(result));
        DoeCompilerStatus status = compile_v2(workspace, source, length,
            words+1, capacity, &result);
        CHECK(status >= DOE_COMPILER_OK && status <= DOE_COMPILER_ABI_MISMATCH);
        CHECK(words[0] == SENTINEL);
        unchanged_words(words+1u+capacity, WORDS+1u-capacity);
        if (status == DOE_COMPILER_OK) {
            CHECK(result.word_count > 5u && result.word_count <= capacity);
            unchanged_words(words+1u+result.word_count, WORDS+1u-result.word_count);
        } else {
            CHECK(result.word_count == 0u);
            zero_bytes(&result.reflection, sizeof(result.reflection));
            unchanged_words(words, WORDS+2u);
            diagnostic_check(&result.diagnostic, status, source, length);
        }
        free(source); success(workspace);
    }
}
static void *thread_run(void *unused) {
    (void)unused;
    void *workspace = malloc(requirements.size_bytes); CHECK(workspace != NULL);
    for (unsigned iteration = 0u; iteration < 200u; ++iteration) {
        success(workspace);
        failure(workspace, uniform_source, sizeof(uniform_source)-1u,
            1u, DOE_COMPILER_OUTPUT_TOO_SMALL);
        uint32_t words[WORDS]; DoeCompilerResult result;
        CHECK(compile_v1(1u, storage_source, sizeof(storage_source)-1u,
            words, WORDS, &result, sizeof(result)) == DOE_COMPILER_OK);
    }
    free(workspace); return NULL;
}
static void thread_checks(void) {
    pthread_t threads[4];
    for (size_t i = 0; i < 4u; ++i) CHECK(pthread_create(&threads[i], NULL, thread_run, NULL) == 0);
    for (size_t i = 0; i < 4u; ++i) CHECK(pthread_join(threads[i], NULL) == 0);
}
static void witness_check(void *workspace, const char *path, const char *output_path) {
    FILE *file = fopen(path, "rb"); CHECK(file != NULL);
    char *source = malloc(DOE_COMPILER_MAX_SOURCE_BYTES+1u); CHECK(source != NULL);
    size_t length = fread(source, 1u, DOE_COMPILER_MAX_SOURCE_BYTES+1u, file);
    CHECK(ferror(file) == 0 && length <= DOE_COMPILER_MAX_SOURCE_BYTES);
    CHECK(fclose(file) == 0);
    uint32_t words[WORDS]; DoeCompilerResultV2 result;
    CHECK(compile_v2(workspace, source, length, words, WORDS, &result) == DOE_COMPILER_OK);
    file = fopen(output_path, "wb"); CHECK(file != NULL);
    for (size_t i = 0; i < result.word_count; ++i) {
        unsigned char little[4];
        for (size_t j = 0; j < 4u; ++j) little[j] = (unsigned char)(words[i] >> (8u*j));
        CHECK(fwrite(little, 1u, 4u, file) == 4u);
    }
    CHECK(fclose(file) == 0); free(source);
}
static void corpus_check(void *workspace, const char *path) {
    FILE *file = fopen(path, "rb"); CHECK(file != NULL);
    char *source = malloc(DOE_COMPILER_MAX_SOURCE_BYTES+1u); CHECK(source != NULL);
    size_t length = fread(source, 1u, DOE_COMPILER_MAX_SOURCE_BYTES+1u, file);
    CHECK(ferror(file) == 0); CHECK(fclose(file) == 0);
    uint32_t words[WORDS+2u]; DoeCompilerResultV2 result;
    fill_words(words, WORDS+2u); memset(&result, 0xcd, sizeof(result));
    DoeCompilerStatus status = compile_v2(workspace, source, length, words+1, WORDS, &result);
    CHECK(words[0] == SENTINEL && words[WORDS+1u] == SENTINEL);
    if (status == DOE_COMPILER_OK) {
        CHECK(result.word_count > 5u && result.word_count <= WORDS);
        unchanged_words(words+1u+result.word_count, WORDS+1u-result.word_count);
    } else {
        CHECK(result.word_count == 0u);
        zero_bytes(&result.reflection, sizeof(result.reflection));
        unchanged_words(words, WORDS+2u);
        diagnostic_check(&result.diagnostic, status, source, length);
    }
    free(source); success(workspace);
}
int main(int argc, char **argv) {
    CHECK(argc == 1 || argc >= 3);
    query_checks();
    void *workspace = malloc(requirements.size_bytes+alignof(max_align_t)); CHECK(workspace != NULL);
    preflight_checks(workspace); reflection_checks(workspace);
    abi1_checks(workspace); failure_checks(workspace); guard_checks(workspace);
    thread_checks(); fuzz_checks(workspace);
    if (argc >= 3) witness_check(workspace, argv[1], argv[2]);
    for (int i = 3; i < argc; ++i) corpus_check(workspace, argv[i]);
    free(workspace);
    CHECK(atomic_load(&allocation_calls) == 0u && atomic_load(&free_calls) == 0u);
    puts("PASS M5 native: query/preflight/layout/atomicity/reuse/guard/ABI1/threads/12000-fuzz");
    return 0;
}
