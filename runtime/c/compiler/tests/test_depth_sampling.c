/* SPDX-License-Identifier: MIT */
#include "hlsl_fixture.h"
#include "../include/doe_compiler_metal.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static char *read_source(const char *path) {
    FILE *file = fopen(path, "rb");
    assert(file != NULL && fseek(file, 0, SEEK_END) == 0);
    const long length = ftell(file);
    assert(length > 0 && length < 8192 && fseek(file, 0, SEEK_SET) == 0);
    char *bytes = malloc((size_t)length + 1u);
    assert(bytes != NULL && fread(bytes, 1u, (size_t)length, file) == (size_t)length && fclose(file) == 0);
    bytes[length] = '\0';
    return bytes;
}
static void artifact(const char *directory, const char *name, const void *bytes, size_t size) {
    if (directory == NULL) return;
    char path[1024];
    const int length = snprintf(path, sizeof(path), "%s/%s", directory, name);
    assert(length > 0 && (size_t)length < sizeof(path));
    FILE *file = fopen(path, "wbx");
    assert(file != NULL && fwrite(bytes, 1u, size, file) == size && fclose(file) == 0);
}
static DoeCompilerStatus compile(uint32_t target, const char *source, bool measure,
    HlslFixture *storage, uint32_t *words, DoeCompilerTextResult *result, size_t operand_capacity) {
    DoeCompilerTextCapacities caps = hlsl_capacities();
    caps.stages.module.typed.words = 65536u;
    if (operand_capacity != SIZE_MAX) caps.stages.module.typed.ir_operands = operand_capacity;
    caps.stages.module.typed.target_declarations = 8192u;
    DoeCompilerWorkspaceRequirements requirements;
    DoeCompilerStatus status;
    if (target == 0u) status = doe_compiler_stage_requirements(9u, &caps.stages, sizeof(caps.stages),
        &requirements, sizeof(requirements));
    else if (target == 1u) status = doe_compiler_metal_requirements(12u, &caps, sizeof(caps),
        &requirements, sizeof(requirements));
    else status = doe_compiler_hlsl_requirements(11u, &caps, sizeof(caps), &requirements, sizeof(requirements));
    assert(status == DOE_COMPILER_OK);
    void *workspace = malloc(requirements.size_bytes);
    assert(workspace != NULL);
    const DoeCompilerEntrySelectionV5 selection = {DOE_COMPILER_SELECT_AUTO, 0u, NULL, 0u};
    DoeCompilerHlslOutputs hlsl = hlsl_outputs(storage);
    DoeCompilerMetalBindingV12 bindings[64];
    DoeCompilerMetalEntryV12 entries[8];
    DoeCompilerMetalOutputs metal = {hlsl.stages,hlsl.text,hlsl.text_capacity,
        bindings,64u,sizeof(bindings[0]),entries,8u,sizeof(entries[0])};
    const uint32_t flags = 3u | (measure ? 4u : 0u);
    memset(result, 0, sizeof(*result));
    if (target == 0u) {
        hlsl.stages.resources.module.words = words;
        hlsl.stages.resources.module.word_capacity = 65536u;
        status = doe_compiler_compile_stages(9u, &caps.stages, sizeof(caps.stages), workspace,
            requirements.size_bytes, source, strlen(source), &selection, sizeof(selection), flags,
            measure ? NULL : &hlsl.stages, measure ? 0u : sizeof(hlsl.stages), &result->stages, sizeof(result->stages));
    } else if (target == 1u) status = doe_compiler_compile_metal(12u, &caps, sizeof(caps), workspace,
        requirements.size_bytes, source, strlen(source), &selection, sizeof(selection), flags,
        measure ? NULL : &metal, measure ? 0u : sizeof(metal), result, sizeof(*result));
    else status = doe_compiler_compile_hlsl(11u, &caps, sizeof(caps), workspace, requirements.size_bytes,
        source, strlen(source), &selection, sizeof(selection), flags,
        measure ? NULL : &hlsl, measure ? 0u : sizeof(hlsl), result, sizeof(*result));
    free(workspace);
    return status;
}
static void expect(const char *source, bool success, const char *directory, const char *name) {
    HlslFixture *storage = malloc(sizeof(*storage));
    uint32_t *words = malloc(65536u * sizeof(*words));
    assert(storage != NULL && words != NULL);
    for (uint32_t target = 0u; target < 3u; ++target) {
        memset(storage, 0x5a, sizeof(*storage));
        memset(words, 0x5a, 65536u * sizeof(*words));
        DoeCompilerTextResult result;
        const DoeCompilerStatus status = compile(target, source, false, storage, words, &result, SIZE_MAX);
        if ((status == DOE_COMPILER_OK) != success)
            fprintf(stderr, "target%u offset%zu %s\n%s\n", target,
                result.stages.resources.module.diagnostic.offset, result.stages.resources.module.diagnostic.message, source);
        assert((status == DOE_COMPILER_OK) == success);
        if (!success) {
            const unsigned char *bytes = (const unsigned char *)storage;
            for (size_t i = 0u; i < sizeof(*storage); ++i) assert(bytes[i] == 0x5a);
            for (size_t i = 0u; i < 65536u; ++i) assert(words[i] == UINT32_C(0x5a5a5a5a));
            continue;
        }
        if (name != NULL && strncmp(name, "depth_", 6u) == 0) {
            assert(result.stages.resources.published_handles == 3u);
            assert(storage->handles[0].resource_index == 0u);
            assert(storage->handles[0].flags == DOE_COMPILER_HANDLE_DEPTH);
            assert(storage->handles[1].resource_index == 1u && storage->handles[1].flags == 0u);
            assert(storage->handles[2].resource_index == 2u);
            assert(storage->handles[2].flags == DOE_COMPILER_HANDLE_COMPARISON);
        }
        if (name != NULL && strcmp(name, "public") == 0 && target == 0u) {
            size_t calls = 0u, samples = 0u, compares = 0u, offsets = 0u;
            const size_t count = result.stages.resources.module.published.words;
            for (size_t at = 5u; at < count;) {
                const uint32_t width = words[at] >> 16u, opcode = words[at] & 65535u;
                assert(width != 0u && width <= count - at);
                if (opcode == 57u) ++calls;
                if (opcode == 88u) {
                    ++samples;
                    if (words[at + 5u] == 10u) ++offsets;
                }
                if (opcode == 90u) {
                    ++compares;
                    if (words[at + 6u] == 10u) ++offsets;
                }
                at += width;
            }
            assert(calls == 5u && samples == 7u && compares == 7u && offsets == 3u);
        }
        DoeCompilerTextResult measured;
        assert(compile(target, source, true, storage, words, &measured, SIZE_MAX) == DOE_COMPILER_OK);
        assert(result.stages.resources.module.required.words == measured.stages.resources.module.required.words);
        assert(result.required_text_bytes == measured.required_text_bytes);
        if (name != NULL) {
            char filename[64];
            const int n = snprintf(filename, sizeof(filename), "%s.%s", name, target == 0u ? "spv" : target == 1u ? "metal" : "hlsl");
            assert(n > 0 && (size_t)n < sizeof(filename));
            artifact(directory, filename, target == 0u ? (const void *)words : (const void *)storage->text,
                target == 0u ? result.stages.resources.module.published.words * sizeof(*words) : result.published_text_bytes);
        }
        if (name != NULL && strcmp(name, "public") == 0) {
            memset(storage, 0x5a, sizeof(*storage));
            memset(words, 0x5a, 65536u * sizeof(*words));
            assert(compile(target, source, false, storage, words, &result, 0u) != DOE_COMPILER_OK);
            for (size_t i = 0u; i < sizeof(*storage); ++i)
                assert(((unsigned char *)storage)[i] == 0x5a);
            for (size_t i = 0u; i < 65536u; ++i) assert(words[i] == UINT32_C(0x5a5a5a5a));
            assert(compile(target, source, true, storage, words, &result, 0u) != DOE_COMPILER_OK);
        }
    }
    free(words);
    free(storage);
}
int main(int argc, char **argv) {
    assert(argc == 1 || argc == 2 || argc == 3);
    const char *directory = argc >= 2 ? argv[1] : NULL;
    static const char *const shapes[] = {"2d", "2d_array", "cube", "cube_array"};
    static const char *const coords[] = {"vec2f(0.5)", "vec2f(0.5)", "vec3f(1,0,0)", "vec3f(1,0,0)"};
    for (uint32_t i = 0u; i < 4u; ++i) {
        char source[4096], name[64];
        const char *layer = i == 1u || i == 3u ? ",-1i" : "";
        const int n = snprintf(source, sizeof(source),
            "@group(0) @binding(0) var tex:texture_depth_%s;"
            "@group(0) @binding(1) var regular:sampler;"
            "@group(0) @binding(2) var comparison:sampler_comparison;"
            "@group(0) @binding(3) var<storage,read_write> output:array<f32>;"
            "fn sample(t:texture_depth_%s,s:sampler,c:sampler_comparison)->f32{"
            "return textureSampleCompareLevel(t,c,%s%s,0.5)+textureSampleLevel(t,s,%s%s,1u);}"
            "@compute @workgroup_size(1) fn main(){output[0]=sample(tex,regular,comparison);}",
            shapes[i], shapes[i], coords[i], layer, coords[i], layer);
        assert(n > 0 && (size_t)n < sizeof(source));
        assert(snprintf(name, sizeof(name), "depth_%s", shapes[i]) > 0);
        expect(source, true, directory, name);
    }
    const char *prefix = "@group(0) @binding(0) var t:texture_depth_2d;"
        "@group(0) @binding(1) var s:sampler;@group(0) @binding(2) var c:sampler_comparison;"
        "@group(0) @binding(3) var<storage,read_write> output:array<f32>;";
    static const char *const valid[] = {
        "output[0]=textureSampleLevel(t,s,vec2f(0.5),-1i,vec2i(-8,7));",
        "output[0]=textureSampleCompareLevel(t,c,vec2f(0.5),0.5,vec2i(7,-8));",
        "const o=vec2i(1,2)+vec2i(2,1);output[0]=textureSampleCompareLevel(t,c,vec2f(0.5),0.5,o);",
        "if(false){output[0]=textureSampleCompareLevel(t,c,vec2f(0.5),0.5,vec2i(1+2));}"
    };
    static const char *const invalid[] = {
        "output[0]=textureSampleLevel(t,c,vec2f(0.5),0i);",
        "output[0]=textureSampleCompareLevel(t,s,vec2f(0.5),0.5);",
        "output[0]=textureSampleLevel(t,s,vec2f(0.5),0.0f);",
        "output[0]=textureSampleCompareLevel(t,c,vec2f(0.5),0i);",
        "output[0]=textureSampleCompareLevel(t,c,vec3f(0.5),0.5);",
        "output[0]=textureSampleCompareLevel(t,c,vec2f(0.5),0.5,vec2u(0));",
        "output[0]=textureSampleCompareLevel(t,c,vec2f(0.5),0.5,vec2i(-9,0));",
        "output[0]=textureSampleLevel(t,s,vec2f(0.5),0i,vec2i(0,8));",
        "let o=vec2i(0);output[0]=textureSampleCompareLevel(t,c,vec2f(0.5),0.5,o);",
        "var o=vec2i(0);output[0]=textureSampleLevel(t,s,vec2f(0.5),0i,o);",
        "if(false){output[0]=textureSampleCompareLevel(t,c,vec2f(0.5),0.5,vec2i(4+4));}"
    };
    for (uint32_t set = 0u; set < 2u; ++set) {
        const char *const *cases = set == 0u ? valid : invalid;
        const size_t count = set == 0u ? sizeof(valid)/sizeof(valid[0]) : sizeof(invalid)/sizeof(invalid[0]);
        for (size_t i = 0u; i < count; ++i) {
            char source[4096], name[64];
            assert(snprintf(source, sizeof(source), "%s @compute @workgroup_size(1) fn main(){%s}", prefix, cases[i]) > 0);
            assert(snprintf(name, sizeof(name), "offset_%zu", i) > 0);
            expect(source, set == 0u, directory, set == 0u ? name : NULL);
        }
    }
    expect("@group(0) @binding(0) var t:texture_depth_2d_array;"
        "@group(0) @binding(1) var s:sampler;@group(0) @binding(2) var c:sampler_comparison;"
        "@group(0) @binding(3) var<storage,read_write> output:array<f32>;"
        "@compute @workgroup_size(1) fn main(){"
        "output[0]=textureSampleCompareLevel(t,c,vec2f(0.5),0u,0.5,vec2i(1,-1));"
        "output[1]=textureSampleLevel(t,s,vec2f(0.5),-1i,0u,vec2i(-1,1));}",
        true, directory, "array_offsets");
    expect("@group(0) @binding(0) var t:texture_depth_2d;@group(0) @binding(1) var c:sampler_comparison;"
        "@fragment fn main()->@location(0) f32{return textureSampleCompareLevel(t,c,vec2f(0.5),0.5);}",
        true, directory, "fragment");
    expect("@group(0) @binding(0) var t:texture_depth_2d;@group(0) @binding(1) var s:sampler;"
        "@vertex fn main()->@builtin(position) vec4f{return vec4f(textureSampleLevel(t,s,vec2f(0.5),0u));}",
        true, directory, "vertex");
    expect("@group(0) @binding(0) var t:texture_2d<f32>;@group(0) @binding(1) var s:sampler_comparison;"
        "@compute @workgroup_size(1) fn main(){let a=textureSampleCompareLevel(t,s,vec2f(0.5),0.5);}", false, NULL, NULL);
    expect("@group(0) @binding(0) var t:texture_depth_multisampled_2d;@group(0) @binding(1) var s:sampler_comparison;"
        "@compute @workgroup_size(1) fn main(){let a=textureSampleCompareLevel(t,s,vec2f(0.5),0.5);}", false, NULL, NULL);
    expect("@group(0) @binding(0) var t:texture_depth_cube;@group(0) @binding(1) var s:sampler_comparison;"
        "@compute @workgroup_size(1) fn main(){let a=textureSampleCompareLevel(t,s,vec3f(1,0,0),0.5,vec2i(0));}", false, NULL, NULL);
    expect("@group(0) @binding(0) var t:texture_depth_2d;@group(0) @binding(1) var s:sampler_comparison;"
        "override delta:i32=0;@compute @workgroup_size(1) fn main(){"
        "let a=textureSampleCompareLevel(t,s,vec2f(0.5),0.5,vec2i(delta));}", false, NULL, NULL);
    char *source = read_source(argc == 3 ? argv[2] : "tests/depth_sampling.wgsl");
    expect(source, true, directory, "public");
    free(source);
    puts("depth explicit/compare all-target overloads, const offsets and untouched output controls passed");
    return 0;
}
