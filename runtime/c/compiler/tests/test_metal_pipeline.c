/* SPDX-License-Identifier: MIT */
#include "metal12_fixture.h"
#include "../include/doe_compiler_metal_pipeline.h"
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>

static DoeCompilerStatus compile(uint32_t abi, const char *source, uint32_t mask,
    bool measure, Metal12Fixture *fixture, DoeCompilerTextResult *result)
{
    DoeCompilerSpecializationCapacities caps = {hlsl_capacities(), 8};
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_specialization_requirements(13, DOE_COMPILER_TARGET_METAL,
        &caps, sizeof(caps), &requirements, sizeof(requirements)) == DOE_COMPILER_OK);
    void *workspace = malloc(requirements.size_bytes);
    assert(workspace);
    const DoeCompilerEntrySelectionV5 selection = {.mode=DOE_COMPILER_SELECT_AUTO};
    const DoeCompilerMetalPipelineOptionsV15 options = {mask};
    const DoeCompilerSpecializationOutputs outputs = {.metal=metal12_outputs(fixture)};
    const uint32_t flags = 3u | (measure ? 4u : 0u) | DOE_COMPILER_CORE_BINDING_NUMBERS;
    const DoeCompilerStatus status = abi == 13 ?
        doe_compiler_specialize(13,DOE_COMPILER_TARGET_METAL,&caps,sizeof(caps),workspace,
            requirements.size_bytes,source,strlen(source),&selection,sizeof(selection),NULL,0,
            sizeof(DoeCompilerOverrideValueV13),flags,measure ? NULL : &outputs,
            measure ? 0 : sizeof(outputs),result,sizeof(*result)) :
        doe_compiler_specialize_metal_pipeline(abi,&options,sizeof(options),&caps,sizeof(caps),workspace,
            requirements.size_bytes,source,strlen(source),&selection,sizeof(selection),NULL,0,
            sizeof(DoeCompilerOverrideValueV13),flags,measure ? NULL : &outputs,
            measure ? 0 : sizeof(outputs),result,sizeof(*result));
    free(workspace);
    return status;
}
static void emit(const char *directory, const char *name, const char *text, size_t size)
{
    if (!directory) return;
    char path[1024];
    int n = snprintf(path,sizeof(path),"%s/%s.metal",directory,name);
    assert(n > 0 && (size_t)n < sizeof(path));
    FILE *file = fopen(path,"wbx");
    assert(file && fwrite(text,1,size,file) == size && fclose(file) == 0);
}
int main(int argc, char **argv)
{
    assert(argc <= 2);
    const char *directory = argc == 2 ? argv[1] : NULL;
    static const char *const sources[] = {
        "@fragment fn main()->@location(0) vec4f{return vec4f(1);}",
        "@fragment fn main()->@builtin(sample_mask) u32{return 10u;}",
        "struct Out{@builtin(sample_mask) mask:u32,@location(0) color:vec4f}"
        "@fragment fn main(@builtin(sample_mask) coverage:u32)->Out{return Out(coverage,vec4f(1));}",
        "@group(0) @binding(0) var<storage,read_write> effect:array<atomic<u32>>;"
        "@fragment fn main(){atomicAdd(&effect[0],1u);}",
        "@fragment fn main()->@builtin(frag_depth) f32{return 0.25;}"
    };
    static const char *const names[] = {"color", "mask", "struct", "effects", "depth"};
    Metal12Fixture *fixture = calloc(1,sizeof(*fixture));
    Metal12Fixture *legacy = calloc(1,sizeof(*legacy));
    assert(fixture && legacy);
    for (size_t i = 0; i < sizeof(sources)/sizeof(sources[0]); ++i) {
        DoeCompilerTextResult ordinary, full, result, measured;
        assert(compile(13,sources[i],UINT32_MAX,false,legacy,&ordinary) == DOE_COMPILER_OK);
        assert(compile(15,sources[i],UINT32_MAX,false,fixture,&full) == DOE_COMPILER_OK);
        assert(full.published_text_bytes == ordinary.published_text_bytes &&
            !memcmp(fixture->results.text,legacy->results.text,full.published_text_bytes));
        for (uint32_t j = 0; j < 2; ++j) {
            const uint32_t mask = j ? 5u : 0u;
            assert(fesetround(FE_DOWNWARD) == 0);
            errno = EDOM;
            DoeCompilerStatus status = compile(15,sources[i],mask,false,fixture,&result);
            if (status != DOE_COMPILER_OK) fprintf(stderr,"%u: %s\n",status,result.stages.resources.module.diagnostic.message);
            assert(status == DOE_COMPILER_OK && errno == EDOM && fegetround() == FE_DOWNWARD);
            assert(compile(15,sources[i],mask,true,fixture,&measured) == DOE_COMPILER_OK);
            assert(result.required_text_bytes == measured.required_text_bytes);
            assert(result.stages.published_interfaces == ordinary.stages.published_interfaces);
            assert(result.stages.resources.module.published.entries == 1);
            assert(result.published_text_bytes < sizeof(fixture->results.text));
            fixture->results.text[result.published_text_bytes] = '\0';
            const char *text = fixture->results.text;
            const char *assignment = strstr(text,i == 1 || i == 2 ?
                (j ? " & 0x00000005u;" : " & 0x00000000u;") :
                (j ? "output.fixed_sample_mask = 0x00000005u;" : "output.fixed_sample_mask = 0x00000000u;"));
            const char *call = strstr(text,"doe_fn0(ctx");
            assert(assignment && call && assignment > call);
            if (i == 1 || i == 2) assert(strstr(text," & 0x") && !strstr(text,"fixed_sample_mask"));
            else assert(strstr(text,"uint fixed_sample_mask [[sample_mask]];"));
            char name[64];
            assert(snprintf(name,sizeof(name),"%s-%u",names[i],mask) > 0);
            emit(directory,name,text,result.published_text_bytes);
        }
    }
    DoeCompilerTextResult result;
    memset(&result,0xa5,sizeof(result));
    DoeCompilerTextResult untouched = result;
    assert(compile(14,sources[0],3,false,fixture,&result) == DOE_COMPILER_ABI_MISMATCH);
    assert(!memcmp(&result,&untouched,sizeof(result)));
    const DoeCompilerMetalPipelineOptionsV15 options = {3};
    assert(doe_compiler_specialize_metal_pipeline(15,&options,sizeof(options)-1,NULL,0,NULL,0,
        NULL,0,NULL,0,NULL,0,0,0,NULL,0,&result,sizeof(result)) == DOE_COMPILER_ABI_MISMATCH);
    assert(!memcmp(&result,&untouched,sizeof(result)));
    assert(doe_compiler_specialize_metal_pipeline(15,NULL,sizeof(options),NULL,0,NULL,0,
        NULL,0,NULL,0,NULL,0,0,0,NULL,0,&result,sizeof(result)) == DOE_COMPILER_INPUT_ERROR);
    assert(!memcmp(&result,&untouched,sizeof(result)));
    const char *invalid[] = {"@compute @workgroup_size(1) fn main(){}",
        "@vertex fn main()->@builtin(position) vec4f{return vec4f(0);}"};
    for (size_t i=0;i<2;++i) {
        memset(fixture,0x5a,sizeof(*fixture));
        assert(compile(15,invalid[i],3,false,fixture,&result) == DOE_COMPILER_INPUT_ERROR);
        const unsigned char *bytes = (const unsigned char *)fixture;
        for (size_t j=0;j<sizeof(*fixture);++j) assert(bytes[j] == 0x5a);
    }
    assert(fesetround(FE_TONEAREST) == 0);
    free(legacy);
    free(fixture);
    puts("ABI15 Metal fragment mask: coverage, effects, reflection, rejection and FP controls passed");
    return 0;
}
