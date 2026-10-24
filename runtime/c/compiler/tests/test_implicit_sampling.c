/* SPDX-License-Identifier: MIT */
#include "hlsl_fixture.h"
#include "../include/doe_compiler_metal.h"
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <fenv.h>
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
    const int saved_errno = errno, rounding = fegetround(), exceptions = fetestexcept(FE_ALL_EXCEPT);
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
    assert(fegetround() == rounding && fetestexcept(FE_ALL_EXCEPT) == exceptions && errno == saved_errno);
    return status;
}
static void expect(const char *source, bool success, const char *directory, const char *name, const char *error) {
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
            if (error != NULL) assert(strstr(result.stages.resources.module.diagnostic.message, error) != NULL);
            if (error != NULL && strcmp(error,"fragment entry call graphs") == 0) {
                const char *call = strstr(source,"textureSample(");
                if (call == NULL) call = strstr(source,"textureSampleCompare(");
                assert(call != NULL && result.stages.resources.module.diagnostic.offset == (size_t)(call - source));
            }
            for (size_t i = 0u; i < sizeof(*storage); ++i) assert(((const unsigned char *)storage)[i] == 0x5a);
            for (size_t i = 0u; i < 65536u; ++i) assert(words[i] == UINT32_C(0x5a5a5a5a));
            continue;
        }
        if (target == 0u && name != NULL && strcmp(name,"effects") == 0) {
            uint32_t scalar = 0u, vector = 0u, calls[2] = {0u};
            size_t count = 0u;
            for (size_t at = 5u; at < result.stages.resources.module.published.words;) {
                const uint32_t width = words[at] >> 16u, opcode = words[at] & 65535u;
                assert(width != 0u && width <= result.stages.resources.module.published.words - at);
                if (opcode == 22u && words[at + 2u] == 32u) scalar = words[at + 1u];
                if (opcode == 23u && words[at + 2u] == scalar && words[at + 3u] == 2u) vector = words[at + 1u];
                if (opcode == 57u) {
                    assert(count < 2u);
                    calls[count++] = words[at + 1u];
                }
                at += width;
            }
            assert(count == 2u && scalar != 0u && vector != 0u && calls[0] == vector && calls[1] == scalar);
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
    }
    free(words);
    free(storage);
}
int main(int argc, char **argv) {
    assert(argc == 1 || argc == 2 || argc == 3 || argc == 5);
    fenv_t caller;
    assert(fegetenv(&caller) == 0 && fesetround(FE_UPWARD) == 0);
    assert(feraiseexcept(FE_INVALID | FE_DIVBYZERO) == 0);
    errno = EDOM;
    const char *directory = argc >= 2 ? argv[1] : NULL;
    static const char *const shapes[] = {"1d", "2d", "2d_array", "3d", "cube", "cube_array"};
    for (uint32_t depth = 0u; depth < 2u; ++depth) for (uint32_t i = 0u; i < 6u; ++i) {
        if (depth != 0u && (i == 0u || i == 3u)) continue;
        const char *coordinates = i == 0u ? "0.5f" : i < 3u ? "vec2f(0.5)" : "vec3f(1,0,0)";
        const char *layer = i == 2u || i == 5u ? ",-1i" : "";
        const char *offset = i == 1u || i == 2u ? ",vec2i(-8,7)" : i == 3u ? ",vec3i(-8,0,7)" : "";
        char source[4096], type[64], name[64], comparison[256];
        if (depth) {
            assert(snprintf(type, sizeof(type), "texture_depth_%s", shapes[i]) > 0);
            assert(snprintf(comparison, sizeof(comparison),
                "+textureSampleCompare(t,c,%s%s,0.5%s)", coordinates, layer, offset) > 0);
        } else {
            assert(snprintf(type, sizeof(type), "texture_%s<f32>", shapes[i]) > 0);
            comparison[0] = '\0';
        }
        assert(snprintf(source, sizeof(source), "@group(0) @binding(0) var t:%s;"
            "@group(0) @binding(1) var s:sampler;@group(0) @binding(2) var c:sampler_comparison;"
            "fn sample(tex:%s,smp:sampler,cmp:sampler_comparison,coords:%s)->%s{"
            "return textureSample(tex,smp,coords%s%s);}"
            "@fragment fn main()->@location(0) vec4f{return vec4f(sample(t,s,c,%s)%s);}",
            type, type, i == 0u ? "f32" : i < 3u ? "vec2f" : "vec3f", depth ? "f32" : "vec4f",
            layer, offset, coordinates, comparison) > 0);
        assert(snprintf(name, sizeof(name), "%s_%s", depth ? "depth" : "color", shapes[i]) > 0);
        expect(source, true, directory, name, NULL);
    }
    expect("fn textureSample(x:u32)->u32{return x;}@compute @workgroup_size(1) fn main(){let x=textureSample(7u);}",
        true,NULL,NULL,NULL);
    expect("alias textureSample=u32;@compute @workgroup_size(1) fn main(){let x=textureSample(7u);}",
        true,NULL,NULL,NULL);
    const char *prefix = "@group(0) @binding(0) var t:texture_2d<f32>;@group(0) @binding(1) var s:sampler;"
        "fn sample(coords:vec2f)->vec4f{return textureSample(t,s,coords);}"
        "fn indirect(coords:vec2f)->vec4f{return sample(coords);}";
    static const char *const valid[] = {
        "return indirect(p.xy);",
        "var result=vec4f(0);if(true){result=sample(p.xy);}return result;",
        "let result=sample(p.xy);if(p.x>0){return result;}return vec4f(0);"
    };
    static const char *const invalid[] = {
        "if(p.x>0){return indirect(p.xy);}return vec4f(0);",
        "if(p.x>0&&sample(p.xy).x>0){return vec4f(1);}return vec4f(0);",
        "if(p.x>0){return vec4f(0);}return sample(p.xy);",
        "if(textureSample(t,s,vec2f(0.5)).x>0){return sample(p.xy);}return vec4f(0);",
        "var result=vec4f(0);for(var i=0u;i<u32(p.x);i++){result=sample(p.xy);}return result;"
    };
    for (uint32_t set=0u;set<2u;++set) {
        const char *const *cases=set==0u ? valid : invalid;
        const size_t count=set==0u ? sizeof(valid)/sizeof(valid[0]) : sizeof(invalid)/sizeof(invalid[0]);
        for (size_t i=0u;i<count;++i) {
            char source[4096];
            assert(snprintf(source,sizeof(source),"%s @fragment fn main(@builtin(position) p:vec4f)->@location(0) vec4f{%s}",prefix,cases[i])>0);
            expect(source,set==0u,NULL,NULL,set==0u ? NULL : "uniform control");
        }
    }
    static const char *const stage_bodies[] = {
        "@compute @workgroup_size(1) fn main(){let color=indirect(vec2f(0.5));}",
        "@vertex fn main()->@builtin(position) vec4f{return indirect(vec2f(0.5));}",
        "@compute @workgroup_size(1) fn main(){if(false){let color=indirect(vec2f(0.5));}}",
        "@compute @workgroup_size(1) fn main(){return;let color=indirect(vec2f(0.5));}",
        ("@fragment fn good()->@location(0) vec4f{return sample(vec2f(0.5));}"
        "@compute @workgroup_size(1) fn bad(){let color=indirect(vec2f(0.5));}")
    };
    for (size_t i=0u;i<sizeof(stage_bodies)/sizeof(stage_bodies[0]);++i) {
        char source[4096];
        assert(snprintf(source,sizeof(source),"%s %s",prefix,stage_bodies[i])>0);
        expect(source,false,NULL,NULL,"fragment entry call graphs");
    }
    expect("@group(0) @binding(0) var t:texture_depth_2d;@group(0) @binding(1) var c:sampler_comparison;"
        "@fragment fn main(@builtin(position) p:vec4f)->@location(0) vec4f{"
        "if(p.x>0){return vec4f(textureSampleCompareLevel(t,c,p.xy,0.5));}return vec4f(0);}",
        true,directory,"explicit_fragment",NULL);
    expect("@group(0) @binding(0) var t:texture_depth_2d;@group(0) @binding(1) var c:sampler_comparison;"
        "@fragment fn main(@builtin(position) p:vec4f)->@location(0) vec4f{"
        "if(p.x>0){return vec4f(textureSampleCompare(t,c,p.xy,0.5));}return vec4f(0);}",
        false,NULL,NULL,"uniform control");
    expect("@group(0) @binding(0) var t:texture_2d<f32>;@group(0) @binding(1) var s:sampler;"
        "fn unused(coords:vec2f)->vec4f{return textureSample(t,s,coords);}"
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) i:vec3u){"
        "if(i.x>0u){let color=textureSampleLevel(t,s,vec2f(i.xy),0.0);}}",
        true,directory,"explicit_compute",NULL);
    expect("@group(0) @binding(0) var t:texture_3d<f32>;@group(0) @binding(1) var s:sampler;"
        "@fragment fn main()->@location(0) vec4f{return textureSample(t,s,vec3f(0.5),vec3i(0,0,8));}",
        false,NULL,NULL,"outside [-8,7]");
    expect("@group(0) @binding(0) var t:texture_depth_2d;@group(0) @binding(1) var s:sampler;"
        "@fragment fn main()->@location(0) vec4f{return vec4f(textureSampleCompare(t,s,vec2f(0.5),0.5));}",
        false,NULL,NULL,"sampler kind");
    expect("@group(0) @binding(0) var t:texture_depth_2d;@group(0) @binding(1) var c:sampler_comparison;"
        "@group(0) @binding(2) var<storage,read_write> trace:array<u32>;"
        "fn coordinates()->vec2f{trace[0]=trace[0]*10u+1u;return vec2f(0.5);}"
        "fn reference()->f32{trace[0]=trace[0]*10u+2u;return 0.5;}"
        "@fragment fn main()->@location(0) vec4f{return vec4f(textureSampleCompare(t,c,coordinates(),reference()));}",
        true,directory,"effects",NULL);
    char *source = read_source(argc >= 3 ? argv[2] : "tests/implicit_sampling.wgsl");
    expect(source,true,directory,"public",NULL);
    free(source);
    const char *retained_paths[2] = {
        argc == 5 ? argv[3] : "tests/implicit_sample_compute_retained.wgsl",
        argc == 5 ? argv[4] : "tests/implicit_compare_compute_retained.wgsl"
    };
    for (size_t i = 0u; i < 2u; ++i) {
        source = read_source(retained_paths[i]);
        expect(source,false,NULL,NULL,"fragment entry call graphs");
        free(source);
    }
    assert(fesetenv(&caller) == 0);
    puts("implicit sampling all-target overloads, transitive fragment stage, uniform control and untouched output controls passed");
    return 0;
}
