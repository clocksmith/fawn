/* SPDX-License-Identifier: MIT
 * Dynamic immediate accesses select flattened root scalars without CBV arrays. */
#include "hlsl_fixture.h"
#include "../include/doe_compiler_module_specialization.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static const char source[] =
    "enable f16;struct Params{prefix:u32,values:vec4h}"
    "var<immediate> params:Params;"
    "@group(0) @binding(0) var<storage,read_write> output:array<u32>;"
    "@compute @workgroup_size(1) fn run(@builtin(global_invocation_id) id:vec3u){"
    "output[0]=params.prefix+u32(params.values[id.x]);}";

int main(void)
{
    DoeCompilerSpecializationCapacities caps = {hlsl_capacities(),0u};
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_module_specialization_requirements(14u,DOE_COMPILER_TARGET_HLSL,
        &caps,sizeof(caps),&requirements,sizeof(requirements)) == DOE_COMPILER_OK);
    void *workspace = malloc(requirements.size_bytes);
    HlslFixture *fixture = calloc(1,sizeof(*fixture));
    assert(workspace && fixture);
    DoeCompilerSpecializationOutputs outputs = {.hlsl=hlsl_outputs(fixture)};
    DoeCompilerTextResult result;
    DoeCompilerStatus status = doe_compiler_specialize_module(14u,DOE_COMPILER_TARGET_HLSL,
        &caps,sizeof(caps),workspace,requirements.size_bytes,source,strlen(source),
        NULL,0u,sizeof(DoeCompilerOverrideValueV13),3u,&outputs,sizeof(outputs),&result,sizeof(result));
    if (status != DOE_COMPILER_OK) fprintf(stderr,"%s\n",result.stages.resources.module.diagnostic.message);
    assert(status == DOE_COMPILER_OK);
    assert(result.published_entries == 1u && result.published_bindings == 1u);
    assert(result.published_text_bytes < sizeof(fixture->text));
    fixture->text[result.published_text_bytes] = '\0';
    assert(strstr(fixture->text,"cbuffer DoeImmediate : register(b1, space7)"));
    assert(strstr(fixture->text,"uint r1_word0;"));
    assert(strstr(fixture->text,"uint r1_word3;"));
    assert(!strstr(fixture->text,"r1_data["));
    assert(!strstr(fixture->text,"r1_word4;"));
    assert(strstr(fixture->text,"if (index == 3u) return r1_word3;"));
    assert(strstr(fixture->text,"doe_r1_word(byte / 4u)"));
    assert(strstr(fixture->text,"doe_r1_word((offset + 0u) / 4u)"));
    free(fixture);
    free(workspace);
    puts("PASS HLSL scalar immediate carrier, dynamic integer/half loads, exact byte layout, no root arrays");
    return 0;
}
