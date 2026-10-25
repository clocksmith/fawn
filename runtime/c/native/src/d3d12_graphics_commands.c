/* SPDX-License-Identifier: MIT */
#include "d3d12_graphics_internal.h"
#include <stdlib.h>

#if defined(_WIN32)
static bool same_targets(const doe_resource_render_targets *a,const doe_resource_render_targets *b)
{
    return a->color_count==1 && b->color_count==1 && a->depth_stencil==b->depth_stencil &&
        !a->occlusion_queries && !b->occlusion_queries && a->colors[0].view==b->colors[0].view &&
        !a->colors[0].resolve_target && !b->colors[0].resolve_target &&
        !a->colors[0].has_depth_slice && !b->colors[0].has_depth_slice;
}
static native_failure prepare_draw(const doe_resource_render_packet *packet,
    doe_dx_render_draw *out,native_d3d12_graphics_snapshot *storage)
{
    if (packet->indexed || packet->indirect || packet->occlusion_active)
        return native_failure_status(NATIVE_UNSUPPORTED);
    native_pipeline *pipeline=doe_resource_render_pipeline_native(packet->pipeline);
    if (!pipeline || !pipeline->d3d12_graphics )
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    doe_resource_binding resolved[NATIVE_MAX_PIPELINE_BINDINGS];
    if (native_resolve_resources(pipeline,doe_resource_render_pipeline_get_layout(packet->pipeline),
        packet->groups,&packet->offsets,resolved)!=DOE_RESOURCE_OK) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    *out=(doe_dx_render_draw){.pipeline=pipeline->handle.d3d12,.vertex_count=packet->count,
        .instance_count=packet->instances,.first_vertex=packet->first,.first_instance=packet->first_instance};
    for (size_t stage=0;stage<DOE_DX_RENDER_STAGES;++stage) {
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
    const doe_resource_color_attachment *attachment=&begin->targets.colors[0];
    native_view *view=doe_resource_view_native(attachment->view);
    doe_resource_texture *texture=doe_resource_view_texture(attachment->view);
    const doe_resource_view_desc *span=doe_resource_view_description(attachment->view);
    const doe_resource_texture_desc *desc=doe_resource_texture_description(texture);
    native_texture *target=doe_resource_texture_native(texture);
    if (!view || !span || !desc || !target || !target->d3d12 || span->format!=desc->format ||
        span->dimension!=WGPUTextureViewDimension_2D || span->aspect!=WGPUTextureAspect_All ||
        span->base_mip || span->mip_count!=1 || span->base_layer || span->layer_count!=1)
        return native_failure_status(NATIVE_UNSUPPORTED);
    size_t draws_count=end-1;
    if (draws_count>SIZE_MAX/sizeof(native_d3d12_graphics_snapshot) || draws_count>SIZE_MAX/sizeof(doe_dx_render_draw))
        return native_failure_status(NATIVE_OUT_OF_MEMORY);
    native_d3d12_graphics_snapshot *storage=draws_count ? calloc(draws_count,sizeof(*storage)) : NULL;
    doe_dx_render_draw *draws=draws_count ? calloc(draws_count,sizeof(*draws)) : NULL;
    native_failure failure=native_failure_status(draws_count && (!storage || !draws) ? NATIVE_OUT_OF_MEMORY : NATIVE_OK);
    for (size_t i=0;failure.status==NATIVE_OK && i<draws_count;++i) {
        const doe_resource_render_packet *packet=doe_resource_render_record_borrow(records[i+1].value.render);
        failure=prepare_draw(packet,&draws[i],&storage[i]);
    }
    doe_dx_render_pass pass={.color=target->d3d12,.draws=draws,.draw_count=draws_count};
    if (failure.status==NATIVE_OK && begin->targets.depth_stencil) {
        doe_resource_view *depth_view=begin->targets.depth_stencil;
        const doe_resource_view_desc *depth_span=doe_resource_view_description(depth_view);
        native_texture *depth=doe_resource_texture_native(doe_resource_view_texture(depth_view));
        if (!depth_span || !depth || !depth->d3d12 || depth_span->format!=WGPUTextureFormat_Depth32Float ||
            depth_span->dimension!=WGPUTextureViewDimension_2D || depth_span->aspect!=WGPUTextureAspect_All ||
            depth_span->base_mip || depth_span->mip_count!=1 || depth_span->base_layer || depth_span->layer_count!=1)
            failure=native_failure_status(NATIVE_UNSUPPORTED);
        else {
            pass.depth=depth->d3d12;
            pass.clear_depth=begin->targets.depth_load==WGPULoadOp_Clear;
            pass.depth_clear=begin->targets.clear_depth;
            pass.discard_depth=begin->targets.depth_store==WGPUStoreOp_Discard;
            if (begin->targets.depth_read_only) {
                if (begin->targets.depth_load!=WGPULoadOp_Undefined || begin->targets.depth_store!=WGPUStoreOp_Undefined)
                    failure=native_failure_status(NATIVE_INVALID_ARGUMENT);
            } else if ((begin->targets.depth_load!=WGPULoadOp_Load && !pass.clear_depth) ||
                (begin->targets.depth_store!=WGPUStoreOp_Store && !pass.discard_depth))
                failure=native_failure_status(NATIVE_INVALID_ARGUMENT);
        }
    }
    if (failure.status==NATIVE_OK) {
        if (attachment->load==WGPULoadOp_Clear) {
            doe_vk_clear_color clear;
            if (!native_render_clear(span->format,attachment->clear,&clear)) failure=native_failure_status(NATIVE_INVALID_ARGUMENT);
            else { pass.clear=true; memcpy(pass.clear_color,clear.float32,sizeof(pass.clear_color)); }
        } else if (attachment->load!=WGPULoadOp_Load) failure=native_failure_status(NATIVE_INVALID_ARGUMENT);
        if (attachment->store==WGPUStoreOp_Discard) pass.discard=true;
        else if (attachment->store!=WGPUStoreOp_Store) failure=native_failure_status(NATIVE_INVALID_ARGUMENT);
    }
    if (failure.status==NATIVE_OK) {
        doe_dx_error detail={0};
        doe_dx_status status=doe_dx_commands_render(commands->value.d3d12.commands,&pass,&detail);
        failure=native_failure_d3d12(status,&detail);
    }
    free(draws);
    free(storage);
    if (failure.status==NATIVE_OK) *consumed=end+1;
    return failure;
#else
    (void)commands; (void)records; (void)count; (void)consumed;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}
