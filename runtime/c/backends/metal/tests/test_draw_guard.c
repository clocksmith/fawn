/* SPDX-License-Identifier: MIT
 * Native-call windows/ownership only; PRIVATE mock objects retain metadata,
 * never backing host bytes. No GPU or large actual allocation claim. */
#include "mock_native.h"
#include "../src/render_internal.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    mock_native mock;
    doe_mtl_native calls=mock_driver(&mock);
    mock.buffer_limit=UINT64_C(0x100000010);
    doe_mtl_context *context=NULL;
    assert(doe_mtl_context_open(calls,&context,NULL)==DOE_MTL_OK);
    doe_mtl_draw_guard_desc desc={.source_version=DOE_MTL_DRAW_GUARD_SOURCE_VERSION,
        .normalize={"normalize",9,"main",{1,1,1},30},.scan={"scan",4,"main",{64,1,1},30},
        .record_slot=5,.scan_slots={2,4,6},.max_groups_x=1};
    doe_mtl_draw_guard *guard=NULL;
    unsigned baseline=mock.live;
    mock.fail_pipeline_at=mock.pipeline_calls+2;
    mock.pipeline_failure=DOE_MTL_OUT_OF_MEMORY;
    doe_mtl_error error={0};
    assert(doe_mtl_draw_guard_create(context,&desc,&guard,&error)==DOE_MTL_OUT_OF_MEMORY);
    assert(!guard && error.native_code==-71 && mock.live==baseline);
    mock.fail_pipeline_at=0; mock.pipeline_failure=DOE_MTL_OK;
    assert(doe_mtl_draw_guard_create(context,&desc,&guard,NULL)==DOE_MTL_OK);
    doe_mtl_texture *texture=NULL;
    doe_mtl_texture_desc image={DOE_MTL_TEXTURE_2D,DOE_MTL_RGBA8_UNORM,4,4,1,1,DOE_MTL_TEXTURE_RENDER_TARGET,false,1};
    assert(doe_mtl_texture_create(context,&image,&texture,NULL)==DOE_MTL_OK);
    doe_mtl_texture_view *view=NULL;
    doe_mtl_texture_view_desc span={DOE_MTL_TEXTURE_2D,DOE_MTL_RGBA8_UNORM,0,1,0,1,
        DOE_MTL_SWIZZLE_RED,DOE_MTL_SWIZZLE_GREEN,DOE_MTL_SWIZZLE_BLUE,DOE_MTL_SWIZZLE_ALPHA};
    assert(doe_mtl_texture_view_create(texture,&span,&view,NULL)==DOE_MTL_OK);
    doe_mtl_texture_release(texture);
    const doe_mtl_vertex_layout layout={0,4,1};
    const doe_mtl_vertex_attribute attribute={0,0,0,DOE_MTL_VERTEX_UINT};
    doe_mtl_render_pipeline_desc graphics={.vertex={"vertex",6,"v",UINT32_MAX},
        .fragment={"fragment",8,"f",UINT32_MAX},.color_count=1,.colors={{.format=DOE_MTL_RGBA8_UNORM,.write_mask=15}},
        .layouts=&layout,.layout_count=1,.attributes=&attribute,.attribute_count=1,.sample_count=1};
    doe_mtl_render_pipeline *pipeline=NULL;
    assert(doe_mtl_render_pipeline_create(context,&graphics,&pipeline,NULL)==DOE_MTL_OK);
    doe_mtl_buffer *indices=NULL,*vertices=NULL,*arguments=NULL;
    assert(doe_mtl_buffer_create(context,UINT64_C(0x100000010),DOE_MTL_PRIVATE,&indices,NULL)==DOE_MTL_OK);
    assert(doe_mtl_buffer_create(context,16,DOE_MTL_SHARED,&vertices,NULL)==DOE_MTL_OK);
    assert(doe_mtl_buffer_create(context,24,DOE_MTL_SHARED,&arguments,NULL)==DOE_MTL_OK);
    doe_mtl_buffer_binding vertex={0,vertices,0,16};
    doe_mtl_guarded_draw proof={guard,arguments,4,UINT64_C(0x100000008),0,false};
    doe_mtl_render_draw draw={.pipeline=pipeline,.vertex={.buffers=&vertex,.buffer_count=1},
        .topology=3,.count=3,.instance_count=1,.index_buffer=indices,.index_type=1,.guarded=&proof};
    doe_mtl_render_pass_desc pass={.color_count=1,.colors={{.view=view,.load=DOE_MTL_CLEAR,.store=DOE_MTL_STORE}}};
    doe_mtl_commands *commands=NULL;
    assert(doe_mtl_commands_create(context,&commands,NULL)==DOE_MTL_OK);
    assert(doe_mtl_commands_fill(commands,indices,0,indices->size,0,NULL)==DOE_MTL_OK);
    mock.fail=MOCK_COPY;
    assert(doe_mtl_commands_render(commands,&pass,&draw,1,&error)==DOE_MTL_NATIVE_FAILURE);
    assert(error.native_code==-71 && !mock.renders && !texture->initialized[0]);
    assert(doe_mtl_commands_release(commands)==DOE_MTL_OK);
    mock.fail=MOCK_NONE;
    assert(doe_mtl_commands_create(context,&commands,NULL)==DOE_MTL_OK);
    assert(doe_mtl_commands_fill(commands,indices,0,indices->size,0,NULL)==DOE_MTL_OK);
    unsigned dispatches=mock.dispatches,queries=mock.content_queries;
    doe_mtl_render_draw repeated[2]={draw,draw};
    assert(doe_mtl_commands_render(commands,&pass,repeated,2,NULL)==DOE_MTL_OK);
    if (mock.dispatches!=dispatches+3) {
        fprintf(stderr,"identical draw guards: expected3 dispatches, observed%u for2 native draws\n",
            mock.dispatches-dispatches);
        return 1;
    }
    assert(mock.renders==1 && mock.render_draw_count==2);
    assert(mock.render_draws[0].indirect_arguments==mock.render_draws[1].indirect_arguments);
    assert(mock.copy_sequence<mock.dispatch_sequence && mock.dispatch_sequence<mock.render_sequence);
    assert(mock.last_length==20 && mock.last_source_offset==4);
    assert(mock.bindings[4].offset==UINT64_C(0xfffffffc) && mock.bindings[4].size==12);
    assert(mock.content_queries==queries+6); /* Scratch/2params only, never PRIVATE indices. */
    assert(mock.render_draws[0].indirect_arguments && mock.render_draws[0].index_buffer==doe_mtl_buffer_native(indices));
    void *first_scratch=mock.render_draws[0].indirect_arguments;
    dispatches=mock.dispatches;
    assert(doe_mtl_commands_render(commands,&pass,repeated,2,NULL)==DOE_MTL_OK);
    assert(mock.dispatches==dispatches+3 && mock.render_draw_count==2);
    assert(mock.render_draws[0].indirect_arguments!=first_scratch); /* Never reuse across passes. */
    doe_mtl_guarded_draw other=proof;
    other.argument_offset=0;
    repeated[1].guarded=&other;
    dispatches=mock.dispatches;
    assert(doe_mtl_commands_render(commands,&pass,repeated,2,NULL)==DOE_MTL_OK);
    assert(mock.dispatches==dispatches+6 && mock.render_draw_count==2);
    assert(mock.render_draws[0].indirect_arguments!=mock.render_draws[1].indirect_arguments);
    repeated[1]=draw;
    doe_mtl_buffer_binding shorter=vertex;
    shorter.size=8; /* The same PSO now has a smaller safe vertex-fetch capacity. */
    repeated[1].vertex.buffers=&shorter;
    dispatches=mock.dispatches;
    assert(doe_mtl_commands_render(commands,&pass,repeated,2,NULL)==DOE_MTL_OK);
    assert(mock.dispatches==dispatches+6 && mock.render_draw_count==2);
    assert(mock.render_draws[0].indirect_arguments!=mock.render_draws[1].indirect_arguments);
    other=proof;
    other.index_size=16; /* One scan window rather than the original two. */
    repeated[1]=draw;
    repeated[1].guarded=&other;
    dispatches=mock.dispatches;
    assert(doe_mtl_commands_render(commands,&pass,repeated,2,NULL)==DOE_MTL_OK);
    assert(mock.dispatches==dispatches+5 && mock.render_draw_count==2);
    assert(mock.render_draws[0].indirect_arguments!=mock.render_draws[1].indirect_arguments);
    doe_mtl_guarded_draw direct=proof;
    direct.arguments=NULL; direct.argument_offset=0; direct.index_size=16;
    repeated[0]=draw; repeated[0].guarded=&direct;
    repeated[1]=repeated[0];
    dispatches=mock.dispatches;
    assert(doe_mtl_commands_render(commands,&pass,repeated,2,NULL)==DOE_MTL_OK);
    assert(mock.dispatches==dispatches+2 && mock.render_draw_count==2);
    assert(mock.render_draws[0].indirect_arguments==mock.render_draws[1].indirect_arguments);
    assert(doe_mtl_commands_submit(commands,NULL)==DOE_MTL_OK);
    mock_result(&mock,0,0);
    assert(doe_mtl_commands_poll(commands,NULL)==DOE_MTL_COMPLETION_UNKNOWN);
    doe_mtl_buffer_release(arguments); doe_mtl_buffer_release(vertices); doe_mtl_buffer_release(indices);
    doe_mtl_texture_view_release(view); doe_mtl_render_pipeline_release(pipeline); doe_mtl_draw_guard_release(guard);
    doe_mtl_context_release(context);
    assert(doe_mtl_commands_release(commands)==DOE_MTL_BUSY && mock.live>0);
    mock_result(&mock,4,0);
    assert(doe_mtl_commands_poll(commands,NULL)==DOE_MTL_OK);
    assert(doe_mtl_commands_release(commands)==DOE_MTL_OK);
    mock_assert_closed(&mock);
    puts("PASS Metal draw guard host:64-bit windows, exact pass-local guard reuse with original draws, changed argument/vertex/index spans, no index mapping, rollback and unknown leases; no GPU/large allocation claim");
}
