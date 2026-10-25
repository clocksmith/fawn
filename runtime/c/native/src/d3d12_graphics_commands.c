/* SPDX-License-Identifier: MIT */
#include "d3d12_draw_guard.h"
#include <stdlib.h>

#if defined(_WIN32)
static bool same_targets(const doe_resource_render_targets *a,const doe_resource_render_targets *b)
{
    if (a->color_count>DOE_DX_MAX_COLOR_TARGETS || a->color_count!=b->color_count ||
        a->depth_stencil!=b->depth_stencil || a->occlusion_queries!=b->occlusion_queries) return false;
    for (uint32_t i=0;i<a->color_count;++i)
        if (a->colors[i].view!=b->colors[i].view || a->colors[i].resolve_target!=b->colors[i].resolve_target ||
            a->colors[i].has_depth_slice || b->colors[i].has_depth_slice) return false;
    return true;
}
static native_texture *attachment_texture(doe_resource_view *view)
{
    if (!view) return NULL;
    const doe_resource_view_desc *span=doe_resource_view_description(view);
    doe_resource_texture *texture=doe_resource_view_texture(view);
    const doe_resource_texture_desc *desc=doe_resource_texture_description(texture);
    native_texture *native=doe_resource_texture_native(texture);
    if (!doe_resource_view_native(view) || !span || !desc || !native || !native->d3d12 || span->format!=desc->format ||
        span->dimension!=WGPUTextureViewDimension_2D || span->aspect!=WGPUTextureAspect_All ||
        span->base_mip || span->mip_count!=1 || span->base_layer || span->layer_count!=1) return NULL;
    return native;
}
static native_failure color_attachment(const doe_resource_color_attachment *source,doe_dx_render_color *out)
{
    if (!source->view) return native_failure_status(source->resolve_target ? NATIVE_INVALID_ARGUMENT : NATIVE_OK);
    native_texture *texture=attachment_texture(source->view);
    if (!texture) return native_failure_status(NATIVE_UNSUPPORTED);
    out->texture=texture->d3d12;
    if (source->resolve_target) {
        texture=attachment_texture(source->resolve_target);
        if (!texture) return native_failure_status(NATIVE_UNSUPPORTED);
        out->resolve=texture->d3d12;
    }
    if (source->load==WGPULoadOp_Clear) {
        doe_vk_clear_color value;
        if (!native_render_clear(doe_resource_view_description(source->view)->format,source->clear,&value))
            return native_failure_status(NATIVE_INVALID_ARGUMENT);
        out->clear=true; memcpy(out->value,value.float32,sizeof(out->value));
    } else if (source->load!=WGPULoadOp_Load) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    out->discard=source->store==WGPUStoreOp_Discard;
    if (!out->discard && source->store!=WGPUStoreOp_Store) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    return native_failure_status(NATIVE_OK);
}
static native_failure prepare_draw(const doe_resource_render_packet *packet,
    doe_dx_render_draw *out,native_d3d12_graphics_snapshot *storage)
{
    native_pipeline *pipeline=doe_resource_render_pipeline_native(packet->pipeline);
    if (!pipeline || !pipeline->d3d12_graphics )
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    doe_resource_binding resolved[NATIVE_MAX_PIPELINE_BINDINGS];
    if (native_resolve_resources(pipeline,doe_resource_render_pipeline_get_layout(packet->pipeline),
        packet->groups,&packet->offsets,resolved)!=DOE_RESOURCE_OK) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    *out=(doe_dx_render_draw){.pipeline=pipeline->handle.d3d12,.vertex_count=packet->count,
        .instance_count=packet->instances,.first_vertex=packet->first,.first_instance=packet->first_instance};
    for (size_t stage=0;stage<pipeline->d3d12_graphics->stage_count;++stage) {
        native_failure failure=native_d3d12_binding_values(pipeline,&pipeline->d3d12_graphics->stages[stage],
            resolved,&storage->stages[stage]);
        if (failure.status!=NATIVE_OK) return failure;
        out->stages[stage]=storage->stages[stage].values;
    }
    out->vertices=storage->vertices;
    out->vertex_buffer_count=pipeline->vertex_count;
    for (size_t i=0;i<pipeline->vertex_count;++i) {
        native_buffer *buffer=doe_resource_buffer_native(packet->vertices[i].buffer);
        if (!buffer) {
            if (doe_resource_render_pipeline_vertex_required(packet->pipeline,i))
                return native_failure_status(NATIVE_INVALID_STATE);
            continue;
        }
        native_d3d12_buffer *native=buffer->handle.d3d12;
        uint64_t offset=packet->vertices[i].offset,size=packet->vertices[i].size;
        if (!native || offset>native->allocation_size || size>native->allocation_size-offset)
            return native_failure_status(NATIVE_INVALID_ARGUMENT);
        storage->vertices[i]=(doe_dx_buffer_binding){native->allocation,offset,size};
    }
    doe_vk_clear_color blend;
    if (!native_render_clear(WGPUTextureFormat_RGBA32Float,packet->state.blend,&blend))
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    out->stencil_reference=packet->state.stencil_reference;
    memcpy(out->blend_constant,blend.float32,sizeof(out->blend_constant));
    memcpy(out->viewport,packet->state.viewport,sizeof(out->viewport));
    memcpy(out->scissor,packet->state.scissor,sizeof(out->scissor));
    return native_failure_status(NATIVE_OK);
}
#endif

native_failure native_d3d12_render(native_command_handle *commands,
    const doe_resource_command *records,size_t count,size_t *consumed)
{
#if defined(_WIN32)
    if (!commands || !records || !count || !consumed || records[0].kind!=DOE_COMMAND_RENDER)
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    const doe_resource_render_packet *begin=doe_resource_render_record_borrow(records[0].value.render);
    if (!begin || !begin->boundary_only || !begin->first_in_pass || begin->last_in_pass ||
        !same_targets(&begin->targets,&begin->targets)) return native_failure_status(NATIVE_UNSUPPORTED);
    size_t end=0;
    for (size_t i=1;i<count;++i) {
        if (records[i].kind==DOE_COMMAND_BEGIN_OCCLUSION || records[i].kind==DOE_COMMAND_END_OCCLUSION) {
            if (records[i].value.timestamp.set!=begin->targets.occlusion_queries) return native_failure_status(NATIVE_INVALID_ARGUMENT);
            continue;
        }
        if (records[i].kind!=DOE_COMMAND_RENDER) return native_failure_status(NATIVE_UNSUPPORTED);
        const doe_resource_render_packet *packet=doe_resource_render_record_borrow(records[i].value.render);
        if (!packet || packet->first_in_pass || !same_targets(&begin->targets,&packet->targets))
            return native_failure_status(NATIVE_INVALID_ARGUMENT);
        if (packet->boundary_only) {
            if (!packet->last_in_pass) return native_failure_status(NATIVE_INVALID_ARGUMENT);
            end=i;
            break;
        }
        if (packet->last_in_pass) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    }
    if (!end) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    doe_dx_render_color colors[DOE_DX_MAX_COLOR_TARGETS]={0};
    for (uint32_t i=0;i<begin->targets.color_count;++i) {
        native_failure result=color_attachment(&begin->targets.colors[i],&colors[i]);
        if (result.status!=NATIVE_OK) return result;
    }
    size_t draws_count=end-1;
    if (draws_count>SIZE_MAX/sizeof(native_d3d12_graphics_snapshot) || draws_count>SIZE_MAX/sizeof(doe_dx_render_draw))
        return native_failure_status(NATIVE_OUT_OF_MEMORY);
    native_d3d12_graphics_snapshot *storage=draws_count ? calloc(draws_count,sizeof(*storage)) : NULL;
    doe_dx_render_draw *draws=draws_count ? calloc(draws_count,sizeof(*draws)) : NULL;
    native_failure failure=native_failure_status(draws_count && (!storage || !draws) ? NATIVE_OUT_OF_MEMORY : NATIVE_OK);
    for (size_t i=0;failure.status==NATIVE_OK && i<draws_count;++i) {
        const doe_resource_command *record=&records[i+1];
        if (record->kind==DOE_COMMAND_BEGIN_OCCLUSION || record->kind==DOE_COMMAND_END_OCCLUSION) {
            native_queries *queries=doe_resource_query_set_native(record->value.timestamp.set);
            if (!queries || !queries->d3d12_queries) { failure=native_failure_status(NATIVE_INVALID_ARGUMENT); break; }
            draws[i].query=queries->d3d12_queries; draws[i].query_index=record->value.timestamp.index;
            draws[i].query_begin=record->kind==DOE_COMMAND_BEGIN_OCCLUSION;
            continue;
        }
        const doe_resource_render_packet *packet=doe_resource_render_record_borrow(record->value.render);
        failure=prepare_draw(packet,&draws[i],&storage[i]);
        if (failure.status==NATIVE_OK) failure=native_d3d12_draw_prepare(commands,packet,&draws[i],&storage[i]);
    }
    doe_dx_render_pass pass={.colors_explicit=true,.colors=colors,.color_count=begin->targets.color_count,
        .draws=draws,.draw_count=draws_count};
    if (failure.status==NATIVE_OK && begin->targets.depth_stencil) {
        doe_resource_view *depth_view=begin->targets.depth_stencil;
        const doe_resource_view_desc *depth_span=doe_resource_view_description(depth_view);
        native_texture *depth=doe_resource_texture_native(doe_resource_view_texture(depth_view));
        if (!depth_span || !depth || !depth->d3d12 ||
            depth_span->dimension!=WGPUTextureViewDimension_2D || depth_span->aspect!=WGPUTextureAspect_All ||
            depth_span->base_mip || depth_span->mip_count!=1 || depth_span->base_layer || depth_span->layer_count!=1)
            failure=native_failure_status(NATIVE_UNSUPPORTED);
        else {
            pass.depth=depth->d3d12;
            pass.clear_depth=begin->targets.depth_load==WGPULoadOp_Clear;
            pass.depth_clear=begin->targets.clear_depth;
            pass.discard_depth=begin->targets.depth_store==WGPUStoreOp_Discard;
            pass.depth_read_only=begin->targets.depth_read_only;
            pass.stencil_read_only=begin->targets.stencil_read_only;
            pass.clear_stencil=begin->targets.stencil_load==WGPULoadOp_Clear;
            pass.discard_stencil=begin->targets.stencil_store==WGPUStoreOp_Discard;
            pass.stencil_clear=begin->targets.clear_stencil;
            /* Canonical pass admission already resolves absent-aspect load/store
             * and read-only contracts; backend independently rejects writes. */
        }
    }
    if (failure.status==NATIVE_OK) {
        doe_dx_error detail={0};
        doe_dx_status status=doe_dx_commands_render(commands->value.d3d12.commands,&pass,&detail);
        failure=native_failure_d3d12(status,&detail);
    }
    if (storage) for (size_t i=0;i<draws_count;++i) doe_dx_buffer_release(storage[i].arguments.buffer);
    free(draws);
    free(storage);
    if (failure.status==NATIVE_OK) *consumed=end+1;
    return failure;
#else
    (void)commands; (void)records; (void)count; (void)consumed;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}
