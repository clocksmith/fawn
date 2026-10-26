/* SPDX-License-Identifier: MIT
 * Nonfatal module notices retain owned source, records and artifact lifetimes. */
#include "doe_shader.h"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { size_t calls, fail, live; } allocations;
static void *allocate(void *context, size_t size, size_t alignment)
{
    allocations *state=context;
    assert(alignment<=alignof(max_align_t));
    if(++state->calls==state->fail) return NULL;
    void *memory=malloc(size);
    if(memory) ++state->live;
    return memory;
}
static void release(void *context, void *memory, size_t size, size_t alignment)
{
    allocations *state=context;
    assert(size && alignment<=alignof(max_align_t) && state->live);
    --state->live;
    free(memory);
}
static doe_shader_options options(allocations *state, doe_shader_target target)
{
    return (doe_shader_options){.allocator={state,allocate,release},.target=target,
        .compiler_identity="diagnostic-control",.compiler_identity_size=18,
        .capacities={.typed={.source_bytes=4096,.tokens=2048,.statements=256,.expressions=512,
            .types=64,.members=64,.resources=8,.access_steps=1024,.constants=256,.constant_edges=1024,
            .ir_instructions=4096,.ir_operands=2048,.target_declarations=4096,.traversal_items=128,
            .words=8192,.expression_depth=16,.control_depth=8,.type_syntax_depth=16},
            .entries=4,.entry_inputs=16,.entry_references=32},
        .interface_attributes=32,.stage_interfaces=32,.sampling_pairs=16};
}
static void target_case(doe_shader_target target)
{
    char source[]="diagnostic(warning,derivative_uniformity); @fragment fn main(@location(0)x:f32)->@location(0) f32{if(x>0.0){return dpdx(x);}return 0.0;}";
    allocations state={0};
    doe_shader_options policy=options(&state,target);
    doe_shader *module=NULL;
    doe_shader_error error;
    assert(doe_shader_create_module(&policy,0,source,strlen(source),&module,&error)==DOE_SHADER_OK);
    const size_t allocation_count=state.calls;
    assert(state.live==1);
    size_t count=99;
    const DoeCompilerNoticeV18 *notices=doe_shader_get_diagnostics(module,&count);
    assert(count==1 && notices && notices[0].severity==DOE_COMPILER_DIAGNOSTIC_WARNING);
    assert(notices[0].offset==(size_t)(strstr(source,"dpdx")-source));
    const DoeCompilerNoticeV18 saved=notices[0];
    source[0]='!';
    DoeCompilerEntrySelectionV5 selection={.mode=DOE_COMPILER_SELECT_NAME,.name="main",.name_length=4};
    doe_shader *shader=NULL;
    assert(doe_shader_specialize(module,&selection,NULL,0,&shader,&error)==DOE_SHADER_OK);
    doe_shader_release(module);
    notices=doe_shader_get_diagnostics(shader,&count);
    assert(count==1 && notices && !memcmp(notices,&saved,sizeof(saved)));
    doe_shader_release(shader);
    assert(!state.live);
    source[0]='d';
    for(size_t fail=1;fail<=allocation_count;++fail){
        state=(allocations){.fail=fail};
        module=(doe_shader *)(uintptr_t)1;
        assert(doe_shader_create_module(&policy,0,source,strlen(source),&module,&error)==DOE_SHADER_OUT_OF_MEMORY);
        assert(module==(doe_shader *)(uintptr_t)1 && !state.live);
    }
}
int main(void)
{
    size_t count=99;
    assert(!doe_shader_get_diagnostics(NULL,&count) && count==0);
    assert(!doe_shader_get_diagnostics(NULL,NULL));
    target_case(DOE_SHADER_SPIRV);
    target_case(DOE_SHADER_METAL);
    target_case(DOE_SHADER_HLSL);
    puts("shader diagnostic ownership controls passed");
    return 0;
}
