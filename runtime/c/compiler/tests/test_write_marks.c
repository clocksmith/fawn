/* SPDX-License-Identifier: MIT
 * DOE_COMPILER_WRITE_MARKS: the hidden mark binding appears exactly when the
 * flag is set and the module has a writable storage resource, the default is
 * unchanged, and every case the marks cannot represent is refused. With an
 * output directory, marked modules are saved for spirv-val (Makefile). The
 * execution checks are native/tests/snapshot_canary.c and snapshot_ledger.c. */
#include "hlsl_fixture.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WORDS = 16384 };

typedef struct compiled {
    DoeCompilerStatus status;
    uint32_t words[WORDS];
    size_t word_count;
    char message[256];
} compiled;

static void compile(const char *source, uint32_t flags, compiled *out)
{
    DoeCompilerTextCapacities caps = hlsl_capacities();
    caps.stages.module.typed.words = WORDS;
    caps.stages.module.typed.target_declarations = 2048;
    HlslFixture *fixture = calloc(1, sizeof(*fixture));
    assert(fixture);
    DoeCompilerHlslOutputs hlsl = hlsl_outputs(fixture);
    const DoeCompilerEntrySelectionV5 selection = {DOE_COMPILER_SELECT_NONE, 0, NULL, 0};
    DoeCompilerWorkspaceRequirements required;
    assert(doe_compiler_stage_requirements(9, &caps.stages, sizeof(caps.stages), &required, sizeof(required)) == DOE_COMPILER_OK);
    void *workspace = malloc(required.size_bytes);
    assert(workspace);
    DoeCompilerStageOutputs stages = hlsl.stages;
    stages.resources.module.words = out->words;
    stages.resources.module.word_capacity = WORDS;
    DoeCompilerStageResult result;
    out->status = doe_compiler_compile_stages(9, &caps.stages, sizeof(caps.stages), workspace, required.size_bytes,
        source, strlen(source), &selection, sizeof(selection), 3 | flags, &stages, sizeof(stages), &result, sizeof(result));
    out->word_count = out->status == DOE_COMPILER_OK ? result.resources.module.published.words : 0;
    snprintf(out->message, sizeof(out->message), "%s", result.resources.module.diagnostic.message);
    free(workspace);
    free(fixture);
}

/* OpDecorate <id> Binding 1000 and OpDecorate <same id> DescriptorSet 0. */
static bool hidden_binding(const compiled *c)
{
    for (size_t at = 5; at < c->word_count; at += c->words[at] >> 16) {
        if ((c->words[at] & 0xffffu) != 71u || (c->words[at] >> 16) != 4u || c->words[at + 2] != 33u ||
            c->words[at + 3] != DOE_COMPILER_WRITE_MARK_BINDING) continue;
        for (size_t set = 5; set < c->word_count; set += c->words[set] >> 16)
            if ((c->words[set] & 0xffffu) == 71u && (c->words[set] >> 16) == 4u &&
                c->words[set + 1] == c->words[at + 1] && c->words[set + 2] == 34u)
                return c->words[set + 3] == DOE_COMPILER_WRITE_MARK_GROUP;
    }
    return false;
}

static size_t opcode_count(const compiled *c, uint32_t opcode)
{
    size_t count = 0;
    for (size_t at = 5; at < c->word_count; at += c->words[at] >> 16) count += (c->words[at] & 0xffffu) == opcode;
    return count;
}

static void save(const char *directory, const char *name, const compiled *c)
{
    if (!directory) return;
    char path[1024];
    int length = snprintf(path, sizeof(path), "%s/%s", directory, name);
    assert(length > 0 && (size_t)length < sizeof(path));
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(c->words, sizeof(uint32_t), c->word_count, file) == c->word_count && !fclose(file));
}

static void refused(const char *source, const char *expected)
{
    static compiled c;
    compile(source, DOE_COMPILER_WRITE_MARKS, &c);
    if (c.status == DOE_COMPILER_OK || !strstr(c.message, expected)) {
        fprintf(stderr, "expected refusal \"%s\", got %u: %s\n", expected, (unsigned)c.status, c.message);
        abort();
    }
}

/* Every marked access form: scalar, struct member vector component, whole
 * struct, fixed array element of a struct, matrix column, atomic, a store
 * through a pointer parameter, and a 2D textureStore. */
static const char marked_source[] =
    "struct Cell { a: u32, b: vec4<f32>, m: mat2x2<f32>, tail: array<u32, 3> }\n"
    "@group(0) @binding(0) var<storage, read_write> words: array<u32>;\n"
    "@group(0) @binding(1) var<storage, read_write> cells: array<Cell>;\n"
    "@group(0) @binding(2) var<storage, read_write> counters: array<atomic<u32>, 4>;\n"
    "@group(0) @binding(3) var<storage, read> input: array<u32>;\n"
    "@group(1) @binding(0) var image: texture_storage_2d<rgba8unorm, write>;\n"
    "fn put(p: ptr<storage, u32, read_write>, v: u32) { *p = v; }\n"
    "@compute @workgroup_size(8) fn main(@builtin(global_invocation_id) g: vec3<u32>) {\n"
    "  let i = g.x;\n"
    "  words[i] = input[i];\n"
    "  cells[i].b.y = 2.0;\n"
    "  cells[i + 1u] = Cell(1u, vec4<f32>(1.0), mat2x2<f32>(), array<u32, 3>(1u, 2u, 3u));\n"
    "  cells[i].tail[i % 3u] = i;\n"
    "  cells[i].m[1] = vec2<f32>(3.0, 4.0);\n"
    "  atomicAdd(&counters[i % 4u], 1u);\n"
    "  put(&words[i + 64u], i);\n"
    "  textureStore(image, vec2<u32>(i, 2u * i), vec4<f32>(0.5));\n"
    "}\n"
    "@compute @workgroup_size(1) fn reader() { _ = input[0]; }\n";

int main(int argc, char **argv)
{
    assert(argc == 1 || argc == 2);
    const char *directory = argc == 2 ? argv[1] : NULL;
    static compiled off, on, plain;
    compile(marked_source, 0, &off);
    compile(marked_source, DOE_COMPILER_WRITE_MARKS, &on);
    if (on.status != DOE_COMPILER_OK) fprintf(stderr, "%s\n", on.message);
    assert(off.status == DOE_COMPILER_OK && on.status == DOE_COMPILER_OK);
    /* Default compilation is unchanged; the flag adds the hidden binding and
     * one mark store per page a store can touch. */
    assert(!hidden_binding(&off) && hidden_binding(&on));
    assert(opcode_count(&on, 62u) > opcode_count(&off, 62u));
    assert(opcode_count(&on, 68u) > opcode_count(&off, 68u)); /* OpArrayLength of the mark buffer. */
    save(directory, "write-marks.spv", &on);
    save(directory, "write-marks-off.spv", &off);
    /* No writable resource: the flag emits nothing. */
    compile("@group(0) @binding(0) var<storage, read> a: array<u32>;\n"
            "@group(0) @binding(1) var<uniform> u: vec4<u32>;\n"
            "@compute @workgroup_size(1) fn main() { _ = a[u.x]; }\n", DOE_COMPILER_WRITE_MARKS, &plain);
    assert(plain.status == DOE_COMPILER_OK && !hidden_binding(&plain));
    save(directory, "write-marks-plain.spv", &plain);
    refused("requires immediate_address_space; struct P { v: u32 } var<immediate> p: P;\n"
            "@group(0) @binding(0) var<storage, read_write> d: array<u32>;\n"
            "@compute @workgroup_size(1) fn main() { d[0] = p.v; }\n", "var<immediate>");
    refused("@group(0) @binding(0) var<storage, read_write> d: array<array<u32, 16385>, 2>;\n"
            "@compute @workgroup_size(1) fn main() { d[1] = d[0]; }\n", "larger than one 64 KiB page");
    refused("@group(0) @binding(0) var image: texture_storage_2d_array<r32uint, write>;\n"
            "@compute @workgroup_size(1) fn main() { textureStore(image, vec2<u32>(0u), 0u, vec4<u32>(1u)); }\n",
            "2D storage textures only");
    refused("@group(0) @binding(0) var<storage, read_write> a0: u32; @group(0) @binding(1) var<storage, read_write> a1: u32;\n"
            "@group(0) @binding(2) var<storage, read_write> a2: u32; @group(0) @binding(3) var<storage, read_write> a3: u32;\n"
            "@group(0) @binding(4) var<storage, read_write> a4: u32; @group(0) @binding(5) var<storage, read_write> a5: u32;\n"
            "@group(0) @binding(6) var<storage, read_write> a6: u32; @group(0) @binding(7) var<storage, read_write> a7: u32;\n"
            "@group(1) @binding(0) var<storage, read_write> a8: u32;\n"
            "@compute @workgroup_size(1) fn main() { a0 = 1u; }\n", "at most 8 writable");
    printf("PASS write marks: hidden binding only under the flag and only with writable storage, "
        "%zu extra stores, 4 refusals; no device used\n", opcode_count(&on, 62u) - opcode_count(&off, 62u));
    return 0;
}
