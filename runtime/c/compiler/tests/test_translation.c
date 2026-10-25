/* SPDX-License-Identifier: MIT */
#include "hlsl_fixture.h"
#include "../include/doe_compiler_translation.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

static const char source[] =
    "requires immediate_address_space;"
    "struct Params { x:u32, @align(16) y:vec3f, z:f32 }"
    "var<immediate> p:Params;"
    "@group(0)@binding(0)var<storage,read_write>out:array<vec4f>;"
    "fn helper()->vec4f{return vec4f(p.y,p.z)+vec4f(f32(p.x));}"
    "@compute @workgroup_size(1)fn main(){out[0]=helper();}"
    "@compute @workgroup_size(1)fn absent(){out[0]=vec4f(0);} ";
static void run(uint32_t target, const char *entry, size_t capacity, bool measure) {
    DoeCompilerTranslationCapacitiesV17 caps = {{{hlsl_capacities(),32u},0u},8u};
    caps.sampling.specialization.target.stages.module.typed.words = 65536u;
    caps.sampling.specialization.target.stages.module.typed.target_declarations = 8192u;
    DoeCompilerWorkspaceRequirements required;
    assert(doe_compiler_translation_requirements(17u,target,&caps,sizeof(caps),&required,sizeof(required)) == DOE_COMPILER_OK);
    void *workspace = malloc(required.size_bytes);
    HlslFixture *fixture = calloc(1,sizeof(*fixture));
    uint32_t *words = malloc(65536u*sizeof(*words));
    assert(workspace != NULL && fixture != NULL && words != NULL);
    DoeCompilerTranslationEntryV17 records[8];
    memset(records,0x5a,sizeof(records));
    DoeCompilerTranslationOutputsV17 outputs = {.entries=records,.entry_capacity=capacity,
        .entry_record_size=sizeof(records[0]),.sampling={.pair_record_size=sizeof(DoeCompilerSamplingPairV16)}};
    DoeCompilerHlslOutputs hlsl = hlsl_outputs(fixture);
    DoeCompilerWorkgroupV5 workgroup_canary;
    memset(&workgroup_canary,0x5a,sizeof(workgroup_canary));
    hlsl.stages.resources.module.workgroups = &workgroup_canary;
    hlsl.stages.resources.module.workgroup_capacity = 0u;
    uint32_t references[2] = {0x5a5a5a5au,0x5a5a5a5au};
    hlsl.stages.resources.module.entry_references = references;
    hlsl.stages.resources.module.entry_reference_capacity = 1u;
    DoeCompilerMetalBindingV12 bindings[64];
    DoeCompilerMetalEntryV12 entries[8];
    if (target == DOE_COMPILER_TARGET_SPIRV) {
        outputs.sampling.target.spirv = hlsl.stages;
        outputs.sampling.target.spirv.resources.module.words = words;
        outputs.sampling.target.spirv.resources.module.word_capacity = 65536u;
    } else if (target == DOE_COMPILER_TARGET_METAL) {
        outputs.sampling.target.metal = (DoeCompilerMetalOutputs){hlsl.stages,hlsl.text,hlsl.text_capacity,
            bindings,64u,sizeof(bindings[0]),entries,8u,sizeof(entries[0])};
    } else outputs.sampling.target.hlsl = hlsl;
    const DoeCompilerEntrySelectionV5 selection = {DOE_COMPILER_SELECT_NAME,0u,entry,strlen(entry)};
    const DoeCompilerSamplingOptionsV16 options = {UINT32_MAX,0u};
    DoeCompilerTranslationResultV17 result;
    errno = ERANGE;
    const DoeCompilerStatus status = doe_compiler_specialize_translation(17u,target,&options,sizeof(options),
        &caps,sizeof(caps),workspace,required.size_bytes,source,strlen(source),&selection,sizeof(selection),
        NULL,0u,sizeof(DoeCompilerOverrideValueV13),3u | (measure ? 4u : 0u),
        measure ? NULL : &outputs,measure ? 0u : sizeof(outputs),&result,sizeof(result));
    assert(errno == ERANGE);
    if (capacity != 0u || measure) {
        if (status != DOE_COMPILER_OK) fprintf(stderr,"translation target%u: %s\n",target,
            result.sampling.target.stages.resources.module.diagnostic.message);
        assert(status == DOE_COMPILER_OK);
        assert(result.required_entries == 1u);
        if (!measure) {
            const bool absent = strcmp(entry,"absent") == 0;
            assert(result.published_entries == 1u && records[0].entry_index == 0u);
            assert(records[0].immediate_bytes == (absent ? 0u : 32u));
            assert(records[0].immediate_accessible_bytes_low64 == (absent ? 0u : UINT64_C(0xffff000f)));
            assert(records[0].flags == 0u && records[0].reserved == 0u);
            assert(result.sampling.target.stages.resources.module.required.resources == 1u);
            assert(result.sampling.target.stages.resources.module.required.workgroups == 0u);
            assert(result.sampling.target.stages.resources.module.published.entry_references == 1u);
            const DoeCompilerEntryV5 *published = hlsl.stages.resources.module.entries;
            assert(published[0].stage == DOE_COMPILER_STAGE_COMPUTE);
            assert(published[0].workgroup_reference_count == 0u);
            assert(published[0].name.length == strlen(entry));
            assert(memcmp(hlsl.stages.resources.module.names+published[0].name.offset,entry,strlen(entry)) == 0);
            assert(references[0] == 0u && references[1] == 0x5a5a5a5au);
            const unsigned char *canary = (const unsigned char *)&workgroup_canary;
            for (size_t i = 0u; i < sizeof(workgroup_canary); ++i) assert(canary[i] == 0x5au);
        }
    } else {
        assert(status == DOE_COMPILER_OUTPUT_TOO_SMALL);
        assert(result.published_entries == 0u && records[0].entry_index == 0x5a5a5a5au);
    }
    free(words); free(fixture); free(workspace);
}
int main(void) {
    for (uint32_t target = DOE_COMPILER_TARGET_SPIRV; target <= DOE_COMPILER_TARGET_HLSL; ++target) {
        run(target,"main",8u,false); run(target,"absent",8u,false);
        run(target,"main",8u,true); run(target,"main",0u,false);
    }
    puts("translation ABI17 owner controls passed");
    return 0;
}
