/* SPDX-License-Identifier: MIT */
#include "hlsl_fixture.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static void run(const char *source, bool raw, bool atomic, bool sizes) {
    DoeCompilerTextCapacities caps = hlsl_capacities();
    DoeCompilerWorkspaceRequirements requirements;
    assert(doe_compiler_hlsl_requirements(11u,&caps,sizeof(caps),&requirements,sizeof(requirements)) == DOE_COMPILER_OK);
    void *workspace = malloc(requirements.size_bytes);
    HlslFixture *fixture = calloc(1,sizeof(*fixture));
    assert(workspace != NULL && fixture != NULL);
    DoeCompilerHlslOutputs outputs = hlsl_outputs(fixture);
    const DoeCompilerEntrySelectionV5 selection = {DOE_COMPILER_SELECT_NAME,0u,"main",4u};
    DoeCompilerTextResult result;
    DoeCompilerStatus status = doe_compiler_compile_hlsl(11u,&caps,sizeof(caps),workspace,requirements.size_bytes,
        source,strlen(source),&selection,sizeof(selection),3u,&outputs,sizeof(outputs),&result,sizeof(result));
    if (status != DOE_COMPILER_OK) fprintf(stderr,"%s\n",result.stages.resources.module.diagnostic.message);
    assert(status == DOE_COMPILER_OK && result.published_entries == 1u);
    outputs.text[result.published_text_bytes] = '\0';
    assert(((outputs.entries[0].flags & DOE_COMPILER_HLSL_ENTRY_RAW_STORAGE) != 0u) == raw);
    /* Raw storage and runtime-array bounds both read the runtime size table. */
    assert(((outputs.entries[0].flags & DOE_COMPILER_HLSL_ENTRY_STORAGE_SIZES) != 0u) == (raw || sizes));
    assert((strstr(outputs.text,"DoeStorageSizes") != NULL) == (raw || sizes));
    assert(strstr(outputs.text,"GetDimensions") == NULL);
    assert((strstr(outputs.text,"doe_storage_sizes[0u][1u]") != NULL) == sizes);
    if (raw) {
        assert((outputs.entries[0].flags & DOE_COMPILER_HLSL_ENTRY_NATIVE16) != 0u);
        assert(outputs.bindings[1].resource_index == 1u && outputs.bindings[1].stride == 0u);
        assert(strstr(outputs.text,"RWByteAddressBuffer r1") != NULL);
        assert(strstr(outputs.text,"register(b2, space7)") != NULL);
        assert(strstr(outputs.text,".Store<float16_t>(") != NULL);
        assert(strstr(outputs.text,"r1[byte / 4u]") == NULL);
        if (atomic) assert(strstr(outputs.text,"r1.InterlockedCompareExchange(offset") != NULL);
        else assert(strstr(outputs.text,".Load<float16_t>(") != NULL);
    } else {
        assert(outputs.bindings[1].resource_index == 1u && outputs.bindings[1].stride == 4u);
        assert(strstr(outputs.text,"RWStructuredBuffer<uint>") != NULL);
    }
    free(fixture); free(workspace);
}
int main(void) {
    run("enable f16; @group(0)@binding(0)var<storage,read>unused:array<u32>;"
        "@group(0)@binding(1)var<storage,read_write>a:array<f16>;"
        "@compute @workgroup_size(1)fn main(){a[unused[0]]=a[0];}",true,false,true);
    run("enable f16; struct S {half:f16, counter:atomic<i32>}"
        "@group(0)@binding(0)var<storage,read>unused:array<u32>;"
        "@group(0)@binding(1)var<storage,read_write>a:S;"
        "@compute @workgroup_size(1)fn main(){a.half=1.5h;let old=atomicMin(&a.counter,i32(unused[0]));}",true,true,false);
    run("@group(0)@binding(0)var<storage,read>fixed:vec4u;@group(0)@binding(1)var<storage,read_write>a:array<u32>;"
        "@compute @workgroup_size(1)fn main(){a[0]=fixed.x;}",false,false,true);
    run("@group(0)@binding(0)var<storage,read>fixed:vec4u;@group(0)@binding(1)var<storage,read_write>a:vec4u;"
        "@compute @workgroup_size(1)fn main(){a.x=fixed.x;}",false,false,false);
    puts("HLSL raw half requirements, runtime size-table flags/rows, exact logical sizes and mixed atomic controls passed");
}
