/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <string.h>

/* Shared pass-layout admission; backend renderability/features remain explicit. */
doe_resource_status resource_render_layout_validate(WGPUTextureFormat color,
    WGPUTextureFormat depth_stencil, uint32_t samples)
{
    if (!color && !depth_stencil) return DOE_RESOURCE_LAYOUT;
    if (color) {
        uint32_t bytes, width, height;
        doe_resource_status status = resource_format_block(color, &bytes, &width, &height);
        if (status != DOE_RESOURCE_OK) return status;
        if (resource_format_depth(color) || width != 1 || height != 1) return DOE_RESOURCE_LAYOUT;
    }
    if (depth_stencil && !resource_format_depth(depth_stencil)) return DOE_RESOURCE_LAYOUT;
    return samples == 1 || samples == 4 ? DOE_RESOURCE_OK : DOE_RESOURCE_UNSUPPORTED;
}

/* Targets have already passed attachment admission, so at least one exists. */
doe_resource_render_bundle_desc resource_render_targets_layout(const doe_resource_render_targets *targets)
{
    const doe_resource_view *primary = targets->color ? targets->color : targets->depth_stencil;
    return (doe_resource_render_bundle_desc){
        targets->color ? targets->color->desc.format : WGPUTextureFormat_Undefined,
        targets->depth_stencil ? targets->depth_stencil->desc.format : WGPUTextureFormat_Undefined,
        primary->texture->desc.samples, targets->depth_read_only, targets->stencil_read_only
    };
}

doe_resource_status resource_render_set_pipeline(void *device, doe_resource_render_packet *state,
    doe_resource_render_pipeline *pipeline)
{
    if (!pipeline) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (pipeline->base.config.device.identity != device)
        return DOE_RESOURCE_DEVICE_MISMATCH;
    doe_resource_render_pipeline_retain(pipeline);
    doe_resource_render_pipeline_release(state->pipeline);
    state->pipeline = pipeline;
    return DOE_RESOURCE_OK;
}

doe_resource_status resource_render_set_group(const doe_resource_config *config,
    resource_usage_scope *scope, doe_resource_render_packet *state,
    uint32_t index, const doe_resource_dispatch_group *group)
{
    if (index >= DOE_RESOURCE_MAX_GROUPS) return DOE_RESOURCE_RANGE;
    if (!group || (!group->group && !group->dynamic_offset_count)) {
        doe_resource_bind_group_release(state->groups[index]);
        state->groups[index] = NULL;
        state->offsets.counts[index] = 0;
        return DOE_RESOURCE_OK;
    }
    if (!group->group) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (group->group->base.config.device.identity != config->device.identity)
        return DOE_RESOURCE_DEVICE_MISMATCH;
    doe_resource_status status = resource_scope_group(config, scope, group);
    if (status != DOE_RESOURCE_OK) return status;
    doe_resource_bind_group_retain(group->group);
    doe_resource_bind_group_release(state->groups[index]);
    state->groups[index] = group->group;
    state->offsets.counts[index] = group->dynamic_offset_count;
    if (group->dynamic_offset_count) memcpy(state->offsets.values[index], group->dynamic_offsets,
        group->dynamic_offset_count * sizeof(uint32_t));
    return DOE_RESOURCE_OK;
}

static doe_resource_status set_buffer(const doe_resource_config *config, resource_usage_scope *scope,
    doe_resource_vertex_binding *binding, doe_resource_buffer *buffer,
    uint64_t offset, uint64_t size, uint64_t usage, uint64_t alignment)
{
    if (!resource_buffer_available(buffer)) return DOE_RESOURCE_UNAVAILABLE;
    if (buffer->base.config.device.identity != config->device.identity)
        return DOE_RESOURCE_DEVICE_MISMATCH;
    if (!(buffer->desc.usage & usage)) return DOE_RESOURCE_USAGE;
    if (offset % alignment) return DOE_RESOURCE_ALIGNMENT;
    uint64_t resolved;
    doe_resource_status status = resource_range(buffer->desc.size, offset, size, &resolved);
    if (status != DOE_RESOURCE_OK) return status;
    status = resource_scope_input(config, scope, buffer);
    if (status != DOE_RESOURCE_OK) return status;
    doe_resource_buffer_retain(buffer);
    doe_resource_buffer_release(binding->buffer);
    *binding = (doe_resource_vertex_binding){buffer, offset, resolved};
    return DOE_RESOURCE_OK;
}

doe_resource_status resource_render_set_vertex(const doe_resource_config *config,
    resource_usage_scope *scope, doe_resource_render_packet *state,
    uint32_t slot, doe_resource_buffer *buffer, uint64_t offset, uint64_t size)
{
    if (slot >= DOE_RESOURCE_MAX_VERTEX_BUFFERS) return DOE_RESOURCE_RANGE;
    if (!buffer) {
        if (offset % 4) return DOE_RESOURCE_ALIGNMENT;
        uint64_t resolved;
        doe_resource_status status = resource_range(0, offset, size, &resolved);
        if (status != DOE_RESOURCE_OK) return status;
        doe_resource_buffer_release(state->vertices[slot].buffer);
        state->vertices[slot] = (doe_resource_vertex_binding){0};
        return DOE_RESOURCE_OK;
    }
    return set_buffer(config, scope, &state->vertices[slot], buffer, offset, size, WGPUBufferUsage_Vertex, 4);
}

doe_resource_status resource_render_set_index(const doe_resource_config *config,
    resource_usage_scope *scope, doe_resource_render_packet *state,
    doe_resource_buffer *buffer, WGPUIndexFormat format, uint64_t offset, uint64_t size)
{
    if (format != WGPUIndexFormat_Uint16 && format != WGPUIndexFormat_Uint32)
        return DOE_RESOURCE_LAYOUT;
    doe_resource_status status = set_buffer(config, scope, &state->index, buffer, offset, size, WGPUBufferUsage_Index,
        format == WGPUIndexFormat_Uint16 ? 2 : 4);
    if (status == DOE_RESOURCE_OK) state->index_format = format;
    return status;
}

static doe_resource_status validate_vertex(const doe_resource_vertex_binding *binding,
    const doe_resource_vertex_layout *layout, uint32_t first, uint32_t count, bool check_range)
{
    if (!resource_buffer_available(binding->buffer)) return DOE_RESOURCE_UNAVAILABLE;
    if (binding->buffer->mapped || binding->buffer->map_pending) return DOE_RESOURCE_STATE;
    uint64_t strides = (uint64_t)first + count;
    if (!check_range || !strides) return DOE_RESOURCE_OK;
    uint64_t last = strides - 1;
    if (layout->stride && last > (UINT64_MAX - layout->last_attribute_end) / layout->stride)
        return DOE_RESOURCE_OVERFLOW;
    uint64_t required = last * layout->stride + layout->last_attribute_end;
    return required <= binding->size ? DOE_RESOURCE_OK : DOE_RESOURCE_RANGE;
}

doe_resource_status resource_render_draw_validate(const doe_resource_render_packet *packet,
    void *device, const doe_resource_render_bundle_desc *signature)
{
    doe_resource_status status;
    if (packet->indirect) {
        status = resource_indirect_validate(device, packet->indirect, packet->indirect_offset, packet->indexed ? 20 : 16);
        if (status != DOE_RESOURCE_OK) return status;
        if (packet->indirect->mapped || packet->indirect->map_pending) return DOE_RESOURCE_STATE;
    }
    const doe_resource_render_pipeline *pipeline = packet->pipeline;
    if (!pipeline || !resource_alive(&pipeline->base)) return DOE_RESOURCE_UNAVAILABLE;
    if (pipeline->base.config.device.identity != device) return DOE_RESOURCE_DEVICE_MISMATCH;
    if (pipeline->desc.color_format != signature->color_format ||
        pipeline->desc.depth_stencil_format != signature->depth_stencil_format ||
        pipeline->desc.samples != signature->samples ||
        (signature->depth_read_only && pipeline->desc.depth_write) ||
        (signature->stencil_read_only && pipeline->desc.stencil_write)) return DOE_RESOURCE_LAYOUT;
    doe_resource_dispatch_group groups[DOE_RESOURCE_MAX_GROUPS];
    for (size_t i = 0; i < pipeline->layout->count; i++)
        groups[i] = (doe_resource_dispatch_group){packet->groups[i], packet->offsets.values[i], packet->offsets.counts[i]};
    bool has_dynamic;
    status = resource_shader_bindings_validate(pipeline->layout, groups, pipeline->layout->count,
        packet->indirect, &has_dynamic);
    if (status != DOE_RESOURCE_OK) return status;
    for (size_t i = 0; i < pipeline->desc.vertex_count; i++) {
        if (!doe_resource_render_pipeline_vertex_required(pipeline, i)) continue;
        const doe_resource_vertex_layout *layout = &pipeline->desc.vertices[i];
        bool instance = layout->step_mode == WGPUVertexStepMode_Instance;
        status = validate_vertex(&packet->vertices[i], layout, instance ? packet->first_instance : packet->first,
            instance ? packet->instances : packet->count, !packet->indirect && (!packet->indexed || instance));
        if (status != DOE_RESOURCE_OK) return status;
    }
    if (packet->indexed) {
        bool strip = pipeline->desc.topology == WGPUPrimitiveTopology_LineStrip ||
            pipeline->desc.topology == WGPUPrimitiveTopology_TriangleStrip;
        if (strip && packet->index_format != pipeline->desc.strip_index_format) return DOE_RESOURCE_LAYOUT;
        if (!resource_buffer_available(packet->index.buffer)) return DOE_RESOURCE_UNAVAILABLE;
        if (packet->index.buffer->mapped || packet->index.buffer->map_pending) return DOE_RESOURCE_STATE;
        uint32_t bytes = packet->index_format == WGPUIndexFormat_Uint16 ? 2 : 4;
        uint64_t end = ((uint64_t)packet->first + packet->count) * bytes;
        if (!packet->indirect && end > packet->index.size) return DOE_RESOURCE_RANGE;
    }
    return DOE_RESOURCE_OK;
}

void resource_render_clear_bindings(doe_resource_render_packet *state)
{
    doe_resource_render_packet old = *state;
    *state = (doe_resource_render_packet){
        .targets = old.targets, .state = old.state,
        .occlusion_active = old.occlusion_active, .occlusion_index = old.occlusion_index
    };
    /* Move attachment/query ownership back into state; release only bindings. */
    old.targets = (doe_resource_render_targets){0};
    resource_render_packet_release(&old);
}
