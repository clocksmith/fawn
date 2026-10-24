/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <math.h>

static bool add(size_t *value,size_t count)
{
    if (count>SIZE_MAX-*value) return false;
    *value+=count;
    return true;
}
static doe_dx_status prepare_draw(doe_dx_commands *commands,const doe_dx_render_draw *draw,
    doe_dx_texture *color,doe_dx_native_render_draw *out,doe_dx_reservation *needed,doe_dx_error *error)
{
    doe_dx_pipeline *pipeline=draw->pipeline;
    if (!pipeline || !pipeline->render || pipeline->context!=commands->context ||
        pipeline->color_format!=color->desc.format)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.draw.pipeline",error);
    if ((draw->vertex_count && draw->vertex_count-1>UINT32_MAX-draw->first_vertex) ||
        (draw->instance_count && draw->instance_count-1>UINT32_MAX-draw->first_instance))
        return doe_dx_fail(DOE_DX_RANGE,0,"render.draw.counts",error);
    for (size_t i=0;i<6;++i) if (!isfinite(draw->viewport[i]))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.viewport.finite",error);
    const float *v=draw->viewport;
    if (v[0]<0 || v[1]<0 || v[2]<0 || v[3]<0 || v[0]+v[2]>(float)color->desc.width ||
        v[1]+v[3]>(float)color->desc.height || v[4]<0 || v[5]>1 || v[4]>v[5])
        return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"render.viewport.bounds",error);
    const uint32_t *s=draw->scissor;
    if (s[0]>color->desc.width || s[2]>color->desc.width-s[0] ||
        s[1]>color->desc.height || s[3]>color->desc.height-s[1])
        return doe_dx_fail(DOE_DX_RANGE,0,"render.scissor",error);
    for (size_t stage=0;stage<DOE_DX_RENDER_STAGES;++stage) {
        doe_dx_reservation extra;
        doe_dx_status status=doe_dx_prepare_binding_plan(commands,&pipeline->render->stages[stage],
            &draw->stages[stage],&out->stages[stage],&extra,error);
        if (status!=DOE_DX_OK) return status;
        for (size_t i=0;i<draw->stages[stage].resources.texture_count;++i)
            if (draw->stages[stage].resources.textures[i]==color)
                return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"render.attachment_alias",error);
        if (!add(&needed->buffers,extra.buffers) || !add(&needed->textures,extra.textures))
            return doe_dx_fail(DOE_DX_RANGE,0,"render.reservation.range",error);
    }
    doe_dx_status status=doe_dx_stage_aliases(&pipeline->render->stages[0],&draw->stages[0],
        &pipeline->render->stages[1],&draw->stages[1]);
    if (status!=DOE_DX_OK) return doe_dx_fail(status,0,"render.stage_alias",error);
    status=doe_dx_prepare_render_vertices(commands,pipeline,draw,out,needed,error);
    if (status!=DOE_DX_OK) return status;
    out->pipeline=pipeline->native;
    out->vertex_count=draw->vertex_count;
    out->instance_count=draw->instance_count;
    out->first_vertex=draw->first_vertex;
    out->first_instance=draw->first_instance;
    memcpy(out->viewport,draw->viewport,sizeof(out->viewport));
    memcpy(out->scissor,draw->scissor,sizeof(out->scissor));
    return DOE_DX_OK;
}

doe_dx_status doe_dx_commands_render(doe_dx_commands *commands,
    const doe_dx_render_pass *pass,doe_dx_error *error)
{
    doe_dx_status status=doe_dx_recording(commands,error);
    if (status!=DOE_DX_OK) return status;
    if (!pass || (pass->draw_count && !pass->draws) ||
        pass->draw_count>SIZE_MAX/sizeof(doe_dx_native_render_draw))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.pass",error);
    status=doe_dx_texture_preflight(commands,pass->color,DOE_DX_TEXTURE_RENDER_ATTACHMENT,false);
    if (status!=DOE_DX_OK) return doe_dx_fail(status,0,"render.attachment",error);
    if (pass->color->desc.format!=DOE_DX_RGBA8_UNORM && pass->color->desc.format!=DOE_DX_BGRA8_UNORM)
        return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"render.attachment_format",error);
    for (size_t i=0;pass->clear && i<4;++i) if (!isfinite(pass->clear_color[i]))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.clear",error);
    doe_dx_native_render_draw *draws=pass->draw_count ? calloc(pass->draw_count,sizeof(*draws)) : NULL;
    if (pass->draw_count && !draws) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY,0,"render.draws.allocate",error);
    doe_dx_reservation needed={.textures=doe_dx_find_texture(commands,pass->color) ? 0u : 1u,
        .pipelines=pass->draw_count};
    for (size_t i=0;status==DOE_DX_OK && i<pass->draw_count;++i)
        status=prepare_draw(commands,&pass->draws[i],pass->color,&draws[i],&needed,error);
    if (status==DOE_DX_OK) status=doe_dx_reserve_uses(commands,needed,error);
    size_t retained=0;
    while (status==DOE_DX_OK && retained<pass->draw_count) {
        status=doe_dx_pipeline_retain(pass->draws[retained].pipeline);
        if (status==DOE_DX_OK) ++retained;
        else status=doe_dx_fail(status,0,"render.pipeline.retain",error);
    }
    if (status==DOE_DX_OK) {
        const doe_dx_texture_use *use=doe_dx_find_texture(commands,pass->color);
        bool initialized=use ? use->initialized : pass->color->initialized;
        const float zero[4]={0};
        status=doe_dx_native_render(commands->native,pass->color->native,pass->clear || !initialized,
            pass->clear ? pass->clear_color : zero,pass->discard,draws,pass->draw_count,error);
    }
    if (status==DOE_DX_OK) {
        for (size_t i=0;i<pass->draw_count;++i) {
            const doe_dx_render_draw *draw=&pass->draws[i];
            for (size_t stage=0;stage<DOE_DX_RENDER_STAGES;++stage) {
                const doe_dx_compute_resources *values=&draw->stages[stage].resources;
                for (size_t j=0;j<values->buffer_count;++j) doe_dx_use_buffer(commands,values->buffers[j].buffer);
                for (size_t j=0;j<values->texture_count;++j) doe_dx_use_texture(commands,values->textures[j]);
            }
            for (size_t slot=0;slot<draw->vertex_buffer_count;++slot)
                if (draw->vertices[slot].buffer) doe_dx_use_buffer(commands,draw->vertices[slot].buffer);
            commands->pipelines[commands->pipeline_count++]=draw->pipeline;
        }
        doe_dx_use_texture(commands,pass->color);
        doe_dx_find_texture(commands,pass->color)->initialized=true;
    } else for (size_t i=0;i<retained;++i) doe_dx_pipeline_release(pass->draws[i].pipeline);
    free(draws);
    return status==DOE_DX_OK ? doe_dx_fail(status,0,"render.pass",error) :
        doe_dx_context_result(commands->context,status,error);
}
