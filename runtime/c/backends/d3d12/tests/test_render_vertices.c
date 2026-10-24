/* SPDX-License-Identifier: MIT */
#include "mock_native.h"
#include <stdio.h>

static doe_dx_render_pipeline_desc description(doe_dx_vertex_layout *layouts,doe_dx_vertex_attribute *attributes)
{
    static const unsigned char code=1;
    doe_dx_render_pipeline_desc desc={.color_format=DOE_DX_RGBA8_UNORM,
        .vertex_buffers=layouts,.vertex_buffer_count=3,.attributes=attributes,.attribute_count=2};
    for (size_t i=0;i<2;++i) desc.stages[i].resources.buffers=(doe_dx_pipeline_desc){.bytecode=&code,.bytecode_size=1};
    layouts[0]=(doe_dx_vertex_layout){0,DOE_DX_PER_VERTEX}; /* Empty public hole. */
    layouts[1]=(doe_dx_vertex_layout){8,DOE_DX_PER_VERTEX};
    layouts[2]=(doe_dx_vertex_layout){0,DOE_DX_PER_INSTANCE};
    attributes[0]=(doe_dx_vertex_attribute){0,1,0,DOE_DX_FLOAT32X2};
    attributes[1]=(doe_dx_vertex_attribute){1,2,4,DOE_DX_UINT8X4};
    return desc;
}
static void formats(doe_dx_context *context)
{
    /* Full enum span is additive: old 12 identities followed by 29 new formats. */
    assert(DOE_DX_FLOAT32==0 && DOE_DX_SINT32X4==11 && DOE_DX_UNORM8X4_BGRA==40);
    for (unsigned value=0;value<=40;++value) {
        doe_dx_vertex_layout layout={32,DOE_DX_PER_INSTANCE};
        doe_dx_vertex_attribute attribute={0,0,8,(doe_dx_vertex_format)value};
        static const unsigned char code=1;
        doe_dx_render_pipeline_desc desc={.color_format=DOE_DX_RGBA8_UNORM,
            .vertex_buffers=&layout,.vertex_buffer_count=1,.attributes=&attribute,.attribute_count=1};
        for (size_t i=0;i<2;++i) desc.stages[i].resources.buffers=(doe_dx_pipeline_desc){.bytecode=&code,.bytecode_size=1};
        doe_dx_pipeline *pipeline=NULL;
        assert(doe_dx_pipeline_create_render(context,&desc,&pipeline,NULL)==DOE_DX_OK);
        assert(pipeline->vertex_buffer_count==1 && pipeline->vertex_ends[0]>8 && pipeline->vertex_ends[0]<=24);
        layout.stride=2048;
        assert(pipeline->vertex_buffers[0].stride==32);
        doe_dx_pipeline_release(pipeline);
    }
}
int main(void)
{
    doe_dx_context *context=NULL;
    assert(doe_dx_context_create(NULL,&context,NULL)==DOE_DX_OK);
    formats(context);
    doe_dx_vertex_layout layouts[3]; doe_dx_vertex_attribute attributes[2];
    doe_dx_render_pipeline_desc desc=description(layouts,attributes);
    doe_dx_pipeline *pipeline=NULL;
    attributes[1].offset=3;
    assert(doe_dx_pipeline_create_render(context,&desc,&pipeline,NULL)==DOE_DX_RANGE && !pipeline);
    attributes[1].format=DOE_DX_UNORM8;
    assert(doe_dx_pipeline_create_render(context,&desc,&pipeline,NULL)==DOE_DX_OK);
    doe_dx_pipeline_release(pipeline); pipeline=NULL;
    desc=description(layouts,attributes);
    assert(doe_dx_pipeline_create_render(context,&desc,&pipeline,NULL)==DOE_DX_OK);
    assert(pipeline->vertex_ends[0]==0 && pipeline->vertex_ends[1]==8 && pipeline->vertex_ends[2]==8);
    doe_dx_buffer *upload=NULL,*buffer=NULL;
    assert(doe_dx_buffer_create(context,256,DOE_DX_UPLOAD,&upload,NULL)==DOE_DX_OK);
    assert(doe_dx_buffer_create(context,256,DOE_DX_DEFAULT,&buffer,NULL)==DOE_DX_OK);
    doe_dx_commands *commands=NULL;
    assert(doe_dx_commands_create(context,&commands,NULL)==DOE_DX_OK);
    assert(doe_dx_commands_copy(commands,upload,0,buffer,0,256,NULL)==DOE_DX_OK);
    doe_dx_buffer_release(upload);
    assert(doe_dx_commands_submit(commands,NULL)==DOE_DX_OK);
    doe_dx_commands_release(commands);
    mock.completed=1;
    assert(doe_dx_context_poll(context,0,NULL)==DOE_DX_OK);
    assert(doe_dx_commands_create(context,&commands,NULL)==DOE_DX_OK);
    doe_dx_buffer_binding vertices[3]={{0},{buffer,8,24},{buffer,128,8}};
    doe_dx_render_draw draw={.pipeline=pipeline,.vertex_count=3,.instance_count=2,.first_instance=7,
        .viewport={0,0,4,4,0,1},.scissor={0,0,4,4},.vertices=vertices,.vertex_buffer_count=3};
    doe_dx_texture *color=NULL;
    assert(doe_dx_texture_create(context,&(doe_dx_texture_desc){4,4,DOE_DX_RGBA8_UNORM,
        DOE_DX_TEXTURE_RENDER_ATTACHMENT},&color,NULL)==DOE_DX_OK);
    doe_dx_render_pass pass={.color=color,.draws=&draw,.draw_count=1};
    draw.first_vertex=1;
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_RANGE);
    draw.first_vertex=0;
    vertices[2].size=7;
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_RANGE);
    vertices[2].size=8;
    vertices[1].offset=2;
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_ALIGNMENT);
    vertices[1].offset=8;
    assert(!mock.render_passes && !color->owner && pipeline->references==1);
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_OK);
    assert(commands->buffer_count==1 && commands->pipeline_count==1 && mock.last_render_draw.vertex_buffer_count==3);
    assert(!mock.last_render_draw.vertices[0].buffer && mock.last_render_draw.vertices[1].offset==8 &&
        mock.last_render_draw.vertices[2].size==8 && mock.last_render_draw.first_instance==7);
    /* Zero stride fetches the same admitted bytes even at the last u32 index. */
    draw.first_instance=UINT32_MAX; draw.instance_count=1;
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_OK &&
        mock.last_render_draw.first_instance==UINT32_MAX && commands->buffer_count==1);
    draw.first_instance=7; draw.instance_count=2;
    /* A read shader alias and repeated vertex views keep one allocation lease. */
    const doe_dx_binding_layout read={0,0,4,DOE_DX_READ_ONLY,DOE_DX_STRUCTURED};
    desc.stages[0].resources.buffers.bindings=&read; desc.stages[0].resources.buffers.binding_count=1;
    doe_dx_pipeline *read_pipeline=NULL,*write_pipeline=NULL;
    assert(doe_dx_pipeline_create_render(context,&desc,&read_pipeline,NULL)==DOE_DX_OK);
    const doe_dx_binding_layout write={0,0,4,DOE_DX_READ_WRITE,DOE_DX_STRUCTURED};
    desc.stages[0].resources.buffers.bindings=&write;
    assert(doe_dx_pipeline_create_render(context,&desc,&write_pipeline,NULL)==DOE_DX_OK);
    const doe_dx_buffer_binding shader={buffer,0,256};
    draw.stages[0].resources=(doe_dx_compute_resources){&shader,1,NULL,0};
    draw.pipeline=write_pipeline;
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_UNSUPPORTED && commands->pipeline_count==2);
    draw.pipeline=read_pipeline;
    assert(doe_dx_commands_render(commands,&pass,NULL)==DOE_DX_OK && commands->buffer_count==1);
    doe_dx_pipeline_release(pipeline); doe_dx_pipeline_release(read_pipeline); doe_dx_pipeline_release(write_pipeline);
    doe_dx_texture_release(color); doe_dx_buffer_release(buffer);
    mock.failure=MOCK_SIGNAL;
    assert(doe_dx_commands_submit(commands,NULL)==DOE_DX_COMPLETION_UNKNOWN);
    doe_dx_commands_release(commands);
    assert(mock.buffers==1 && mock.textures==1 && mock.pipelines==2);
    mock.failure=MOCK_NONE; mock.completed=mock.last_signal;
    assert(doe_dx_context_poll(context,0,NULL)==DOE_DX_OK);
    assert(!mock.buffers && !mock.textures && !mock.pipelines && !mock.commands);
    assert(doe_dx_context_destroy(context)==DOE_DX_OK);
    mock_reset();
    puts("PASS direct render vertices: 41 typed formats, immutable strides, holes/zero-stride instancing, fetch spans/alignment, shared-read aliases, writable rejection and early-release unknown retirement; no SDK/GPU");
    return 0;
}
