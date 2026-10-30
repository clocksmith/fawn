/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Fixed-array index guards: a literal index or an AND with a literal mask below
 * the length emits no guard branch; every other index keeps its guard. */
#include "../include/doe_compiler_v2.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DoeCompilerWorkspaceRequirements requirements;
static void *workspace;
static uint32_t words[DOE_COMPILER_MAX_WORDS];

/* Counts OpSelectionMerge in one compiled kernel storing to w[INDEX]. */
static unsigned merges(const char *index) {
    char text[512];
    const int length = snprintf(text, sizeof(text),
        "var<workgroup> w:array<atomic<u32>,256>;"
        "@group(0) @binding(0) var<storage,read_write> o:array<u32>;"
        "@compute @workgroup_size(1) fn m(@builtin(local_invocation_index) i:u32){"
        "let v=o[0];atomicStore(&w[%s],v);}", index);
    assert(length > 0 && (size_t)length < sizeof(text));
    DoeCompilerResultV2 result;
    const DoeCompilerStatus status = doe_compiler_compile_v2(2u, workspace, requirements.size_bytes, text,
        (size_t)length, words, DOE_COMPILER_MAX_WORDS, &result, sizeof(result));
    if (status != DOE_COMPILER_OK) fprintf(stderr, "%s: %s\n", index, result.diagnostic.message);
    assert(status == DOE_COMPILER_OK);
    unsigned count = 0u;
    for (size_t at = 5u; at < result.word_count;) {
        const uint32_t size = words[at] >> 16u;
        assert(size != 0u && at + size <= result.word_count);
        if ((words[at] & 0xffffu) == 247u) ++count; /* OpSelectionMerge */
        at += size;
    }
    return count;
}
int main(void) {
    assert(doe_compiler_workspace_requirements(2u, &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    workspace = malloc(requirements.size_bytes);
    assert(workspace != NULL);
    const unsigned dynamic = merges("v");
    assert(dynamic >= 1u);
    assert(merges("v&255u") == dynamic - 1u);
    assert(merges("255u&v") == dynamic - 1u);
    assert(merges("i32(v)&255") == dynamic - 1u);
    assert(merges("7u") == dynamic - 1u);
    assert(merges("v&256u") == dynamic);      /* Mask reaches the length. */
    assert(merges("i32(v)&-1") == dynamic);   /* Negative mask: no proof. */
    assert(merges("v|255u") == dynamic);      /* OR is unbounded. */
    assert(merges("i") == dynamic);           /* Builtin range is not assumed. */
    free(workspace);
    puts("Index guards: literal and masked-below-length indices unguarded; dynamic, wide, negative and OR masks guarded");
    return 0;
}
