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
    puts("PASS D3D12 render owner: independent stage128/32 layouts, empty/zero draws, real-init requests, sampler snapshots, OOM/native rollback and unknown early-release leases; no SDK/GPU");
    return 0;
}
