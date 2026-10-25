/* SPDX-License-Identifier: MIT */
#include "metal_graphics_internal.h"
#include <stdlib.h>
#include <string.h>

typedef struct draw_storage {
    doe_mtl_render_draw draw;
    native_metal_snapshot stages[2];
    doe_mtl_guarded_draw guarded;
} draw_storage;

static native_failure prepare_draw(doe_mtl_commands *commands, const doe_resource_render_packet *packet, draw_storage *storage)
{
    native_pipeline *pipeline = doe_resource_render_pipeline_native(packet->pipeline);
    if (!pipeline || !pipeline->metal_graphics) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    const native_metal_graphics *graphics = pipeline->metal_graphics;
    doe_resource_binding resolved[NATIVE_MAX_PIPELINE_BINDINGS];
    if (native_resolve_resources(pipeline, doe_resource_render_pipeline_get_layout(packet->pipeline),
        packet->groups, &packet->offsets, resolved) != DOE_RESOURCE_OK)
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    doe_mtl_render_draw *draw = &storage->draw;
    *draw = (doe_mtl_render_draw){.pipeline=pipeline->handle.metal_render,
        .topology=graphics->topology, .count=packet->count, .instance_count=packet->instances,
        .first_vertex=packet->indexed ? 0 : packet->first, .first_instance=packet->first_instance,
        .winding=graphics->winding, .cull=graphics->cull, .viewport_enabled=true, .scissor_enabled=true,
        .viewport={packet->state.viewport[0],packet->state.viewport[1],packet->state.viewport[2],
            packet->state.viewport[3],packet->state.viewport[4],packet->state.viewport[5]},
        .scissor={packet->state.scissor[0],packet->state.scissor[1],packet->state.scissor[2],packet->state.scissor[3]},
        .depth_bias=graphics->depth_bias, .depth_slope=graphics->depth_slope,
        .depth_clamp=graphics->depth_clamp, .unclipped_depth=graphics->unclipped_depth,
        .stencil_reference=packet->state.stencil_reference,
        .visibility_mode=packet->occlusion_active ? 1u : 0u,
        .visibility_offset=packet->occlusion_active ? (uint64_t)packet->occlusion_index * 8 : 0};
    if (packet->indexed || packet->indirect) {
        native_device *device=pipeline->device;
        if (!device->metal_draw_guard) return native_failure_status(NATIVE_INVALID_STATE);
        native_buffer *arguments=doe_resource_buffer_native(packet->indirect);
        storage->guarded=(doe_mtl_guarded_draw){.guard=device->metal_draw_guard,
            .arguments=arguments ? arguments->handle.metal : NULL,.argument_offset=packet->indirect_offset,
            .first_index=packet->first,.first_instance_allowed=!packet->indirect ||
                native_feature_enabled(device,WGPUFeatureName_IndirectFirstInstance)};
        if (packet->indexed) {
            native_buffer *index=doe_resource_buffer_native(packet->index.buffer);
            if (!index) return native_failure_status(NATIVE_INVALID_ARGUMENT);
            switch (packet->index_format) {
            case WGPUIndexFormat_Uint16: draw->index_type=0; break;
            case WGPUIndexFormat_Uint32: draw->index_type=1; break;
            case WGPUIndexFormat_Undefined:
            case WGPUIndexFormat_Force32: return native_failure_status(NATIVE_INVALID_ARGUMENT);
            }
            draw->index_buffer=index->handle.metal;
            draw->index_offset=packet->index.offset;
            draw->base_vertex=packet->base_vertex;
            storage->guarded.index_size=packet->index.size;
        }
        draw->guarded=&storage->guarded;
    }
    doe_vk_clear_color blend;
    if (!native_render_clear(WGPUTextureFormat_RGBA32Float, packet->state.blend, &blend))
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    memcpy(draw->blend_constant, blend.float32, sizeof(draw->blend_constant));
    for (size_t stage = 0; stage < 2; ++stage) {
        native_failure failure = native_metal_binding_snapshot(commands, pipeline, &graphics->stages[stage],
            resolved, &storage->stages[stage]);
        if (failure.status != NATIVE_OK) return failure;
    }
    draw->vertex = storage->stages[0].bindings;
    draw->fragment = storage->stages[1].bindings;
    for (size_t i = 0; i < pipeline->vertex_count; ++i) {
        uint32_t slot = graphics->vertex_slots[i];
        if (slot == UINT32_MAX) continue;
        if (draw->vertex.buffer_count == DOE_MTL_BUFFER_SLOTS) return native_failure_status(NATIVE_UNSUPPORTED);
        native_buffer *buffer = doe_resource_buffer_native(packet->vertices[i].buffer);
        if (!buffer) return native_failure_status(NATIVE_INVALID_ARGUMENT);
        storage->stages[0].buffers[draw->vertex.buffer_count++] = (doe_mtl_buffer_binding){slot,
            buffer->handle.metal, packet->vertices[i].offset, packet->vertices[i].size};
    }
    return native_failure_status(NATIVE_OK);
}

static bool clear_color(const doe_resource_color_attachment *source, doe_mtl_clear_color *out)
{
    const doe_resource_view_desc *view = doe_resource_view_description(source->view);
    doe_vk_clear_color converted;
    uint32_t scalar;
    if (!view || !native_color_scalar(view->format, &scalar) ||
        !native_render_clear(view->format, source->clear, &converted)) return false;
    double values[4];
    for (size_t i = 0; i < 4; ++i) values[i] = scalar == DOE_COMPILER_LEAF_F32 ? converted.float32[i] :
        scalar == DOE_COMPILER_LEAF_U32 ? (double)converted.uint32[i] : (double)converted.int32[i];
    *out = (doe_mtl_clear_color){values[0],values[1],values[2],values[3]};
    return true;
}

static bool same_targets(const doe_resource_render_targets *a, const doe_resource_render_targets *b)
{
    if (a->color_count != b->color_count || a->depth_stencil != b->depth_stencil ||
        a->occlusion_queries != b->occlusion_queries) return false;
    for (uint32_t i = 0; i < a->color_count; ++i)
        if (a->colors[i].view != b->colors[i].view || a->colors[i].resolve_target != b->colors[i].resolve_target) return false;
    return true;
}

native_failure native_metal_render(doe_mtl_commands *commands,
    const doe_resource_command *records, size_t count, size_t *consumed)
{
    if (!count || records[0].kind != DOE_COMMAND_RENDER) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    const doe_resource_render_packet *begin = doe_resource_render_record_borrow(records[0].value.render);
    if (!begin || !begin->boundary_only || !begin->first_in_pass || begin->last_in_pass)
        return native_failure_status(NATIVE_UNSUPPORTED);
    size_t end = 0, draws_count = 0;
    for (size_t i = 1; i < count; ++i) {
        if (records[i].kind == DOE_COMMAND_BEGIN_OCCLUSION || records[i].kind == DOE_COMMAND_END_OCCLUSION) {
            if (records[i].value.timestamp.set != begin->targets.occlusion_queries)
                return native_failure_status(NATIVE_INVALID_ARGUMENT);
            continue;
        }
        if (records[i].kind != DOE_COMMAND_RENDER) return native_failure_status(NATIVE_UNSUPPORTED);
        const doe_resource_render_packet *packet = doe_resource_render_record_borrow(records[i].value.render);
        if (!packet || packet->first_in_pass || !same_targets(&packet->targets, &begin->targets))
            return native_failure_status(NATIVE_INVALID_ARGUMENT);
        if (packet->boundary_only) {
            if (!packet->last_in_pass) return native_failure_status(NATIVE_INVALID_ARGUMENT);
            end = i;
            break;
        }
        if (packet->last_in_pass) return native_failure_status(NATIVE_INVALID_ARGUMENT);
        ++draws_count;
    }
    if (!end) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    if (draws_count > SIZE_MAX / sizeof(draw_storage) || draws_count > SIZE_MAX / sizeof(doe_mtl_render_draw))
        return native_failure_status(NATIVE_OUT_OF_MEMORY);
    draw_storage *storage = draws_count ? calloc(draws_count, sizeof(*storage)) : NULL;
    doe_mtl_render_draw *draws = draws_count ? calloc(draws_count, sizeof(*draws)) : NULL;
    native_failure failure = native_failure_status(draws_count && (!storage || !draws) ? NATIVE_OUT_OF_MEMORY : NATIVE_OK);
    size_t draw_index = 0;
    for (size_t i = 1; failure.status == NATIVE_OK && i < end; ++i) {
        if (records[i].kind != DOE_COMMAND_RENDER) continue;
        const doe_resource_render_packet *packet = doe_resource_render_record_borrow(records[i].value.render);
        failure = prepare_draw(commands, packet, &storage[draw_index]);
        draws[draw_index] = storage[draw_index].draw;
        ++draw_index;
    }
    doe_mtl_render_pass_desc pass = {.color_count=begin->targets.color_count};
    doe_mtl_error detail = {0};
    if (failure.status == NATIVE_OK && begin->targets.occlusion_queries) {
        native_queries *queries = doe_resource_query_set_native(begin->targets.occlusion_queries);
        if (!queries) failure = native_failure_status(NATIVE_INVALID_ARGUMENT);
        else pass.visibility_buffer = queries->metal_results;
        /* The resource owner admits each index once per pass. Reset only those
         * begun here: unused indices preserve earlier pass results, while an
         * empty begin/end writes zero without manufacturing a visibility hit. */
        for (size_t i = 1; failure.status == NATIVE_OK && i < end; ++i) {
            if (records[i].kind != DOE_COMMAND_BEGIN_OCCLUSION) continue;
            doe_mtl_status status = doe_mtl_commands_fill(commands, pass.visibility_buffer,
                (uint64_t)records[i].value.timestamp.index * 8, 8, 0, &detail);
            failure = native_failure_metal(status, &detail);
        }
    }
    for (uint32_t i = 0; failure.status == NATIVE_OK && i < pass.color_count; ++i) {
        const doe_resource_color_attachment *source = &begin->targets.colors[i];
        native_view *view = doe_resource_view_native(source->view);
        if (!view) continue;
        native_view *resolve = doe_resource_view_native(source->resolve_target);
        doe_mtl_render_attachment *target = &pass.colors[i];
        *target = (doe_mtl_render_attachment){.view=view->metal,
            .load=source->load == WGPULoadOp_Clear ? DOE_MTL_CLEAR : DOE_MTL_LOAD,
            .store=resolve ? (source->store == WGPUStoreOp_Store ? DOE_MTL_STORE_RESOLVE : DOE_MTL_RESOLVE) :
                source->store == WGPUStoreOp_Store ? DOE_MTL_STORE : DOE_MTL_STORE_DISCARD,
            .resolve_view=resolve ? resolve->metal : NULL};
        if (source->load == WGPULoadOp_Clear && !clear_color(source, &target->clear_color))
            failure = native_failure_status(NATIVE_INVALID_ARGUMENT);
        if (failure.status == NATIVE_OK) failure = native_failure_metal(
            doe_mtl_commands_initialize_attachment(commands, view->metal, &detail), &detail);
    }
    native_view *depth = doe_resource_view_native(begin->targets.depth_stencil);
    if (failure.status == NATIVE_OK && depth) {
        const doe_resource_render_targets *source = &begin->targets;
        const doe_resource_view_desc *view = doe_resource_view_description(source->depth_stencil);
        WGPUTextureFormat aspect;
        if (doe_resource_format_aspect_format(view->format, WGPUTextureAspect_DepthOnly, &aspect) == DOE_RESOURCE_OK)
            pass.depth = (doe_mtl_render_attachment){.view=depth->metal,
                .load=source->depth_load == WGPULoadOp_Clear ? DOE_MTL_CLEAR : DOE_MTL_LOAD,
                .store=source->depth_read_only || source->depth_store == WGPUStoreOp_Store ? DOE_MTL_STORE : DOE_MTL_STORE_DISCARD,
                .clear_depth=source->clear_depth};
        if (doe_resource_format_aspect_format(view->format, WGPUTextureAspect_StencilOnly, &aspect) == DOE_RESOURCE_OK)
            pass.stencil = (doe_mtl_render_attachment){.view=depth->metal,
                .load=source->stencil_load == WGPULoadOp_Clear ? DOE_MTL_CLEAR : DOE_MTL_LOAD,
                .store=source->stencil_read_only || source->stencil_store == WGPUStoreOp_Store ? DOE_MTL_STORE : DOE_MTL_STORE_DISCARD,
                .clear_stencil=source->clear_stencil};
        failure = native_failure_metal(doe_mtl_commands_initialize_attachment(commands, depth->metal, &detail), &detail);
    }
    if (failure.status == NATIVE_OK) {
        doe_mtl_status status = doe_mtl_commands_render(commands, &pass, draws, draws_count, &detail);
        failure = native_failure_metal(status, &detail);
    }
    free(draws);
    free(storage);
    if (failure.status == NATIVE_OK) *consumed = end + 1;
    return failure;
}
