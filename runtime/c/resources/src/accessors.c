/* SPDX-License-Identifier: MIT */
#include "internal.h"

void *doe_resource_buffer_device(const doe_resource_buffer *buffer)
{
    return buffer ? buffer->base.config.device.identity : NULL;
}
void *doe_resource_texture_device(const doe_resource_texture *texture)
{
    return texture ? texture->base.config.device.identity : NULL;
}
void *doe_resource_view_device(const doe_resource_view *view)
{
    return view ? view->base.config.device.identity : NULL;
}
void *doe_resource_sampler_device(const doe_resource_sampler *sampler)
{
    return sampler ? sampler->base.config.device.identity : NULL;
}
void *doe_resource_layout_device(const doe_resource_layout *layout)
{
    return layout ? layout->base.config.device.identity : NULL;
}
void *doe_resource_pipeline_layout_device(const doe_resource_pipeline_layout *layout)
{
    return layout ? layout->base.config.device.identity : NULL;
}
void *doe_resource_bind_group_device(const doe_resource_bind_group *group)
{
    return group ? group->base.config.device.identity : NULL;
}
void *doe_resource_pipeline_device(const doe_resource_pipeline *pipeline)
{
    return pipeline ? pipeline->base.config.device.identity : NULL;
}
void *doe_resource_encoder_device(const doe_resource_encoder *encoder)
{
    return encoder ? encoder->base.config.device.identity : NULL;
}
void *doe_resource_commands_device(const doe_resource_commands *commands)
{
    return commands ? commands->base.config.device.identity : NULL;
}
WGPUBufferMapState doe_resource_buffer_map_state(const doe_resource_buffer *buffer)
{
    if (!buffer || buffer->destroyed) return WGPUBufferMapState_Unmapped;
    if (buffer->map_pending) return WGPUBufferMapState_Pending;
    return buffer->mapped ? WGPUBufferMapState_Mapped : WGPUBufferMapState_Unmapped;
}

doe_resource_status doe_resource_buffer_write_validate(const doe_resource_buffer *buffer,
    const void *device, uint64_t offset, uint64_t size)
{
    if (!device || !buffer) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (!resource_buffer_available(buffer)) return DOE_RESOURCE_UNAVAILABLE;
    if (buffer->base.config.device.identity != device) return DOE_RESOURCE_DEVICE_MISMATCH;
    if (buffer->mapped || buffer->map_pending) return DOE_RESOURCE_STATE;
    if (!(buffer->desc.usage & WGPUBufferUsage_CopyDst)) return DOE_RESOURCE_USAGE;
    if (offset % 4 || size % 4) return DOE_RESOURCE_ALIGNMENT;
    if (offset > buffer->desc.size || size > buffer->desc.size - offset) return DOE_RESOURCE_RANGE;
    return DOE_RESOURCE_OK;
}

size_t doe_resource_pipeline_layout_count(const doe_resource_pipeline_layout *layout)
{
    return layout ? layout->count : 0;
}
doe_resource_layout *doe_resource_pipeline_layout_group(const doe_resource_pipeline_layout *layout,
    size_t index)
{
    return layout && index < layout->count ? layout->groups[index] : NULL;
}
doe_resource_pipeline_layout *doe_resource_pipeline_get_layout(const doe_resource_pipeline *pipeline)
{
    return pipeline ? pipeline->layout : NULL;
}

void *doe_resource_compute_pass_device(const doe_resource_compute_pass *pass)
{
    return pass ? pass->base.config.device.identity : NULL;
}

WGPUMapMode doe_resource_buffer_map_mode(const doe_resource_buffer *buffer)
{
    return buffer ? buffer->map_mode : WGPUMapMode_None;
}

doe_resource_status doe_resource_buffer_map_span(const doe_resource_buffer *buffer,
    uint64_t *offset, uint64_t *size)
{
    if (!buffer || !offset || !size) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (!resource_buffer_available(buffer)) return DOE_RESOURCE_UNAVAILABLE;
    if (!buffer->mapped && !buffer->map_pending) return DOE_RESOURCE_STATE;
    *offset = buffer->map_offset;
    *size = buffer->map_size;
    return DOE_RESOURCE_OK;
}

const WGPUSamplerDescriptor *doe_resource_sampler_description(const doe_resource_sampler *sampler)
{
    return sampler ? &sampler->desc : NULL;
}

bool doe_resource_pipeline_layout_group_required(const doe_resource_pipeline_layout *layout, size_t index)
{
    return layout && index < layout->count && layout->groups[index]->count != 0;
}

void *doe_resource_render_pipeline_device(const doe_resource_render_pipeline *pipeline)
{
    return pipeline ? pipeline->base.config.device.identity : NULL;
}
doe_resource_pipeline_layout *doe_resource_render_pipeline_get_layout(const doe_resource_render_pipeline *pipeline)
{
    return pipeline ? pipeline->layout : NULL;
}
const doe_resource_render_pipeline_desc *doe_resource_render_pipeline_description(const doe_resource_render_pipeline *pipeline)
{
    return pipeline ? &pipeline->desc : NULL;
}
void *doe_resource_render_pass_device(const doe_resource_render_pass *pass)
{
    return pass ? pass->base.config.device.identity : NULL;
}
