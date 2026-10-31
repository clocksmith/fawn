/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Fixed-array index guards: a literal index or an AND with a literal mask below
 * the length emits no guard branch; every other index keeps its guard.
 * Storage and uniform resource indices clamp instead: no guard branch at all,
 * one OpULessThan/OpSelect pair per clamped index (runtime tails add one more
 * for max(length, 1) - 1), and no OpPhi zero merge. */
#include "../include/doe_compiler_v2.h"
#include "../include/doe_compiler_typed.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DoeCompilerWorkspaceRequirements requirements;
static void *workspace;
static uint32_t words[DOE_COMPILER_MAX_WORDS];

typedef struct { unsigned merges, selects, phis; } Shape;
static Shape count(size_t word_count) {
    Shape out = {0u, 0u, 0u};
    for (size_t at = 5u; at < word_count;) {
        const uint32_t size = words[at] >> 16u, opcode = words[at] & 0xffffu;
        assert(size != 0u && at + size <= word_count);
        if (opcode == 247u) ++out.merges; /* OpSelectionMerge */
        if (opcode == 169u) ++out.selects; /* OpSelect */
        if (opcode == 245u) ++out.phis; /* OpPhi */
        at += size;
    }
    return out;
}
static Shape shape(const char *text, int length) {
    assert(length > 0);
    DoeCompilerResultV2 result;
    const DoeCompilerStatus status = doe_compiler_compile_v2(2u, workspace, requirements.size_bytes, text,
        (size_t)length, words, DOE_COMPILER_MAX_WORDS, &result, sizeof(result));
    if (status != DOE_COMPILER_OK) fprintf(stderr, "%s: %s\n", text, result.diagnostic.message);
    assert(status == DOE_COMPILER_OK);
    return count(result.word_count);
}
/* Merges in one compiled kernel storing to the workgroup array w[INDEX]. */
static unsigned merges(const char *index) {
    char text[512];
    const int length = snprintf(text, sizeof(text),
        "var<workgroup> w:array<atomic<u32>,256>;"
        "@group(0) @binding(0) var<storage,read_write> o:array<u32>;"
        "@compute @workgroup_size(1) fn m(@builtin(local_invocation_index) i:u32){"
        "let v=o[0];atomicStore(&w[%s],v);}", index);
    assert((size_t)length < sizeof(text));
    return shape(text, length).merges;
}
/* Typed ABI: reflects fixed storage arrays, vectors and uniform roots. */
static const DoeCompilerTypedCapacities caps = {.source_bytes = 8192u, .tokens = 1024u, .statements = 64u,
    .expressions = 256u, .types = 32u, .members = 64u, .resources = 8u, .access_steps = 320u, .constants = 64u,
    .constant_edges = 128u, .ir_instructions = 1600u, .ir_operands = 256u, .target_declarations = 512u,
    .traversal_items = 97u, .words = 8192u, .expression_depth = 32u, .control_depth = 16u, .type_syntax_depth = 32u};
static Shape resource(const char *body) {
    char text[512];
    const int length = snprintf(text, sizeof(text),
        "@group(0) @binding(0) var<storage,read_write> o:array<u32>;"
        "@group(0) @binding(1) var<storage,read_write> f:array<vec4<u32>,8>;"
        "@group(0) @binding(2) var<uniform> u:array<vec4<u32>,4>;"
        "@compute @workgroup_size(1) fn m(@builtin(local_invocation_index) i:u32){%s}", body);
    assert(length > 0 && (size_t)length < sizeof(text));
    DoeCompilerWorkspaceRequirements typed;
    assert(doe_compiler_typed_requirements(4u, &caps, sizeof(caps), &typed, sizeof(typed)) == DOE_COMPILER_OK);
    void *space = malloc(typed.size_bytes);
    assert(space != NULL);
    const DoeCompilerTypedOutputs outputs = {.words = words, .word_capacity = caps.words,
        .type_record_size = sizeof(DoeCompilerTypeV4), .member_record_size = sizeof(DoeCompilerMemberV4),
        .resource_record_size = sizeof(DoeCompilerResourceV4)};
    DoeCompilerTypedResult result;
    const DoeCompilerStatus status = doe_compiler_compile_typed(4u, &caps, sizeof(caps), space, typed.size_bytes,
        text, (size_t)length, DOE_COMPILER_TYPED_WORDS, &outputs, sizeof(outputs), &result, sizeof(result));
    if (status != DOE_COMPILER_OK) fprintf(stderr, "%s: %d %s\n", text, (int)status, result.diagnostic.message);
    assert(status == DOE_COMPILER_OK);
    free(space);
    return count(result.published.words);
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
    /* Resource indices clamp without a guard branch or zero Phi. */
    Shape r = resource("o[i]=7u;");
    assert(r.merges == 0u && r.phis == 0u && r.selects == 2u); /* runtime tail */
    r = resource("o[i32(i)-1]=o[i+1u];");
    assert(r.merges == 0u && r.phis == 0u && r.selects == 4u); /* signed, load and store */
    r = resource("f[i][i]=u[i][i];");
    assert(r.merges == 0u && r.phis == 0u && r.selects == 4u); /* fixed array and vector steps */
    r = resource("f[3][i&3u]=u[1][2];");
    assert(r.merges == 0u && r.phis == 0u && r.selects == 0u); /* provably in range: no clamp */
    free(workspace);
    puts("Index guards: literal and masked-below-length indices unguarded; dynamic, wide, negative and OR masks guarded; "
        "storage/uniform indices clamped branch-free");
    return 0;
}
