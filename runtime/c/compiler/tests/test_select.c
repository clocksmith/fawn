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
    assert(argc == 1 || argc == 3 || argc == 4);
    const char *retained_path = argc >= 3 ? argv[1] : "tests/select_retained.wgsl";
    const char *cases_path = argc >= 3 ? argv[2] : "tests/select_cases.wgsl";
    const char *directory = argc == 4 ? argv[3] : NULL;
    char *retained = read_source(retained_path);
    char *cases = read_source(cases_path);
    expect(retained, true, directory, "retained");
    expect(cases, true, directory, "cases");
    expect("@compute @workgroup_size(1) fn main(){let a=select(1,2,true); _=a;}", true, NULL, NULL);
    expect("fn select(a:u32,b:u32,c:bool)->u32{return a;} @compute @workgroup_size(1) fn main(){_=select(1u,2u,true);}", true, NULL, NULL);
    expect("alias select=u32; @compute @workgroup_size(1) fn main(){_=select(1u);}", true, NULL, NULL);
    const char *invalid[] = {
        "select(1u,2u,1u)", "select(1u,2i,true)", "select(vec2u(1u),vec3u(2u),true)",
        "select(vec2u(1u),vec2u(2u),vec3<bool>(true))", "select(1u,2u,vec2<bool>(true))",
        "select(array(1u,2u),array(3u,4u),true)", "select(1u/0u,2u,true)",
        "select(2.0,sqrt(-1.0),false)", "select(unknown,2u,true)", "select(1u,2u)"
    };
    for (size_t i = 0u; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        char source[1024];
        const int n = snprintf(source, sizeof(source), "@compute @workgroup_size(1) fn main(){_=%s;}", invalid[i]);
        assert(n > 0 && (size_t)n < sizeof(source));
        expect(source, false, NULL, NULL);
    }
    HlslFixture *guarded = malloc(sizeof(*guarded));
    uint32_t *guarded_words = malloc(65536u * sizeof(*guarded_words));
    assert(guarded != NULL && guarded_words != NULL);
    const char *splat = "@group(0) @binding(0) var<storage,read_write> data:array<u32>;"
        "@compute @workgroup_size(1) fn main(){let c=data[0]!=0u;"
        "let value=select(vec2u(1u),vec2u(2u),c);data[0]=value.x;}";
    for (uint32_t target = 0u; target < 3u; ++target) {
        memset(guarded,0x5a,sizeof(*guarded));
        memset(guarded_words,0x5a,65536u*sizeof(*guarded_words));
        DoeCompilerTextResult result;
        assert(compile(target,splat,false,guarded,guarded_words,&result,0u)==DOE_COMPILER_INPUT_ERROR);
        assert(result.stages.resources.module.exhausted_pool==DOE_COMPILER_POOL_IR_OPERANDS);
        assert(result.stages.resources.module.minimum_pool_capacity==1u);
        assert(strstr(result.stages.resources.module.diagnostic.message,"mask splat")!=NULL);
        const unsigned char *bytes=(const unsigned char *)guarded;
        for (size_t i=0u;i<sizeof(*guarded);++i) assert(bytes[i]==0x5a);
        for (size_t i=0u;i<65536u;++i) assert(guarded_words[i]==UINT32_C(0x5a5a5a5a));
    }
    free(guarded_words);
    free(guarded);
    free(cases);
    free(retained);
    puts("select all-target admission, eager arguments, masks and untouched-output controls passed");
    return 0;
}
