/* SPDX-License-Identifier: MIT */
#include "mock_native.h"
#include <stdio.h>

enum { ROWS=DOE_DX_MAX_BINDINGS, STAGES=DOE_DX_RENDER_STAGES, SAMPLERS=DOE_DX_MAX_DYNAMIC_SAMPLERS };
static doe_dx_render_draw draw(doe_dx_pipeline *pipeline)
{
    return (doe_dx_render_draw){.pipeline=pipeline,.vertex_count=3,.instance_count=1,
        .viewport={0,0,4,4,0,1},.scissor={0,0,4,4}};
}
/* Two stages share allocation classes, not independent state machines. */
static void aliases(void)
{
    doe_dx_context *context=NULL;
    assert(doe_dx_context_create(&(doe_dx_options){0},&context,NULL)==DOE_DX_OK);
    doe_dx_buffer *upload=NULL,*buffer=NULL;
    assert(doe_dx_buffer_create(context,256,DOE_DX_UPLOAD,&upload,NULL)==DOE_DX_OK);
    assert(doe_dx_buffer_create(context,256,DOE_DX_DEFAULT,&buffer,NULL)==DOE_DX_OK);
    doe_dx_commands *initialize=NULL;
    assert(doe_dx_commands_create(context,&initialize,NULL)==DOE_DX_OK);
    assert(doe_dx_commands_copy(initialize,upload,0,buffer,0,256,NULL)==DOE_DX_OK);
    doe_dx_buffer_release(upload);
    assert(doe_dx_commands_submit(initialize,NULL)==DOE_DX_OK);
    doe_dx_commands_release(initialize);
    mock.completed=1;
    assert(doe_dx_context_poll(context,0,NULL)==DOE_DX_OK);
    doe_dx_texture *color=NULL;
    assert(doe_dx_texture_create(context,&(doe_dx_texture_desc){4,4,DOE_DX_RGBA8_UNORM,
        DOE_DX_TEXTURE_RENDER_ATTACHMENT},&color,NULL)==DOE_DX_OK);
    doe_dx_binding_layout rows[2]={{0,0,4,DOE_DX_READ_ONLY,DOE_DX_STRUCTURED},
        {0,0,4,DOE_DX_READ_WRITE,DOE_DX_STRUCTURED}};
    unsigned char code=1;
    doe_dx_render_pipeline_desc desc={.color_format=DOE_DX_RGBA8_UNORM};
    for (size_t i=0;i<2;++i) desc.stages[i].resources.buffers=(doe_dx_pipeline_desc){&code,1,&rows[i],1};
    doe_dx_pipeline *mixed=NULL,*writable=NULL;
    assert(doe_dx_pipeline_create_render(context,&desc,&mixed,NULL)==DOE_DX_OK);
    rows[0].access=DOE_DX_READ_WRITE;
    assert(doe_dx_pipeline_create_render(context,&desc,&writable,NULL)==DOE_DX_OK);
    doe_dx_buffer_binding views[2]={{buffer,0,128},{buffer,128,128}};
    doe_dx_render_draw packet=draw(mixed);
    for (size_t i=0;i<2;++i) packet.stages[i].resources=(doe_dx_compute_resources){&views[i],1,NULL,0};
    doe_dx_render_pass pass={.color=color,.draws=&packet,.draw_count=1};
    doe_dx_commands *commands=NULL;
    assert(doe_dx_commands_create(context,&commands,NULL)==DOE_DX_OK);
    /* Even disjoint READ/UAV ranges cannot share incompatible native states. */
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_UNSUPPORTED);
    packet.pipeline=writable;
    views[1].offset=64;
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_UNSUPPORTED);
    assert(!commands->buffer_count && !commands->texture_count && !commands->pipeline_count && !mock.render_passes);
    views[1].offset=128;
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_OK);
    assert(commands->buffer_count==1 && commands->texture_count==1 && commands->pipeline_count==1);
    doe_dx_commands_release(commands); /* Unsubmitted cancellation releases one allocation lease. */
    doe_dx_pipeline_release(mixed); doe_dx_pipeline_release(writable);
    doe_dx_texture_release(color); doe_dx_buffer_release(buffer);
    assert(!mock.buffers && !mock.pipelines && !mock.textures && !mock.commands);
    assert(doe_dx_context_destroy(context)==DOE_DX_OK);
    mock_reset();
}
static void indirect_pass(void)
{
    doe_dx_context *context=NULL;
    assert(doe_dx_context_create(&(doe_dx_options){0},&context,NULL)==DOE_DX_OK);
    unsigned char code=1;
    doe_dx_render_pipeline_desc desc={.color_format=DOE_DX_RGBA8_UNORM};
    for (size_t i=0;i<2;++i) desc.stages[i].resources.buffers=(doe_dx_pipeline_desc){&code,1,NULL,0};
    doe_dx_pipeline *pipeline=NULL;
    assert(doe_dx_pipeline_create_render(context,&desc,&pipeline,NULL)==DOE_DX_OK);
    doe_dx_texture *color=NULL;
    assert(doe_dx_texture_create(context,&(doe_dx_texture_desc){4,4,DOE_DX_RGBA8_UNORM,
        DOE_DX_TEXTURE_RENDER_ATTACHMENT},&color,NULL)==DOE_DX_OK);
    doe_dx_buffer *upload=NULL,*buffer=NULL;
    assert(doe_dx_buffer_create(context,64,DOE_DX_UPLOAD,&upload,NULL)==DOE_DX_OK);
    assert(doe_dx_buffer_create(context,64,DOE_DX_DEFAULT,&buffer,NULL)==DOE_DX_OK);
    doe_dx_commands *commands=NULL;
    assert(doe_dx_commands_create(context,&commands,NULL)==DOE_DX_OK);
    doe_dx_buffer_binding arguments={buffer,0,40},indices={buffer,40,24};
    doe_dx_render_draw packet=draw(pipeline);
    packet.vertex_count=packet.instance_count=0;
    packet.indirect=&arguments;
    uint64_t capacities[2]={0};
    assert(doe_dx_render_draw_capacities(&packet,capacities)==DOE_DX_OK &&
        capacities[0]==UINT64_MAX && capacities[1]==UINT64_MAX);
    doe_dx_render_pass pass={.color=color,.draws=&packet,.draw_count=1};
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_UNINITIALIZED);
    assert(!commands->buffer_count && !commands->texture_count && !mock.render_passes);
    assert(doe_dx_commands_copy(commands,upload,0,buffer,0,64,NULL)==DOE_DX_OK);
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_OK);
    assert(mock.last_render_draw.indirect.buffer==buffer->native && !mock.last_render_draw.indices.buffer);
    packet.indices=&indices; packet.index_format=DOE_DX_INDEX_UINT16;
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_OK);
    assert(mock.last_render_draw.indices.buffer==buffer->native && commands->buffer_count==2);
    size_t before=mock.render_passes;
    arguments.size=19;
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_RANGE && mock.render_passes==before);
    arguments.size=40; arguments.offset=2;
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_RANGE && mock.render_passes==before);
    arguments.offset=0; indices.offset=41;
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_RANGE && mock.render_passes==before);
    indices.offset=40; packet.indirect=NULL;
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_INVALID_ARGUMENT && mock.render_passes==before);
    doe_dx_pipeline_release(pipeline); doe_dx_texture_release(color);
    doe_dx_buffer_release(upload); doe_dx_buffer_release(buffer);
    doe_dx_commands_release(commands);
    assert(!mock.buffers && !mock.pipelines && !mock.textures && doe_dx_context_destroy(context)==DOE_DX_OK);
    mock_reset();
}
static void depth_only_pass(void)
{
    doe_dx_context *context=NULL;
    assert(doe_dx_context_create(&(doe_dx_options){0},&context,NULL)==DOE_DX_OK);
    unsigned char code=1;
    doe_dx_render_pipeline_desc desc={.fragment_absent=true,.color_absent=true,.depth_enabled=true,
        .depth_write=true,.depth_compare=DOE_DX_LESS};
    desc.stages[0].resources.buffers=(doe_dx_pipeline_desc){&code,1,NULL,0};
    doe_dx_pipeline *pipeline=NULL;
    desc.stages[1].resources.buffers.bytecode=&code;
    assert(doe_dx_pipeline_create_render(context,&desc,&pipeline,NULL)==DOE_DX_INVALID_ARGUMENT && !pipeline);
    desc.stages[1]=(doe_dx_dynamic_pipeline_desc){0};
    desc.fragment_absent=false;
    assert(doe_dx_pipeline_create_render(context,&desc,&pipeline,NULL)!=DOE_DX_OK && !pipeline);
    desc.fragment_absent=true;
    assert(doe_dx_pipeline_create_render(context,&desc,&pipeline,NULL)==DOE_DX_OK);
    assert(mock.last_render_pipeline.fragment_absent && mock.last_render_pipeline.color_absent);
    doe_dx_texture *depth=NULL;
    assert(doe_dx_texture_create(context,&(doe_dx_texture_desc){4,4,DOE_DX_D32_FLOAT,
        DOE_DX_TEXTURE_DEPTH_ATTACHMENT},&depth,NULL)==DOE_DX_OK);
    doe_dx_commands *commands=NULL;
    assert(doe_dx_commands_create(context,&commands,NULL)==DOE_DX_OK);
    doe_dx_render_draw packet=draw(pipeline);
    doe_dx_render_pass pass={.depth=depth,.clear_depth=true,.depth_clear=1,.draws=&packet,.draw_count=1};
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_OK);
    assert(commands->texture_count==1 && mock.render_depth.texture==depth->native && mock.render_depth.value==1);
    pass.draws=NULL; pass.draw_count=0; pass.clear_depth=false; pass.discard_depth=true;
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_OK && mock.render_depth.discard);
    pass.clear=true;
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_INVALID_ARGUMENT);
    doe_dx_texture_release(depth); doe_dx_pipeline_release(pipeline); doe_dx_commands_release(commands);
    assert(!mock.textures && !mock.pipelines && doe_dx_context_destroy(context)==DOE_DX_OK);
    mock_reset();
}
static void depth_pass(void)
{
    doe_dx_context *context=NULL;
    assert(doe_dx_context_create(&(doe_dx_options){0},&context,NULL)==DOE_DX_OK);
    doe_dx_texture *color=NULL,*depth=NULL;
    assert(doe_dx_texture_create(context,&(doe_dx_texture_desc){4,4,DOE_DX_RGBA8_UNORM,
        DOE_DX_TEXTURE_RENDER_ATTACHMENT},&color,NULL)==DOE_DX_OK);
    assert(doe_dx_texture_create(context,&(doe_dx_texture_desc){4,4,DOE_DX_D32_FLOAT,
        DOE_DX_TEXTURE_DEPTH_ATTACHMENT},&depth,NULL)==DOE_DX_OK);
    doe_dx_commands *commands=NULL;
    assert(doe_dx_commands_create(context,&commands,NULL)==DOE_DX_OK);
    doe_dx_render_pass pass={.color=color,.depth=depth};
    mock.failure=MOCK_DRAW;
    assert(doe_dx_commands_render(commands,&pass,NULL)!=DOE_DX_OK);
    assert(!commands->texture_count && !color->initialized && !depth->initialized);
    mock.failure=MOCK_NONE;
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_OK);
    assert(commands->texture_count==2 && mock.render_depth.texture==depth->native &&
        mock.render_depth.clear && mock.render_depth.value==0 && !mock.render_depth.discard);
    assert(!depth->initialized && doe_dx_find_texture(commands,depth)->initialized);
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_OK);
    assert(!mock.render_depth.clear && commands->texture_count==2);
    pass.clear_depth=true; pass.depth_clear=0.75f; pass.discard_depth=true;
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_OK);
    assert(mock.render_depth.clear && mock.render_depth.value==0.75f && mock.render_depth.discard);
    size_t before=mock.render_passes;
    pass.depth_clear=2;
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_INVALID_ARGUMENT && mock.render_passes==before);
    pass.depth=NULL;
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_INVALID_ARGUMENT && mock.render_passes==before);
    doe_dx_texture_release(color); doe_dx_texture_release(depth);
    assert(mock.textures==2);
    doe_dx_commands_release(commands);
    assert(!mock.textures && !mock.commands && doe_dx_context_destroy(context)==DOE_DX_OK);
    mock_reset();
}
static void fixed_state(void)
{
    doe_dx_context *context=NULL;
    assert(doe_dx_context_create(&(doe_dx_options){0},&context,NULL)==DOE_DX_OK);
    unsigned char code=1;
    doe_dx_render_pipeline_desc desc={.color_format=DOE_DX_RGBA8_UNORM,.masks_explicit=true};
    for (size_t i=0;i<2;++i) desc.stages[i].resources.buffers=(doe_dx_pipeline_desc){&code,1,NULL,0};
    const doe_dx_topology topologies[]={DOE_DX_TRIANGLES,DOE_DX_TRIANGLE_STRIP,DOE_DX_LINES,DOE_DX_LINE_STRIP,DOE_DX_POINTS};
    const doe_dx_cull culls[]={DOE_DX_CULL_NONE,DOE_DX_CULL_FRONT,DOE_DX_CULL_BACK};
    for (size_t i=0;i<sizeof(topologies)/sizeof(*topologies);++i) {
        for (size_t j=0;j<sizeof(culls)/sizeof(*culls);++j) {
            desc.topology=topologies[i]; desc.cull=culls[j];
            doe_dx_pipeline *pipeline=NULL;
            assert(doe_dx_pipeline_create_render(context,&desc,&pipeline,NULL)==DOE_DX_OK);
            assert(mock.last_render_pipeline.topology==topologies[i] && mock.last_render_pipeline.cull==culls[j]);
            assert(mock.last_render_pipeline.masks_explicit && !mock.last_render_pipeline.sample_mask &&
                !mock.last_render_pipeline.color_write_mask);
            doe_dx_pipeline_release(pipeline);
        }
    }
    doe_dx_pipeline *pipeline=NULL;
    desc.blend_enabled=true;
    for (uint32_t factor=0;factor<=14;++factor) {
        desc.color_blend=(doe_dx_blend_component){factor,1,0};
        desc.alpha_blend=(doe_dx_blend_component){1,factor,0};
        doe_dx_status expected=(factor==12 || factor==13) ? DOE_DX_INVALID_ARGUMENT : DOE_DX_OK;
        assert(doe_dx_pipeline_create_render(context,&desc,&pipeline,NULL)==expected);
        if (expected==DOE_DX_OK) {
            assert(mock.last_render_pipeline.blend_enabled && mock.last_render_pipeline.color_blend.source==factor &&
                mock.last_render_pipeline.alpha_blend.destination==factor);
            doe_dx_pipeline_release(pipeline); pipeline=NULL;
        } else assert(!pipeline);
    }
    for (uint32_t operation=0;operation<=5;++operation) {
        desc.color_blend=desc.alpha_blend=(doe_dx_blend_component){1,1,operation};
        assert(doe_dx_pipeline_create_render(context,&desc,&pipeline,NULL)==
            (operation<=4 ? DOE_DX_OK : DOE_DX_INVALID_ARGUMENT));
        if (pipeline) { doe_dx_pipeline_release(pipeline); pipeline=NULL; }
    }
    desc.blend_enabled=false;
    desc.color_write_mask=16;
    assert(doe_dx_pipeline_create_render(context,&desc,&pipeline,NULL)==DOE_DX_INVALID_ARGUMENT && !pipeline);
    desc.color_write_mask=15; desc.cull=(doe_dx_cull)99;
    assert(doe_dx_pipeline_create_render(context,&desc,&pipeline,NULL)==DOE_DX_INVALID_ARGUMENT && !pipeline);
    desc.cull=DOE_DX_CULL_NONE; desc.topology=(doe_dx_topology)99;
    assert(doe_dx_pipeline_create_render(context,&desc,&pipeline,NULL)==DOE_DX_INVALID_ARGUMENT && !pipeline);
    assert(!mock.pipelines && doe_dx_context_destroy(context)==DOE_DX_OK);
    mock_reset();
}
int main(void)
{
    doe_dx_context *context=NULL;
    assert(doe_dx_context_create(&(doe_dx_options){0},&context,NULL)==DOE_DX_OK);
    doe_dx_texture *color=NULL;
    const doe_dx_texture_desc color_desc={4,4,DOE_DX_RGBA8_UNORM,
        DOE_DX_TEXTURE_RENDER_ATTACHMENT|DOE_DX_TEXTURE_COPY_SRC};
    assert(doe_dx_texture_create(context,&color_desc,&color,NULL)==DOE_DX_OK);
    doe_dx_binding_layout layouts[ROWS];
    doe_dx_sampler_layout samplers[SAMPLERS];
    doe_dx_sampler_desc values[SAMPLERS];
    for (size_t i=0;i<ROWS;++i) layouts[i]=(doe_dx_binding_layout){(uint32_t)i,0,4,DOE_DX_READ_ONLY,DOE_DX_STRUCTURED};
    for (size_t i=0;i<SAMPLERS;++i) {
        samplers[i]=(doe_dx_sampler_layout){(uint32_t)i,0,false};
        values[i]=(doe_dx_sampler_desc){.min_filter=DOE_DX_NEAREST,.mag_filter=DOE_DX_NEAREST,
            .mip_filter=DOE_DX_NEAREST,.address_u=DOE_DX_CLAMP,.address_v=DOE_DX_CLAMP,
            .address_w=DOE_DX_CLAMP,.max_lod=8,.max_anisotropy=1};
    }
    unsigned char bytecode=1;
    doe_dx_render_pipeline_desc desc={.color_format=DOE_DX_RGBA8_UNORM,.front_counter_clockwise=true};
    for (size_t stage=0;stage<STAGES;++stage)
        desc.stages[stage]=(doe_dx_dynamic_pipeline_desc){.resources={.buffers={&bytecode,1,layouts,ROWS}},
            .samplers=samplers,.sampler_count=SAMPLERS};
    doe_dx_pipeline *pipeline=NULL;
    /* Same native registers in independent stages are legal. */
    assert(doe_dx_pipeline_create_render(context,&desc,&pipeline,NULL)==DOE_DX_OK);
    layouts[0].shader_register=999;
    samplers[0].comparison=true;
    assert(pipeline->render->stages[0].bindings[0].shader_register==0 &&
        !pipeline->render->stages[1].samplers[0].comparison);
    doe_dx_buffer *upload=NULL,*buffers[STAGES][ROWS];
    doe_dx_buffer_binding bindings[STAGES][ROWS];
    assert(doe_dx_buffer_create(context,256,DOE_DX_UPLOAD,&upload,NULL)==DOE_DX_OK);
    doe_dx_commands *initialize=NULL;
    assert(doe_dx_commands_create(context,&initialize,NULL)==DOE_DX_OK);
    for (size_t stage=0;stage<STAGES;++stage) for (size_t i=0;i<ROWS;++i) {
        assert(doe_dx_buffer_create(context,256,DOE_DX_DEFAULT,&buffers[stage][i],NULL)==DOE_DX_OK);
        assert(doe_dx_commands_copy(initialize,upload,0,buffers[stage][i],0,256,NULL)==DOE_DX_OK);
        bindings[stage][i]=(doe_dx_buffer_binding){buffers[stage][i],0,256};
    }
    doe_dx_buffer_release(upload);
    assert(doe_dx_commands_submit(initialize,NULL)==DOE_DX_OK);
    doe_dx_commands_release(initialize);
    mock.completed=1;
    assert(doe_dx_context_poll(context,0,NULL)==DOE_DX_OK);
    doe_dx_render_draw packet=draw(pipeline);
    for (size_t stage=0;stage<STAGES;++stage)
        packet.stages[stage]=(doe_dx_dynamic_resources){.resources={bindings[stage],ROWS,NULL,0},
            .samplers=values,.sampler_count=SAMPLERS};
    doe_dx_render_pass pass={.color=color,.draws=&packet,.draw_count=1,.discard=true};
    for (size_t ordinal=1;ordinal<=4;++ordinal) {
        doe_dx_commands *failed=NULL;
        assert(doe_dx_commands_create(context,&failed,NULL)==DOE_DX_OK);
        mock.allocation_failure=mock.allocation_attempts+ordinal;
        assert(doe_dx_commands_render(failed,&pass,NULL)==DOE_DX_OUT_OF_MEMORY);
        mock.allocation_failure=0;
        assert(!failed->buffer_count && !failed->texture_count && !failed->pipeline_count && pipeline->references==1);
        assert(!mock.render_passes && !color->owner && !color->initialized);
        doe_dx_commands_release(failed);
    }
    doe_dx_commands *commands=NULL;
    assert(doe_dx_commands_create(context,&commands,NULL)==DOE_DX_OK);
    mock.failure=MOCK_DRAW;
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_NATIVE_FAILURE);
    assert(pipeline->references==1 && !commands->pipeline_count && !color->owner);
    mock.failure=MOCK_NONE;
    packet.viewport[2]=5;
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_UNSUPPORTED);
    packet.viewport[2]=4;
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_OK);
    assert(mock.render_clear && mock.render_discard && mock.render_draws==1);
    assert(commands->buffer_count==STAGES*ROWS && commands->pipeline_count==1 && pipeline->references==2);
    assert(mock.last_render_draw.stages[0].buffer_count==ROWS && mock.last_render_draw.stages[1].sampler_count==SAMPLERS);
    values[0].max_lod=2;
    assert(mock.last_render_draw.stages[1].samplers[0].max_lod==8);
    assert(!color->initialized && doe_dx_find_texture(commands,color)->initialized);
    /* Compatible reads across both stages still take one existing buffer lease. */
    bindings[1][0]=bindings[0][0];
    packet.vertex_count=0;
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_OK);
    assert(commands->buffer_count==STAGES*ROWS && commands->pipeline_count==2 && pipeline->references==3);
    doe_dx_render_pass empty={.color=color};
    assert(doe_dx_commands_render(commands,&empty,NULL)==DOE_DX_OK && !mock.render_clear);
    empty.clear=true;
    empty.clear_color[0]=2; /* UNORM clamp is native, not narrowed validation. */
    assert(doe_dx_commands_render(commands,&empty,NULL)==DOE_DX_OK && mock.render_clear);
    for (size_t stage=0;stage<STAGES;++stage) for (size_t i=0;i<ROWS;++i) doe_dx_buffer_release(buffers[stage][i]);
    doe_dx_texture_release(color);
    doe_dx_pipeline_release(pipeline);
    mock.failure=MOCK_SIGNAL;
    assert(doe_dx_commands_submit(commands,NULL)==DOE_DX_COMPLETION_UNKNOWN);
    doe_dx_commands_release(commands);
    assert(mock.buffers==STAGES*ROWS && mock.pipelines==1 && mock.textures==1);
    mock.failure=MOCK_NONE;
    mock.completed=2;
    assert(doe_dx_context_poll(context,0,NULL)==DOE_DX_OK);
    assert(!mock.buffers && !mock.pipelines && !mock.textures && !mock.commands);
    assert(doe_dx_context_destroy(context)==DOE_DX_OK);
    mock_reset();
    aliases();
    fixed_state();
    depth_pass();
    depth_only_pass();
    indirect_pass();
    puts("PASS D3D12 render owner: independent stage128/32 layouts, empty/zero draws, real-init requests, sampler snapshots, OOM/native rollback and unknown early-release leases; no SDK/GPU");
    return 0;
}
