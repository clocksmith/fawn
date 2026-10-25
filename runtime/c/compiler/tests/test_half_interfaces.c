/* SPDX-License-Identifier: MIT */
#include "hlsl_fixture.h"
#include "../include/doe_compiler_module_specialization.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static const char source[] = "enable f16;"
    "struct Out {@builtin(position)position:vec4f,@location(0)value:vec3h}"
    "@vertex fn vs(@location(0)x:vec3h)->Out{return Out(vec4f(vec3f(x),1.0),x*vec3h(1.5h));}"
    "@fragment fn fs(@location(0)x:vec3h)->@location(0)vec4h{return vec4h(x,1.0h);}";
static void run(uint32_t target) {
    DoeCompilerSpecializationCapacities caps = {hlsl_capacities(),0u};
    caps.target.stages.module.typed.words = 65536u;
    caps.target.stages.module.typed.target_declarations = 4096u;
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_module_specialization_requirements(14u,target,&caps,sizeof(caps),&requirements,sizeof(requirements)) == DOE_COMPILER_OK);
    void *workspace = malloc(requirements.size_bytes);
    HlslFixture *fixture = calloc(1,sizeof(*fixture));
    uint32_t *words = malloc(65536u*sizeof(*words));
    assert(workspace != NULL && fixture != NULL && words != NULL);
    DoeCompilerHlslOutputs hlsl = hlsl_outputs(fixture);
    DoeCompilerSpecializationOutputs outputs = {0};
    DoeCompilerMetalBindingV12 metal_bindings[16];
    DoeCompilerMetalEntryV12 metal_entries[8];
    if (target == 0u) {
        outputs.spirv = hlsl.stages;
        outputs.spirv.resources.module.words = words;
        outputs.spirv.resources.module.word_capacity = 65536u;
    } else if (target == 1u) outputs.metal = (DoeCompilerMetalOutputs){hlsl.stages,hlsl.text,hlsl.text_capacity,
        metal_bindings,16u,sizeof(metal_bindings[0]),metal_entries,8u,sizeof(metal_entries[0])};
    else outputs.hlsl = hlsl;
    DoeCompilerTextResult result;
    DoeCompilerStatus status = doe_compiler_specialize_module(14u,target,&caps,sizeof(caps),workspace,requirements.size_bytes,
        source,strlen(source),NULL,0u,sizeof(DoeCompilerOverrideValueV13),3u,
        &outputs,sizeof(outputs),&result,sizeof(result));
    if (status != DOE_COMPILER_OK) fprintf(stderr,"%s\n",result.stages.resources.module.diagnostic.message);
    assert(status == DOE_COMPILER_OK);
    assert(result.stages.resources.module.published.entries == 2u && result.stages.published_interfaces == 5u);
    uint32_t half = 0u;
    for (size_t i = 0u; i < result.stages.published_interfaces; ++i) {
        const DoeCompilerStageInterfaceV7 *leaf = &fixture->interfaces[i];
        if (leaf->location != UINT32_MAX) {
            assert(leaf->scalar_kind == DOE_COMPILER_LEAF_F16);
            ++half;
        }
    }
    assert(half == 4u);
    if (target == 0u) {
        uint32_t converts = 0u;
        for (size_t at = 5u; at < result.stages.resources.module.published.words; at += words[at]>>16) {
            if ((words[at]&65535u) == 17u) assert(words[at+1u] != 4436u);
            if ((words[at]&65535u) == 115u) ++converts;
        }
        assert(converts >= 4u);
    } else {
        fixture->text[result.published_text_bytes] = '\0';
        assert(strstr(fixture->text,"float3 field") != NULL);
        assert(strstr(fixture->text,target == 1u ? "half3(input.field" : "vector<float16_t, 3>(input.field") != NULL);
        if (target == 2u) for (size_t i = 0u; i < result.published_entries; ++i)
            assert((fixture->target_entries[i].flags & DOE_COMPILER_HLSL_ENTRY_NATIVE16) != 0u);
    }
    free(words); free(fixture); free(workspace);
}
int main(void) {
    for (uint32_t target = 0u; target < 3u; ++target) run(target);
    puts("half interfaces retain logical reflection and use f32 native boundary carriers");
}
