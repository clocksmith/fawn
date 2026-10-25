/* SPDX-License-Identifier: MIT
 * Real compiler/common guard metadata; no DXC, SDK or GPU execution. */
#include "../src/d3d12_draw_guard.h"
#include "../../common/include/doe_draw_guard.h"
#include "../../common/include/doe_timestamp.h"
#include <assert.h>
#include <stdalign.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static void *allocate(void *user,size_t size,size_t alignment)
{
    (void)user; assert(alignment<=alignof(max_align_t)); return malloc(size);
}
static void deallocate(void *user,void *memory,size_t size,size_t alignment)
{
    (void)user; (void)size; (void)alignment; free(memory);
}
int main(void)
{
    const doe_shader_options options={.target=DOE_SHADER_HLSL,.allocator={NULL,allocate,deallocate},
        .capacities={.typed={.source_bytes=4096,.tokens=4096,.statements=1024,.expressions=4096,
            .types=256,.members=256,.resources=8,.access_steps=2048,.constants=1024,.constant_edges=2048,
            .ir_instructions=16384,.ir_operands=16384,.target_declarations=4096,.traversal_items=1024,
            .words=65536,.expression_depth=64,.control_depth=32,.type_syntax_depth=32},
            .entries=4,.entry_inputs=16,.entry_references=16},.selection={.mode=DOE_COMPILER_SELECT_NONE},
        .compiler_identity="draw-guard-control",.compiler_identity_size=18};
    for (unsigned scan=0;scan<3;++scan) {
        char source[DOE_TIMESTAMP_SOURCE_CAPACITY]; size_t length=0;
        if (scan==2) assert(doe_timestamp_source(100.0f,64,64,source,sizeof(source),&length)==DOE_TIMESTAMP_SOURCE_OK);
        else assert((scan ? doe_draw_index_scan_source(source,sizeof(source),&length) :
            doe_draw_argument_source_v2(source,sizeof(source),&length))==DOE_DRAW_GUARD_SOURCE_OK);
        doe_shader *shader=NULL; doe_shader_error error={0};
        doe_shader_status status=doe_shader_create(&options,source,length,&shader,&error);
        if (status!=DOE_SHADER_OK) fprintf(stderr,"draw guard %u: %d %s\n",scan,(int)status,error.diagnostic.message);
        assert(status==DOE_SHADER_OK);
        doe_shader_view view;
        assert(doe_shader_borrow(shader,&view)==DOE_SHADER_OK);
        doe_dx_binding_layout bindings[3]={0};
        assert(scan==2 ? native_d3d12_storage_kernel_layout(&view,64,1,bindings) :
            native_d3d12_draw_layout(&view,scan!=0,bindings));
        size_t count=scan==1 ? 3u : 1u;
        for (size_t i=0;i<count;++i) assert(bindings[i].stride==4 && bindings[i].view==DOE_DX_STRUCTURED &&
            bindings[i].access==(i ? DOE_DX_READ_ONLY : DOE_DX_READ_WRITE));
        doe_dx_binding_layout saved[3]; memcpy(saved,bindings,sizeof(saved));
        doe_shader_view bad=view; DoeCompilerEntryV5 entry=*view.entries;
        bad.entries=&entry; entry.workgroup_size[0]=2;
        assert(!native_d3d12_draw_layout(&bad,scan!=0,bindings) && !memcmp(saved,bindings,sizeof(saved)));
        bad=view; bad.target=DOE_SHADER_METAL;
        assert(!native_d3d12_draw_layout(&bad,scan!=0,bindings) && !memcmp(saved,bindings,sizeof(saved)));
        if (scan<2) assert(!native_d3d12_draw_layout(&view,scan==0,bindings) && !memcmp(saved,bindings,sizeof(saved)));
        doe_shader_release(shader);
    }
    puts("PASS shared timestamp and V2 argument/index-scan WGSL to real HLSL reflection, typed register/stride/access and unchanged failures; no DXC/SDK/GPU");
}
