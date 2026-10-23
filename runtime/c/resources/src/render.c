/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <math.h>
#include <string.h>

static doe_resource_status record_draw(doe_resource_render_pass *pass, bool boundary_only,
    bool indexed, uint32_t count, uint32_t instances, uint32_t first,
    int32_t base_vertex, uint32_t first_instance, doe_resource_buffer *arguments, uint64_t indirect_offset);

static doe_resource_status validate_attachment(void *device, const doe_resource_view *view,
    bool depth, uint32_t extent[2])
{
    if (!view || !resource_texture_available(view->texture)) return DOE_RESOURCE_UNAVAILABLE;
    if (view->base.config.device.identity != device) return DOE_RESOURCE_DEVICE_MISMATCH;
    if (!(view->desc.usage & WGPUTextureUsage_RenderAttachment)) return DOE_RESOURCE_USAGE;
    if (view->desc.dimension != WGPUTextureViewDimension_2D) return DOE_RESOURCE_UNSUPPORTED;
    if (view->desc.mip_count != 1 || view->desc.layer_count != 1 ||
        resource_view_aspects(view) != resource_format_aspects(view->texture->desc.format) ||
        view->desc.base_mip >= 32)
        return DOE_RESOURCE_LAYOUT;
    if (resource_format_depth(view->desc.format) != depth) return DOE_RESOURCE_LAYOUT;
    extent[0] = view->texture->desc.width >> view->desc.base_mip;
    extent[1] = view->texture->desc.height >> view->desc.base_mip;
    if (!extent[0]) extent[0] = 1;
    if (!extent[1]) extent[1] = 1;
    return DOE_RESOURCE_OK;
}

static bool valid_operations(bool present, bool read_only, bool transient,
    WGPULoadOp load, WGPUStoreOp store)
{
    if (!present || read_only)
        return load == WGPULoadOp_Undefined && store == WGPUStoreOp_Undefined &&
            !(present && transient);
    if (transient) return load == WGPULoadOp_Clear && store == WGPUStoreOp_Discard;
    return (load == WGPULoadOp_Load || load == WGPULoadOp_Clear) &&
        (store == WGPUStoreOp_Store || store == WGPUStoreOp_Discard);
}

static doe_resource_status validate_targets(void *device,
    const doe_resource_render_targets *targets, uint32_t extent[2])
{
    doe_resource_status status = validate_attachment(device, targets->color, false, extent);
    if (status != DOE_RESOURCE_OK) return status;
    bool transient = (targets->color->desc.usage & WGPUTextureUsage_TransientAttachment) != 0;
    if (!valid_operations(true, false, transient, targets->color_load, targets->color_store))
        return DOE_RESOURCE_LAYOUT;
    if (targets->color_load == WGPULoadOp_Clear)
        for (size_t i = 0; i < 4; i++) if (!isfinite(targets->clear_color[i])) return DOE_RESOURCE_RANGE;
    if (targets->depth_stencil) {
        uint32_t depth_extent[2];
        status = validate_attachment(device, targets->depth_stencil, true, depth_extent);
        if (status != DOE_RESOURCE_OK) return status;
        if (extent[0] != depth_extent[0] || extent[1] != depth_extent[1] ||
            targets->color->texture->desc.samples != targets->depth_stencil->texture->desc.samples)
            return DOE_RESOURCE_LAYOUT;
        WGPUTextureFormat format = targets->depth_stencil->desc.format;
        bool depth = resource_format_aspect(format, WGPUTextureAspect_DepthOnly);
        bool stencil = resource_format_aspect(format, WGPUTextureAspect_StencilOnly);
        transient = (targets->depth_stencil->desc.usage & WGPUTextureUsage_TransientAttachment) != 0;
        if (!valid_operations(depth, targets->depth_read_only, transient,
            targets->depth_load, targets->depth_store) ||
            !valid_operations(stencil, targets->stencil_read_only, transient,
            targets->stencil_load, targets->stencil_store)) return DOE_RESOURCE_LAYOUT;
        if (depth && targets->depth_load == WGPULoadOp_Clear &&
            (!isfinite(targets->clear_depth) || targets->clear_depth < 0 || targets->clear_depth > 1))
            return DOE_RESOURCE_RANGE;
    }
    if (targets->resolve_target) {
        uint32_t resolve_extent[2];
        status = validate_attachment(device, targets->resolve_target, false, resolve_extent);
        if (status != DOE_RESOURCE_OK) return status;
        const doe_resource_view *color = targets->color;
        const doe_resource_view *resolve = targets->resolve_target;
        if (color->texture->desc.samples <= 1 || resolve->texture->desc.samples != 1 ||
            color->desc.format != resolve->desc.format ||
            color->texture->desc.format != resolve->texture->desc.format ||
            extent[0] != resolve_extent[0] || extent[1] != resolve_extent[1]) return DOE_RESOURCE_LAYOUT;
        if (resolve->desc.usage & WGPUTextureUsage_TransientAttachment) return DOE_RESOURCE_USAGE;
    }
    if (targets->occlusion_queries) {
        if (!resource_query_available(targets->occlusion_queries)) return DOE_RESOURCE_UNAVAILABLE;
        if (targets->occlusion_queries->base.config.device.identity != device) return DOE_RESOURCE_DEVICE_MISMATCH;
        if (targets->occlusion_queries->type != WGPUQueryType_Occlusion) return DOE_RESOURCE_LAYOUT;
    }
    return DOE_RESOURCE_OK;
}

void resource_render_packet_retain(const doe_resource_render_packet *packet)
{
    doe_resource_view_retain(packet->targets.color);
    doe_resource_view_retain(packet->targets.depth_stencil);
    doe_resource_view_retain(packet->targets.resolve_target);
    doe_resource_query_set_retain(packet->targets.occlusion_queries);
    doe_resource_render_pipeline_retain(packet->pipeline);
    for (size_t i = 0; i < DOE_RESOURCE_MAX_GROUPS; i++) doe_resource_bind_group_retain(packet->groups[i]);
    for (size_t i = 0; i < DOE_RESOURCE_MAX_VERTEX_BUFFERS; i++) doe_resource_buffer_retain(packet->vertices[i].buffer);
    doe_resource_buffer_retain(packet->index.buffer);
    doe_resource_buffer_retain(packet->indirect);
}

void resource_render_packet_release(const doe_resource_render_packet *packet)
{
    doe_resource_view_release(packet->targets.color);
    doe_resource_view_release(packet->targets.depth_stencil);
    doe_resource_view_release(packet->targets.resolve_target);
    doe_resource_query_set_release(packet->targets.occlusion_queries);
    doe_resource_render_pipeline_release(packet->pipeline);
    for (size_t i = 0; i < DOE_RESOURCE_MAX_GROUPS; i++) doe_resource_bind_group_release(packet->groups[i]);
    for (size_t i = 0; i < DOE_RESOURCE_MAX_VERTEX_BUFFERS; i++) doe_resource_buffer_release(packet->vertices[i].buffer);
    doe_resource_buffer_release(packet->index.buffer);
    doe_resource_buffer_release(packet->indirect);
}

doe_resource_status doe_resource_render_pipeline_create(const doe_resource_config *config,
    doe_resource_pipeline_layout *layout, const doe_resource_render_pipeline_desc *desc,
    doe_resource_native native, doe_resource_render_pipeline **out)
{
    if (!resource_config_valid(config) || !layout || !desc || !out || !native.handle || !native.release)
        return DOE_RESOURCE_INVALID_ARGUMENT;
    if (!config->device.alive(config->device.identity)) return DOE_RESOURCE_UNAVAILABLE;
    if (layout->base.config.device.identity != config->device.identity) return DOE_RESOURCE_DEVICE_MISMATCH;
    uint32_t bytes, width, height;
    doe_resource_status status = resource_format_block(desc->color_format, &bytes, &width, &height);
    if (status != DOE_RESOURCE_OK) return status;
    if (resource_format_depth(desc->color_format) || width != 1 || height != 1 ||
        (desc->depth_stencil_format && !resource_format_depth(desc->depth_stencil_format))) return DOE_RESOURCE_LAYOUT;
    if (desc->samples != 1 && desc->samples != 4) return DOE_RESOURCE_UNSUPPORTED;
    if (desc->vertex_count > DOE_RESOURCE_MAX_VERTEX_BUFFERS) return DOE_RESOURCE_UNSUPPORTED;
    doe_resource_render_pipeline_desc normalized = *desc;
    if (!normalized.topology) normalized.topology = WGPUPrimitiveTopology_TriangleList;
    if (normalized.topology < WGPUPrimitiveTopology_PointList ||
        normalized.topology > WGPUPrimitiveTopology_TriangleStrip) return DOE_RESOURCE_LAYOUT;
    bool strip = normalized.topology == WGPUPrimitiveTopology_LineStrip ||
        normalized.topology == WGPUPrimitiveTopology_TriangleStrip;
    if (normalized.strip_index_format != WGPUIndexFormat_Undefined &&
        (!strip || (normalized.strip_index_format != WGPUIndexFormat_Uint16 &&
        normalized.strip_index_format != WGPUIndexFormat_Uint32))) return DOE_RESOURCE_LAYOUT;
    for (size_t i = 0; i < normalized.vertex_count; i++) {
        doe_resource_vertex_layout *vertex = &normalized.vertices[i];
        if (!vertex->step_mode) {
            if (!vertex->last_attribute_end) {
                *vertex = (doe_resource_vertex_layout){0};
                continue;
            }
            vertex->step_mode = WGPUVertexStepMode_Vertex;
        }
        if (vertex->stride % 4 || (vertex->step_mode != WGPUVertexStepMode_Vertex &&
            vertex->step_mode != WGPUVertexStepMode_Instance)) return DOE_RESOURCE_LAYOUT;
        if (vertex->stride && vertex->last_attribute_end > vertex->stride) return DOE_RESOURCE_RANGE;
    }
    for (size_t i = 0; i < layout->count; i++)
        if (doe_resource_pipeline_layout_group_required(layout, i) && layout->groups[i]->automatic && layout->groups[i]->automatic_claimed) return DOE_RESOURCE_LAYOUT;
    doe_resource_render_pipeline *pipeline = resource_allocate(config, sizeof(*pipeline), _Alignof(doe_resource_render_pipeline));
    if (!pipeline) return DOE_RESOURCE_OUT_OF_MEMORY;
    pipeline->layout = layout;
    pipeline->desc = normalized;
    for (size_t i = 0; i < layout->count; i++)
        if (doe_resource_pipeline_layout_group_required(layout, i) && layout->groups[i]->automatic) layout->groups[i]->automatic_claimed = true;
    doe_resource_pipeline_layout_retain(layout);
    resource_init(&pipeline->base, config, native);
    *out = pipeline;
    return DOE_RESOURCE_OK;
}

void doe_resource_render_pipeline_retain(doe_resource_render_pipeline *pipeline)
{
    if (pipeline) resource_retain(&pipeline->base);
}

void doe_resource_render_pipeline_release(doe_resource_render_pipeline *pipeline)
{
    if (!pipeline || !resource_last_reference(&pipeline->base)) return;
    doe_resource_pipeline_layout *layout = pipeline->layout;
    resource_finalize(&pipeline->base, sizeof(*pipeline), _Alignof(doe_resource_render_pipeline));
    doe_resource_pipeline_layout_release(layout);
}

void *doe_resource_render_pipeline_native(const doe_resource_render_pipeline *pipeline)
{
    return pipeline ? pipeline->base.native.handle : NULL;
}

doe_resource_status doe_resource_encoder_begin_render(doe_resource_encoder *encoder,
    const doe_resource_render_targets *targets, doe_resource_render_pass **out)
{
    return doe_resource_encoder_begin_render_with_timestamps(encoder, targets, NULL, out);
}

doe_resource_status doe_resource_encoder_begin_render_with_timestamps(doe_resource_encoder *encoder,
    const doe_resource_render_targets *targets, const doe_resource_pass_timestamps *timestamps,
    doe_resource_render_pass **out)
{
    if (!out) return DOE_RESOURCE_INVALID_ARGUMENT;
    doe_resource_status status = resource_encoder_open(encoder);
    if (status != DOE_RESOURCE_OK) return status;
    status = resource_pass_timestamps_validate(encoder->base.config.device.identity, timestamps);
    if (status != DOE_RESOURCE_OK) return resource_encoder_fail(encoder, status);
    if (!targets) return resource_encoder_fail(encoder, DOE_RESOURCE_INVALID_ARGUMENT);
    uint32_t extent[2];
    status = validate_targets(encoder->base.config.device.identity, targets, extent);
    if (status != DOE_RESOURCE_OK) return resource_encoder_fail(encoder, status);
    doe_resource_render_pass *pass = resource_allocate(&encoder->base.config,
        sizeof(*pass), _Alignof(doe_resource_render_pass));
    if (!pass) return resource_encoder_fail(encoder, DOE_RESOURCE_OUT_OF_MEMORY);
    memset(pass, 0, sizeof(*pass));
    pass->encoder = encoder;
    pass->state.targets = *targets;
    if (!pass->state.targets.max_draw_count) pass->state.targets.max_draw_count = UINT64_C(50000000);
    pass->state.state.viewport[2] = (float)extent[0];
    pass->state.state.viewport[3] = (float)extent[1];
    pass->state.state.viewport[5] = 1;
    pass->state.state.scissor[2] = extent[0];
    pass->state.state.scissor[3] = extent[1];
    resource_render_packet_retain(&pass->state);
    doe_resource_encoder_retain(encoder);
    resource_init(&pass->base, &encoder->base.config, (doe_resource_native){0});
    encoder->active_pass = pass;
    status = resource_scope_targets(&pass->base.config, &pass->scope, targets);
    if (status != DOE_RESOURCE_OK) {
        resource_encoder_fail(encoder, status);
        doe_resource_render_pass_release(pass);
        return status;
    }
    if (timestamps) {
        pass->timestamps = *timestamps;
        doe_resource_query_set_retain(timestamps->set);
        if (timestamps->begin_index != UINT32_MAX) {
            status = resource_record_timestamp(encoder, timestamps->set, timestamps->begin_index, DOE_TIMESTAMP_PASS_BEGIN);
            if (status != DOE_RESOURCE_OK) {
                doe_resource_render_pass_release(pass);
                return status;
            }
        }
    }
    /* Emit the attachment/pass boundary before query or draw events, including
     * empty scopes. Later draw snapshots cannot reconstruct a missing boundary. */
    status = record_draw(pass, true, false, 0, 0, 0, 0, 0, NULL, 0);
    if (status != DOE_RESOURCE_OK) {
        doe_resource_render_pass_release(pass);
        return status;
    }
    *out = pass;
    return DOE_RESOURCE_OK;
}

doe_resource_status resource_render_pass_active(doe_resource_render_pass *pass)
{
    if (!pass) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (pass->encoder->status != DOE_RESOURCE_OK) return pass->encoder->status;
    if (pass->ended || pass->encoder->active_pass != pass) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_STATE);
    if (!resource_alive(&pass->base)) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_UNAVAILABLE);
    return DOE_RESOURCE_OK;
}

void doe_resource_render_pass_retain(doe_resource_render_pass *pass)
{
    if (pass) resource_retain(&pass->base);
}

void doe_resource_render_pass_release(doe_resource_render_pass *pass)
{
    if (!pass || !resource_last_reference(&pass->base)) return;
    doe_resource_encoder *encoder = pass->encoder;
    if (!pass->ended) {
        resource_encoder_fail(encoder, DOE_RESOURCE_STATE);
        if (encoder->active_pass == pass) encoder->active_pass = NULL;
    }
    doe_resource_query_set_release(pass->timestamps.set);
    resource_render_packet_release(&pass->state);
    resource_scope_release(&pass->base.config, &pass->scope);
    resource_finalize(&pass->base, sizeof(*pass), _Alignof(doe_resource_render_pass));
    doe_resource_encoder_release(encoder);
}

doe_resource_status doe_resource_render_pass_set_pipeline(doe_resource_render_pass *pass,
    doe_resource_render_pipeline *pipeline)
{
    doe_resource_status status = resource_render_pass_active(pass);
    if (status != DOE_RESOURCE_OK) return status;
    status = resource_render_set_pipeline(pass->base.config.device.identity, &pass->state, pipeline);
    return resource_encoder_fail(pass->encoder, status);
}

doe_resource_status doe_resource_render_pass_set_group(doe_resource_render_pass *pass,
    uint32_t index, const doe_resource_dispatch_group *group)
{
    doe_resource_status status = resource_render_pass_active(pass);
    if (status != DOE_RESOURCE_OK) return status;
    status = resource_render_set_group(&pass->base.config, &pass->scope, &pass->state, index, group);
    return resource_encoder_fail(pass->encoder, status);
}

doe_resource_status doe_resource_render_pass_set_vertex(doe_resource_render_pass *pass,
    uint32_t slot, doe_resource_buffer *buffer, uint64_t offset, uint64_t size)
{
    doe_resource_status status = resource_render_pass_active(pass);
    if (status != DOE_RESOURCE_OK) return status;
    status = resource_render_set_vertex(&pass->base.config, &pass->scope, &pass->state, slot, buffer, offset, size);
    return resource_encoder_fail(pass->encoder, status);
}

doe_resource_status doe_resource_render_pass_set_index(doe_resource_render_pass *pass,
    doe_resource_buffer *buffer, WGPUIndexFormat format, uint64_t offset, uint64_t size)
{
    doe_resource_status status = resource_render_pass_active(pass);
    if (status != DOE_RESOURCE_OK) return status;
    status = resource_render_set_index(&pass->base.config, &pass->scope, &pass->state, buffer, format, offset, size);
    return resource_encoder_fail(pass->encoder, status);
}

const doe_resource_render_state *doe_resource_render_pass_state(const doe_resource_render_pass *pass)
{
    return pass ? &pass->state.state : NULL;
}

doe_resource_status doe_resource_render_pass_set_state(doe_resource_render_pass *pass,
    const doe_resource_render_state *state)
{
    doe_resource_status status = resource_render_pass_active(pass);
    if (status != DOE_RESOURCE_OK) return status;
    if (!state) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_INVALID_ARGUMENT);
    for (size_t i = 0; i < 6; i++) if (!isfinite(state->viewport[i])) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_RANGE);
    for (size_t i = 0; i < 4; i++) if (!isfinite(state->blend[i])) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_RANGE);
    for (size_t i = 0; i < 4; i++) if (state->viewport[i] < 0) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_RANGE);
    if (state->viewport[4] < 0 || state->viewport[5] > 1 || state->viewport[4] > state->viewport[5])
        return resource_encoder_fail(pass->encoder, DOE_RESOURCE_RANGE);
    uint32_t extent[2];
    status = validate_targets(pass->base.config.device.identity, &pass->state.targets, extent);
    if (status != DOE_RESOURCE_OK) return resource_encoder_fail(pass->encoder, status);
    if (state->scissor[0] > extent[0] || state->scissor[2] > extent[0] - state->scissor[0] ||
        state->scissor[1] > extent[1] || state->scissor[3] > extent[1] - state->scissor[1])
        return resource_encoder_fail(pass->encoder, DOE_RESOURCE_RANGE);
    pass->state.state = *state;
    return DOE_RESOURCE_OK;
}

static doe_resource_status validate_packet(const doe_resource_render_packet *packet, void *device)
{
    uint32_t extent[2];
    doe_resource_status status = validate_targets(device, &packet->targets, extent);
    if (status != DOE_RESOURCE_OK || packet->boundary_only) return status;
    doe_resource_render_bundle_desc signature = {
        packet->targets.color->desc.format,
        packet->targets.depth_stencil ? packet->targets.depth_stencil->desc.format : 0,
        packet->targets.color->texture->desc.samples,
        packet->targets.depth_read_only, packet->targets.stencil_read_only
    };
    return resource_render_draw_validate(packet, device, &signature);
}

static doe_resource_status append_packet(doe_resource_render_pass *pass,
    doe_resource_render_packet packet)
{
    doe_resource_status status = validate_packet(&packet, pass->base.config.device.identity);
    if (status != DOE_RESOURCE_OK) return resource_encoder_fail(pass->encoder, status);
    if (packet.indirect) {
        status = resource_scope_input(&pass->base.config, &pass->scope, packet.indirect);
        if (status != DOE_RESOURCE_OK) return resource_encoder_fail(pass->encoder, status);
    }
    doe_resource_render_record *record = resource_allocate(&pass->base.config,
        sizeof(*record), _Alignof(doe_resource_render_record));
    if (!record) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_OUT_OF_MEMORY);
    status = resource_encoder_reserve(pass->encoder);
    if (status != DOE_RESOURCE_OK) {
        resource_free(&pass->base.config, record, sizeof(*record), _Alignof(doe_resource_render_record));
        return status;
    }
    record->scope = (resource_usage_scope){0};
    record->packet = packet;
    resource_render_packet_retain(&record->packet);
    resource_init(&record->base, &pass->base.config, (doe_resource_native){0});
    doe_resource_command command = {.kind = DOE_COMMAND_RENDER};
    command.value.render = record;
    pass->encoder->commands[pass->encoder->count++] = command;
    pass->last_record = record;
    if (!packet.boundary_only) pass->draw_count++;
    return DOE_RESOURCE_OK;
}
static doe_resource_status record_draw(doe_resource_render_pass *pass, bool boundary_only,
    bool indexed, uint32_t count, uint32_t instances, uint32_t first,
    int32_t base_vertex, uint32_t first_instance, doe_resource_buffer *arguments, uint64_t indirect_offset)
{
    doe_resource_status status = resource_render_pass_active(pass);
    if (status != DOE_RESOURCE_OK) return status;
    if (!boundary_only && pass->draw_count >= pass->state.targets.max_draw_count)
        return resource_encoder_fail(pass->encoder, DOE_RESOURCE_RANGE);
    doe_resource_render_packet packet = pass->state;
    packet.boundary_only = boundary_only;
    packet.first_in_pass = pass->last_record == NULL;
    packet.last_in_pass = false;
    packet.indexed = indexed;
    packet.count = count;
    packet.instances = instances;
    packet.first = first;
    packet.base_vertex = base_vertex;
    packet.first_instance = first_instance;
    packet.indirect = arguments;
    packet.indirect_offset = indirect_offset;
    return append_packet(pass, packet);
}

doe_resource_status resource_render_replay_packet(doe_resource_render_pass *pass,
    const doe_resource_render_packet *draw)
{
    doe_resource_status status = resource_render_pass_active(pass);
    if (status != DOE_RESOURCE_OK) return status;
    if (pass->draw_count >= pass->state.targets.max_draw_count)
        return resource_encoder_fail(pass->encoder, DOE_RESOURCE_RANGE);
    doe_resource_render_packet packet = *draw;
    packet.targets = pass->state.targets;
    packet.state = pass->state.state;
    packet.occlusion_active = pass->state.occlusion_active;
    packet.occlusion_index = pass->state.occlusion_index;
    return append_packet(pass, packet);
}

doe_resource_status doe_resource_render_pass_draw(doe_resource_render_pass *pass,
    uint32_t vertex_count, uint32_t instances, uint32_t first_vertex, uint32_t first_instance)
{
    return record_draw(pass, false, false, vertex_count, instances, first_vertex, 0, first_instance, NULL, 0);
}

doe_resource_status doe_resource_render_pass_draw_indexed(doe_resource_render_pass *pass,
    uint32_t index_count, uint32_t instances, uint32_t first_index,
    int32_t base_vertex, uint32_t first_instance)
{
    return record_draw(pass, false, true, index_count, instances, first_index, base_vertex, first_instance, NULL, 0);
}

doe_resource_status doe_resource_render_pass_draw_indirect(doe_resource_render_pass *pass,
    doe_resource_buffer *arguments, uint64_t offset, bool indexed)
{
    doe_resource_status status = resource_render_pass_active(pass);
    if (status != DOE_RESOURCE_OK) return status;
    if (!arguments) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_INVALID_ARGUMENT);
    return record_draw(pass, false, indexed, 0, 0, 0, 0, 0, arguments, offset);
}

doe_resource_status doe_resource_render_pass_begin_occlusion(doe_resource_render_pass *pass, uint32_t index)
{
    doe_resource_status status = resource_render_pass_active(pass);
    if (status != DOE_RESOURCE_OK) return status;
    doe_resource_query_set *set = pass->state.targets.occlusion_queries;
    if (!resource_query_available(set)) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_UNAVAILABLE);
    if (pass->state.occlusion_active) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_STATE);
    if (index >= set->count) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_RANGE);
    uint64_t bit = UINT64_C(1) << (index % 64);
    if (pass->occlusion_written[index / 64] & bit)
        return resource_encoder_fail(pass->encoder, DOE_RESOURCE_STATE);
    status = resource_encoder_reserve(pass->encoder);
    if (status != DOE_RESOURCE_OK) return status;
    doe_resource_command command = {.kind = DOE_COMMAND_BEGIN_OCCLUSION};
    command.value.timestamp.set = set;
    command.value.timestamp.index = index;
    doe_resource_query_set_retain(set);
    pass->encoder->commands[pass->encoder->count++] = command;
    pass->occlusion_written[index / 64] |= bit;
    pass->state.occlusion_active = true;
    pass->state.occlusion_index = index;
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_render_pass_end_occlusion(doe_resource_render_pass *pass)
{
    doe_resource_status status = resource_render_pass_active(pass);
    if (status != DOE_RESOURCE_OK) return status;
    if (!pass->state.occlusion_active) return resource_encoder_fail(pass->encoder, DOE_RESOURCE_STATE);
    status = resource_encoder_reserve(pass->encoder);
    if (status != DOE_RESOURCE_OK) return status;
    doe_resource_command command = {.kind = DOE_COMMAND_END_OCCLUSION};
    command.value.timestamp.set = pass->state.targets.occlusion_queries;
    command.value.timestamp.index = pass->state.occlusion_index;
    doe_resource_query_set_retain(command.value.timestamp.set);
    pass->encoder->commands[pass->encoder->count++] = command;
    pass->state.occlusion_active = false;
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_render_pass_end(doe_resource_render_pass *pass)
{
    doe_resource_status status = resource_render_pass_active(pass);
    if (status != DOE_RESOURCE_OK) return status;
    if (pass->state.occlusion_active || pass->debug_depth)
        return resource_encoder_fail(pass->encoder, DOE_RESOURCE_STATE);
    /* End follows all query events, including a zero-draw query scope. Closing
     * at the last draw would place an EndQuery event outside its render pass. */
    status = record_draw(pass, true, false, 0, 0, 0, 0, 0, NULL, 0);
    if (status != DOE_RESOURCE_OK) return status;
    pass->last_record->packet.last_in_pass = true;
    if (pass->timestamps.set && pass->timestamps.end_index != UINT32_MAX) {
        status = resource_record_timestamp(pass->encoder, pass->timestamps.set,
            pass->timestamps.end_index, DOE_TIMESTAMP_PASS_END);
        if (status != DOE_RESOURCE_OK) return status;
    }
    doe_resource_query_set_release(pass->timestamps.set);
    pass->timestamps.set = NULL;
    pass->last_record->scope = pass->scope;
    pass->scope = (resource_usage_scope){0};
    pass->ended = true;
    pass->encoder->active_pass = NULL;
    return DOE_RESOURCE_OK;
}

void resource_render_record_release(doe_resource_render_record *record)
{
    if (!record || !resource_last_reference(&record->base)) return;
    resource_render_packet_release(&record->packet);
    resource_scope_release(&record->base.config, &record->scope);
    resource_finalize(&record->base, sizeof(*record), _Alignof(doe_resource_render_record));
}

doe_resource_status resource_render_record_validate(const doe_resource_render_record *record)
{
    if (!record) return DOE_RESOURCE_INVALID_ARGUMENT;
    doe_resource_status status = resource_scope_validate(&record->scope);
    return status == DOE_RESOURCE_OK ? validate_packet(&record->packet, record->base.config.device.identity) : status;
}

const doe_resource_render_packet *doe_resource_render_record_borrow(const doe_resource_render_record *record)
{
    return record ? &record->packet : NULL;
}

doe_resource_status doe_resource_render_pass_invalidate(doe_resource_render_pass *pass,
    doe_resource_status rejection)
{
    if (!resource_status_is_rejection(rejection)) return DOE_RESOURCE_INVALID_ARGUMENT;
    doe_resource_status status = resource_render_pass_active(pass);
    if (status != DOE_RESOURCE_OK) return status;
    return doe_resource_encoder_invalidate(pass->encoder, rejection);
}

doe_resource_status doe_resource_render_pass_debug(doe_resource_render_pass *pass,
    doe_resource_debug_operation operation)
{
    doe_resource_status status = resource_render_pass_active(pass);
    if (status != DOE_RESOURCE_OK) return status;
    return resource_encoder_fail(pass->encoder, resource_debug_apply(&pass->debug_depth, operation));
}
