/* SPDX-License-Identifier: MIT
 * Hidden draw offsets are selected-entry requirements, not invented WGSL resources. */
#include "hlsl_fixture.h"
#include "../include/doe_compiler_module_specialization.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static const char source[] =
    "struct Inputs{@builtin(vertex_index)vertex:u32,@builtin(instance_index)instance:u32}"
    "@vertex fn indexed(input:Inputs)->@builtin(position)vec4f{"
    "return vec4f(f32(input.vertex),f32(input.instance),0.0,1.0);}"
    "@vertex fn plain()->@builtin(position)vec4f{return vec4f(0.0,0.0,0.0,1.0);}"
    "@fragment fn fragment()->@location(0)vec4f{return vec4f(1.0);}";

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
    assert(result.published_entries == 3u && result.published_bindings == 0u);
    assert(fixture->target_entries[0].flags == DOE_COMPILER_HLSL_ENTRY_DRAW_OFFSETS);
    assert(!fixture->target_entries[1].flags && !fixture->target_entries[2].flags);
    assert(fixture->target_entries[0].stage == DOE_COMPILER_STAGE_VERTEX);
    assert(result.stages.resources.module.published.resources == 0u);
    assert(result.published_text_bytes < sizeof(fixture->text));
    fixture->text[result.published_text_bytes] = '\0';
    const char *declaration = "cbuffer DoeDrawOffsets : register(b3, space7) { uint2 doe_draw_offsets; };";
    const char *found = strstr(fixture->text,declaration);
    assert(found && !strstr(found+strlen(declaration),declaration));
    assert(strstr(fixture->text,"input.field0 + doe_draw_offsets.x"));
    assert(strstr(fixture->text,"input.field1 + doe_draw_offsets.y"));
    assert(!strstr(fixture->text,"SV_StartVertexLocation")); /* No SM6.8 prerequisite. */
    free(fixture);
    free(workspace);
    puts("PASS HLSL draw-offset uint2 carrier, both builtin additions, exact vertex-entry flag, no reflected user resource");
    return 0;
}
