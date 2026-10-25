/* SPDX-License-Identifier: MIT */
#include "hlsl_fixture.h"
#include "../include/doe_compiler_sampling.h"
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static const char source_text[] =
    "@group(0) @binding(0) var t0:texture_2d<f32>;"
    "@group(0) @binding(1) var t1:texture_2d<f32>;"
    "@group(0) @binding(2) var s0:sampler;"
    "@group(0) @binding(3) var s1:sampler;"
    "@group(0) @binding(4) var<storage,read_write> out:array<vec4f>;"
    "fn leaf(t:texture_2d<f32>,s:sampler)->vec4f{return textureSampleLevel(t,s,vec2f(0.5),0.0);}"
    "fn relay(t:texture_2d<f32>,s:sampler)->vec4f{return leaf(t,s);}"
    "@compute @workgroup_size(1) fn main(){out[0]=relay(t0,s1)+relay(t1,s0)+relay(t0,s1);"
    "out[1]=textureGather(0,t0,s1,vec2f(0.5));out[2]=textureLoad(t1,vec2i(0),0);}"
    "@compute @workgroup_size(1) fn other(){out[0]=relay(t0,s0);}"
    "@compute @workgroup_size(1) fn loads(){out[0]=textureLoad(t0,vec2i(0),0);}";
static const char compare_source[] =
    "@group(0) @binding(0) var depth:texture_depth_2d;"
    "@group(0) @binding(1) var cmp:sampler_comparison;"
    "@fragment fn compare()->@location(0) vec4f {"
    "return vec4f(textureSampleCompare(depth,cmp,vec2f(0.5),0.25))+"
    "textureGatherCompare(depth,cmp,vec2f(0.5),0.25);}";
static DoeCompilerSamplingCapacitiesV16 capacities(void) {
    DoeCompilerSamplingCapacitiesV16 caps = {{hlsl_capacities(),32u},8u};
    caps.specialization.target.stages.module.typed.words = 65536u;
    caps.specialization.target.stages.module.typed.target_declarations = 8192u;
    return caps;
}
static void run(uint32_t target, const char *entry, size_t staging, size_t output_count,
    bool measure, DoeCompilerStatus expected, size_t pairs) {
    DoeCompilerSamplingCapacitiesV16 caps = capacities();
    caps.sampling_pairs = staging;
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_sampling_requirements(16u,target,&caps,sizeof(caps),
        &requirements,sizeof(requirements)) == DOE_COMPILER_OK);
    void *workspace = malloc(requirements.size_bytes);
    HlslFixture *fixture = malloc(sizeof(*fixture));
    uint32_t *words = malloc(65536u * sizeof(*words));
    assert(workspace != NULL && fixture != NULL && words != NULL);
    memset(fixture,0x5a,sizeof(*fixture));
    memset(words,0x5a,65536u * sizeof(*words));
    DoeCompilerSamplingPairV16 records[8];
    memset(records,0x5a,sizeof(records));
    DoeCompilerSamplingOutputsV16 outputs = {.pairs=records,.pair_capacity=output_count,
        .pair_record_size=sizeof(records[0])};
    DoeCompilerHlslOutputs hlsl = hlsl_outputs(fixture);
    DoeCompilerMetalBindingV12 bindings[64];
    DoeCompilerMetalEntryV12 entries[8];
    if (target == DOE_COMPILER_TARGET_SPIRV) {
        outputs.target.spirv = hlsl.stages;
        outputs.target.spirv.resources.module.words = words;
        outputs.target.spirv.resources.module.word_capacity = 65536u;
    } else if (target == DOE_COMPILER_TARGET_METAL) {
        outputs.target.metal = (DoeCompilerMetalOutputs){hlsl.stages,hlsl.text,hlsl.text_capacity,
            bindings,64u,sizeof(bindings[0]),entries,8u,sizeof(entries[0])};
    } else outputs.target.hlsl = hlsl;
    const DoeCompilerEntrySelectionV5 selection = {DOE_COMPILER_SELECT_NAME,0u,entry,strlen(entry)};
    const bool comparison = strcmp(entry,"compare") == 0;
    const char *shader = comparison ? compare_source : source_text;
    const bool pipeline = comparison && target == DOE_COMPILER_TARGET_METAL;
    const DoeCompilerSamplingOptionsV16 options = {pipeline ? 21u : UINT32_MAX,
        pipeline ? DOE_COMPILER_SAMPLING_METAL_PIPELINE : 0u};
    DoeCompilerSamplingResultV16 result;
    errno = ERANGE;
    const DoeCompilerStatus status = doe_compiler_specialize_sampling(16u,target,&options,sizeof(options),
        &caps,sizeof(caps),workspace,requirements.size_bytes,shader,strlen(shader),
        &selection,sizeof(selection),NULL,0u,sizeof(DoeCompilerOverrideValueV13),3u | (measure ? 4u : 0u),
        measure ? NULL : &outputs,measure ? 0u : sizeof(outputs),&result,sizeof(result));
    assert(errno == ERANGE);
    if (status != expected) fprintf(stderr,"target%u status%u: %s\n",target,(unsigned)status,
        result.target.stages.resources.module.diagnostic.message);
    assert(status == expected);
    if (status == DOE_COMPILER_OK) {
        assert(result.required_pairs == pairs);
        assert(result.published_pairs == (measure ? 0u : pairs));
        if (!measure && pipeline) {
            assert(result.target.published_text_bytes < sizeof(fixture->text));
            fixture->text[result.target.published_text_bytes] = '\0';
            assert(strstr(fixture->text,"output.fixed_sample_mask = 0x00000015u;") != NULL);
        }
        const char *directory = getenv("DOE_SAMPLING_ARTIFACT_DIR");
        if (!measure && directory != NULL) {
            char path[4096];
            const int length = snprintf(path,sizeof(path),"%s/%s.%s",directory,entry,
                target == 0u ? "spv" : target == 1u ? "msl" : "hlsl");
            assert(length > 0 && (size_t)length < sizeof(path));
            FILE *file = fopen(path,"wb");
            assert(file != NULL);
            const void *data = target == 0u ? (const void *)words : (const void *)fixture->text;
            const size_t bytes = target == 0u ? result.target.stages.resources.module.published.words * sizeof(*words) :
                result.target.published_text_bytes;
            assert(fwrite(data,1u,bytes,file) == bytes && fclose(file) == 0);
        }
        if (!measure && pairs != 0u) {
            assert(records[0].entry_index == 0u && records[0].texture_resource_index == 0u);
            assert(records[0].sampler_resource_index == (comparison ? 1u : strcmp(entry,"main") == 0 ? 3u : 2u));
            if (pairs == 2u) assert(records[1].texture_resource_index == 1u && records[1].sampler_resource_index == 2u);
        }
    }
    if (status != DOE_COMPILER_OK) {
        if (staging == 1u) {
            assert(result.target.stages.resources.module.exhausted_pool == DOE_COMPILER_POOL_SAMPLING_PAIRS);
            assert(result.target.stages.resources.module.minimum_pool_capacity == 2u);
        } else assert(result.required_pairs == 2u);
    }
    if (status != DOE_COMPILER_OK || measure) {
        const unsigned char *bytes = (const unsigned char *)fixture;
        for (size_t i=0u;i<sizeof(*fixture);++i) assert(bytes[i] == 0x5a);
        bytes = (const unsigned char *)records;
        for (size_t i=0u;i<sizeof(records);++i) assert(bytes[i] == 0x5a);
        for (size_t i=0u;i<65536u;++i) assert(words[i] == UINT32_C(0x5a5a5a5a));
        assert(result.published_pairs == 0u && result.target.published_text_bytes == 0u &&
            result.target.stages.resources.module.published.words == 0u);
    }
    free(words);
    free(fixture);
    free(workspace);
}
int main(void) {
    for (uint32_t target=0u;target<3u;++target) {
        run(target,"main",8u,8u,false,DOE_COMPILER_OK,2u);
        run(target,"main",8u,8u,true,DOE_COMPILER_OK,2u);
        run(target,"compare",8u,8u,false,DOE_COMPILER_OK,1u);
        run(target,"other",8u,8u,false,DOE_COMPILER_OK,1u);
        run(target,"loads",0u,0u,false,DOE_COMPILER_OK,0u);
        run(target,"main",1u,8u,false,DOE_COMPILER_INPUT_ERROR,0u);
        run(target,"main",8u,1u,false,DOE_COMPILER_OUTPUT_TOO_SMALL,0u);
    }
    puts("sampling pairs: three targets, call-site aliases, gather, deduplication, entry selection and atomic failure passed");
    return 0;
}
