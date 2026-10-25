/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <math.h>

static bool add(size_t *value,size_t count)
{
    if (count>SIZE_MAX-*value) return false;
    *value+=count;
    return true;
}
static doe_dx_status prepare_indirect(doe_dx_commands *commands,const doe_dx_render_draw *draw,
    doe_dx_native_render_draw *out,doe_dx_reservation *needed,doe_dx_error *error)
{
    if (!draw->indirect) return draw->indices ? doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.index_without_indirect",error) : DOE_DX_OK;
    if (draw->indices && draw->index_format!=DOE_DX_INDEX_UINT16 && draw->index_format!=DOE_DX_INDEX_UINT32)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.index_format",error);
    const doe_dx_render_layout *layout=draw->pipeline->render;
    if (draw->indices && (layout->topology==DOE_DX_LINE_STRIP || layout->topology==DOE_DX_TRIANGLE_STRIP) &&
        (!layout->strip_indexed || layout->strip_index_format!=draw->index_format))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.strip_index_format",error);
    const doe_dx_buffer_binding *views[2]={draw->indirect,draw->indices};
    for (size_t i=0;i<2;++i) {
        const doe_dx_buffer_binding *view=views[i];
        if (!view) continue;
        doe_dx_status status=doe_dx_buffer_preflight(commands,view->buffer,true);
        if (status!=DOE_DX_OK) return doe_dx_fail(status,0,"render.indirect_buffer",error);
        uint32_t alignment=i && draw->index_format==DOE_DX_INDEX_UINT16 ? 2u : 4u;
        if (view->buffer->memory!=DOE_DX_DEFAULT || view->offset%alignment ||
            !doe_dx_range(view->buffer->size,view->offset,view->size) ||
            (i ? view->size>UINT32_MAX || view->size%alignment : view->size<(draw->indices ? 20u : 16u)))
            return doe_dx_fail(DOE_DX_RANGE,0,"render.indirect_view",error);
        for (size_t stage=0;stage<DOE_DX_RENDER_STAGES;++stage) {
            const doe_dx_binding_plan *plan=&draw->pipeline->render->stages[stage];
            for (size_t j=0;j<plan->binding_count;++j)
                if (draw->stages[stage].resources.buffers[j].buffer==view->buffer &&
                    doe_dx_buffer_descriptor_kind(&plan->bindings[j])==DOE_DX_DESCRIPTOR_UAV)
                    return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"render.indirect_write_alias",error);
        }
        if (!doe_dx_find_buffer(commands,view->buffer) && !add(&needed->buffers,1))
            return doe_dx_fail(DOE_DX_RANGE,0,"render.indirect_reservation",error);
        doe_dx_native_binding native={view->buffer->native,view->offset,view->size};
        if (i) out->indices=native; else out->indirect=native;
    }
    out->index_format=draw->index_format;
    return DOE_DX_OK;
}
static doe_dx_status prepare_draw(doe_dx_commands *commands,const doe_dx_render_draw *draw,
    const doe_dx_render_color *colors,uint32_t color_count,doe_dx_texture *depth,doe_dx_native_render_draw *out,doe_dx_reservation *needed,doe_dx_error *error)
{
    if (draw->query) {
        if (draw->pipeline || draw->indirect || draw->indices || draw->vertex_buffer_count)
            return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.query_event",error);
        doe_dx_status status=doe_dx_render_query_preflight(commands,draw->query,draw->query_index,needed);
        if (status!=DOE_DX_OK) return doe_dx_fail(status,0,"render.query",error);
        out->query=draw->query->native; out->query_index=draw->query_index; out->query_begin=draw->query_begin;
        return DOE_DX_OK;
    }
    doe_dx_pipeline *pipeline=draw->pipeline;
    if (!pipeline || !pipeline->render || pipeline->context!=commands->context ||
        pipeline->depth_enabled!=(depth!=NULL))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.draw.pipeline",error);
    doe_dx_texture *extent=depth;
    uint32_t slots=color_count>pipeline->render->target_count ? color_count : pipeline->render->target_count;
    for (uint32_t i=0;i<slots;++i) {
        doe_dx_texture *texture=i<color_count ? colors[i].texture : NULL;
        const doe_dx_render_target *target=i<pipeline->render->target_count ? &pipeline->render->targets[i] : NULL;
        if ((texture!=NULL)!=(target && !target->absent) || (texture && texture->desc.format!=target->format))
            return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.draw.targets",error);
        if (texture) extent=texture;
    }
    if (depth && pipeline->render->depth_format!=depth->desc.format)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.draw.depth_format",error);
    if (!extent || pipeline->render->samples!=extent->samples)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.draw.samples",error);
    if (!draw->indirect && ((draw->vertex_count && draw->vertex_count-1>UINT32_MAX-draw->first_vertex) ||
        (draw->instance_count && draw->instance_count-1>UINT32_MAX-draw->first_instance)))
        return doe_dx_fail(DOE_DX_RANGE,0,"render.draw.counts",error);
    for (size_t i=0;i<4;++i) if (!isfinite(draw->blend_constant[i]))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.blend_constant.finite",error);
    for (size_t i=0;i<6;++i) if (!isfinite(draw->viewport[i]))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.viewport.finite",error);
    const float *v=draw->viewport;
    if (v[0]<0 || v[1]<0 || v[2]<0 || v[3]<0 || v[0]+v[2]>(float)extent->desc.width ||
        v[1]+v[3]>(float)extent->desc.height || v[4]<0 || v[5]>1 || v[4]>v[5])
        return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"render.viewport.bounds",error);
    const uint32_t *s=draw->scissor;
    if (s[0]>extent->desc.width || s[2]>extent->desc.width-s[0] ||
        s[1]>extent->desc.height || s[3]>extent->desc.height-s[1])
        return doe_dx_fail(DOE_DX_RANGE,0,"render.scissor",error);
    for (size_t stage=0;stage<DOE_DX_RENDER_STAGES;++stage) {
        doe_dx_reservation extra;
        doe_dx_status status=doe_dx_prepare_binding_plan(commands,&pipeline->render->stages[stage],
            &draw->stages[stage],&out->stages[stage],&extra,error);
        if (status!=DOE_DX_OK) return status;
        for (size_t i=0;i<draw->stages[stage].resources.texture_count;++i) {
            doe_dx_texture *bound=draw->stages[stage].resources.textures[i];
            if (bound==depth) return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"render.attachment_alias",error);
            for (uint32_t slot=0;slot<color_count;++slot)
                if (bound==colors[slot].texture || bound==colors[slot].resolve)
                    return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"render.attachment_alias",error);
        }
        if (!add(&needed->buffers,extra.buffers) || !add(&needed->textures,extra.textures))
            return doe_dx_fail(DOE_DX_RANGE,0,"render.reservation.range",error);
    }
    doe_dx_status status=doe_dx_stage_aliases(&pipeline->render->stages[0],&draw->stages[0],
        &pipeline->render->stages[1],&draw->stages[1]);
    if (status!=DOE_DX_OK) return doe_dx_fail(status,0,"render.stage_alias",error);
    status=doe_dx_prepare_render_vertices(commands,pipeline,draw,out,needed,error);
    if (status!=DOE_DX_OK) return status;
    status=prepare_indirect(commands,draw,out,needed,error);
    if (status!=DOE_DX_OK) return status;
    out->stencil_reference=draw->stencil_reference;
    out->pipeline=pipeline->native;
    out->vertex_count=draw->vertex_count;
    out->instance_count=draw->instance_count;
    out->first_vertex=draw->first_vertex;
    out->first_instance=draw->first_instance;
    memcpy(out->blend_constant,draw->blend_constant,sizeof(out->blend_constant));
    memcpy(out->viewport,draw->viewport,sizeof(out->viewport));
    memcpy(out->scissor,draw->scissor,sizeof(out->scissor));
    return DOE_DX_OK;
}

static doe_dx_status prepare_targets(doe_dx_commands *commands,const doe_dx_render_pass *pass,
    const doe_dx_render_color *colors,uint32_t count,doe_dx_reservation *needed,
    doe_dx_native_render_color native[DOE_DX_MAX_COLOR_TARGETS],doe_dx_error *error)
{
    doe_dx_texture *extent=pass->depth;
    doe_dx_texture *seen[DOE_DX_MAX_COLOR_TARGETS*2+1];
    size_t seen_count=0;
    if (pass->depth) seen[seen_count++]=pass->depth;
    for (uint32_t i=0;i<count;++i) {
        const doe_dx_render_color *color=&colors[i];
        if (!color->texture) {
            if (color->resolve || color->clear || color->discard)
                return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.color_absent",error);
            continue;
        }
        doe_dx_status status=doe_dx_texture_preflight(commands,color->texture,DOE_DX_TEXTURE_RENDER_ATTACHMENT,false);
        if (status!=DOE_DX_OK) return doe_dx_fail(status,0,"render.attachment",error);
        if (!doe_dx_format_renderable(color->texture->desc.format))
            return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"render.attachment_format",error);
        if (extent && (color->texture->desc.width!=extent->desc.width ||
            color->texture->desc.height!=extent->desc.height || color->texture->samples!=extent->samples))
            return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.attachment_shape",error);
        extent=color->texture;
        doe_dx_format_info info;
        if (!doe_dx_format_properties(color->texture->desc.format,&info))
            return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"render.format",error);
        if (color->clear && info.kind==1) for (size_t lane=0;lane<4;++lane) if (!isfinite(color->value[lane]))
            return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.clear",error);
        doe_dx_texture *resources[2]={color->texture,color->resolve};
        for (size_t j=0;j<2;++j) if (resources[j]) {
            for (size_t prior=0;prior<seen_count;++prior) if (seen[prior]==resources[j])
                return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.attachment_alias",error);
            seen[seen_count++]=resources[j];
            if (!doe_dx_find_texture(commands,resources[j])) ++needed->textures;
        }
        if (color->resolve) {
            status=doe_dx_texture_preflight(commands,color->resolve,DOE_DX_TEXTURE_RENDER_ATTACHMENT,false);
            if (status!=DOE_DX_OK) return doe_dx_fail(status,0,"render.resolve",error);
            if (color->texture->samples!=4 || color->resolve->samples!=1 ||
                color->resolve->desc.format!=color->texture->desc.format ||
                color->resolve->desc.width!=color->texture->desc.width || color->resolve->desc.height!=color->texture->desc.height)
                return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.resolve.shape",error);
        }
        const doe_dx_texture_use *use=doe_dx_find_texture(commands,color->texture);
        bool initialized=use ? use->initialized : color->texture->initialized;
        native[i]=(doe_dx_native_render_color){.texture=color->texture->native,
            .resolve=color->resolve ? color->resolve->native : NULL,.clear=color->clear || !initialized,.discard=color->discard};
        if (color->clear) memcpy(native[i].value,color->value,sizeof(native[i].value));
    }
    if (!extent) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.attachments_absent",error);
    return DOE_DX_OK;
}

doe_dx_status doe_dx_commands_render(doe_dx_commands *commands,const doe_dx_render_pass *pass,doe_dx_error *error)
{
    doe_dx_status status=doe_dx_recording(commands,error);
    if (status!=DOE_DX_OK) return status;
    if (!pass || (pass->draw_count && !pass->draws) || pass->draw_count>SIZE_MAX/sizeof(doe_dx_native_render_draw))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.pass",error);
    doe_dx_render_color legacy={.texture=pass->color,.resolve=pass->resolve,.clear=pass->clear,.discard=pass->discard};
    memcpy(legacy.value,pass->clear_color,sizeof(legacy.value));
    const doe_dx_render_color *colors=pass->colors_explicit ? pass->colors : &legacy;
    uint32_t color_count=pass->colors_explicit ? pass->color_count : pass->color ? 1u : 0u;
    if (color_count>DOE_DX_MAX_COLOR_TARGETS || (color_count && !colors) ||
        (!pass->colors_explicit && !pass->color && (pass->clear || pass->discard || pass->resolve)))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.color_count",error);
    if (pass->depth) {
        status=doe_dx_texture_preflight(commands,pass->depth,DOE_DX_TEXTURE_DEPTH_ATTACHMENT,false);
        if (status!=DOE_DX_OK) return doe_dx_fail(status,0,"render.depth",error);
        if (!doe_dx_format_depth(pass->depth->desc.format))
            return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.depth.format",error);
        if (pass->clear_depth && (!isfinite(pass->depth_clear) || pass->depth_clear<0 || pass->depth_clear>1))
            return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.depth.clear",error);
        if ((pass->clear_stencil || pass->discard_stencil) && !doe_dx_format_stencil(pass->depth->desc.format))
            return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.stencil.format",error);
        if ((pass->depth_read_only && (pass->clear_depth || pass->discard_depth)) ||
            (pass->stencil_read_only && (pass->clear_stencil || pass->discard_stencil)))
            return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.read_only_clear",error);
    } else if (pass->clear_depth || pass->discard_depth || pass->clear_stencil || pass->discard_stencil)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.depth.absent",error);
    doe_dx_reservation needed={.textures=pass->depth && !doe_dx_find_texture(commands,pass->depth) ? 1u : 0u,
        .pipelines=pass->draw_count};
    doe_dx_native_render_color native_colors[DOE_DX_MAX_COLOR_TARGETS]={0};
    status=prepare_targets(commands,pass,colors,color_count,&needed,native_colors,error);
    if (status!=DOE_DX_OK) return status;
    doe_dx_native_render_draw *draws=pass->draw_count ? calloc(pass->draw_count,sizeof(*draws)) : NULL;
    if (pass->draw_count && !draws) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY,0,"render.draws.allocate",error);
    doe_dx_queries *active_query=NULL;
    uint32_t active_index=0;
    for (size_t i=0;status==DOE_DX_OK && i<pass->draw_count;++i) {
        const doe_dx_render_draw *draw=&pass->draws[i];
        if (draw->query) {
            if (draw->query_begin ? active_query!=NULL : active_query!=draw->query || active_index!=draw->query_index) {
                status=doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.query_balance",error); break;
            }
            active_query=draw->query_begin ? draw->query : NULL;
            active_index=draw->query_index;
        }
        status=prepare_draw(commands,draw,colors,color_count,pass->depth,&draws[i],&needed,error);
        if (status==DOE_DX_OK && draw->pipeline &&
            ((pass->depth_read_only && draw->pipeline->render->depth_write) ||
             (pass->stencil_read_only && draw->pipeline->render->stencil_write)))
            status=doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.read_only_write",error);
    }
    if (status==DOE_DX_OK && active_query) status=doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"render.query_unclosed",error);
    if (status==DOE_DX_OK) status=doe_dx_reserve_uses(commands,needed,error);
    size_t retained=0;
    while (status==DOE_DX_OK && retained<pass->draw_count) {
        status=pass->draws[retained].query ? DOE_DX_OK : doe_dx_pipeline_retain(pass->draws[retained].pipeline);
        if (status==DOE_DX_OK) ++retained;
        else status=doe_dx_fail(status,0,"render.pipeline.retain",error);
    }
    if (status==DOE_DX_OK) {
        doe_dx_native_render_depth depth={0};
        if (pass->depth) {
            const doe_dx_texture_use *use=doe_dx_find_texture(commands,pass->depth);
            bool initialized=use ? use->initialized : pass->depth->initialized;
            depth=(doe_dx_native_render_depth){.texture=pass->depth->native,.clear=pass->clear_depth || !initialized,
                .discard=pass->discard_depth,.value=pass->clear_depth ? pass->depth_clear : 0,
                .clear_stencil=pass->clear_stencil || (!initialized && doe_dx_format_stencil(pass->depth->desc.format)),
                .discard_stencil=pass->discard_stencil,.stencil=pass->clear_stencil ? pass->stencil_clear & 255u : 0};
        }
        status=doe_dx_native_render_targets(commands->native,native_colors,color_count,
            pass->depth ? &depth : NULL,draws,pass->draw_count,error);
    }
    if (status==DOE_DX_OK) {
        for (size_t i=0;i<pass->draw_count;++i) {
            const doe_dx_render_draw *draw=&pass->draws[i];
            if (draw->query) {
                doe_dx_use_render_query(commands,draw->query,draw->query_index,!draw->query_begin);
                continue;
            }
            for (size_t stage=0;stage<DOE_DX_RENDER_STAGES;++stage) {
                const doe_dx_compute_resources *values=&draw->stages[stage].resources;
                for (size_t j=0;j<values->buffer_count;++j) doe_dx_use_buffer(commands,values->buffers[j].buffer);
                for (size_t j=0;j<values->texture_count;++j) doe_dx_use_texture(commands,values->textures[j]);
            }
            for (size_t slot=0;slot<draw->vertex_buffer_count;++slot)
                if (draw->vertices[slot].buffer) doe_dx_use_buffer(commands,draw->vertices[slot].buffer);
            if (draw->indirect) doe_dx_use_buffer(commands,draw->indirect->buffer);
            if (draw->indices) doe_dx_use_buffer(commands,draw->indices->buffer);
            commands->pipelines[commands->pipeline_count++]=draw->pipeline;
        }
        if (pass->depth) {
            doe_dx_use_texture(commands,pass->depth);
            doe_dx_find_texture(commands,pass->depth)->initialized=true;
        }
        for (uint32_t i=0;i<color_count;++i) {
            doe_dx_texture *resources[2]={colors[i].texture,colors[i].resolve};
            for (size_t j=0;j<2;++j) if (resources[j]) {
                doe_dx_use_texture(commands,resources[j]);
                doe_dx_find_texture(commands,resources[j])->initialized=true;
            }
        }
    } else for (size_t i=0;i<retained;++i) doe_dx_pipeline_release(pass->draws[i].pipeline);
    free(draws);
    return status==DOE_DX_OK ? doe_dx_fail(status,0,"render.pass",error) : doe_dx_context_result(commands->context,status,error);
}
