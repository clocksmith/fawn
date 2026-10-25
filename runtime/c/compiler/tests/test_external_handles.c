/* SPDX-License-Identifier: MIT
 * Canonical external identity survives selected-entry native target lowering. */
#include "hlsl_fixture.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    const char *source=
        "@group(0)@binding(0)var external_image:texture_external;"
        "@group(0)@binding(1)var ordinary_image:texture_2d<f32>;"
        "@group(0)@binding(2)var sample_state:sampler;"
        "@fragment fn main()->@location(0)vec4f{return "
        "textureSampleBaseClampToEdge(external_image,sample_state,vec2f(0))+"
        "textureSampleBaseClampToEdge(ordinary_image,sample_state,vec2f(1));}";
    DoeCompilerTextCapacities caps=hlsl_capacities();
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_hlsl_requirements(11u,&caps,sizeof(caps),&requirements,sizeof(requirements))==DOE_COMPILER_OK);
    void *workspace=malloc(requirements.size_bytes);
    HlslFixture *fixture=calloc(1,sizeof(*fixture));
    assert(workspace && fixture);
    DoeCompilerHlslOutputs outputs=hlsl_outputs(fixture);
    const DoeCompilerEntrySelectionV5 selection={DOE_COMPILER_SELECT_NAME,0u,"main",4u};
    DoeCompilerTextResult result;
    DoeCompilerStatus status=doe_compiler_compile_hlsl(11u,&caps,sizeof(caps),workspace,requirements.size_bytes,
        source,strlen(source),&selection,sizeof(selection),3u,&outputs,sizeof(outputs),&result,sizeof(result));
    if (status!=DOE_COMPILER_OK) fprintf(stderr,"%s\n",result.stages.resources.module.diagnostic.message);
    assert(status==DOE_COMPILER_OK);
    assert(fixture->handles[0].resource_index==0 && fixture->handles[0].flags==DOE_COMPILER_HANDLE_EXTERNAL);
    assert(fixture->handles[1].resource_index==1 && fixture->handles[1].flags==0);
    assert(fixture->handles[0].view_dimension==DOE_COMPILER_VIEW_2D && fixture->handles[0].storage_format==0);
    assert(fixture->handles[0].sample_scalar==DOE_COMPILER_LEAF_F32);
    assert(fixture->bindings[0].resource_index==0 && fixture->bindings[1].resource_index==1);
    assert(fixture->bindings[0].shader_register!=fixture->bindings[1].shader_register);
    assert(strstr(fixture->text,"SampleLevel(") && strstr(fixture->text,"0.5f / float2"));
    free(fixture); free(workspace);
    puts("PASS distinct external handle flags and real base-clamp target lowering; plane zero only, no YUV conversion");
    return 0;
}
