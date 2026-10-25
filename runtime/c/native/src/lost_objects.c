/* SPDX-License-Identifier: MIT
 * Creation on a device that is no longer alive (destroy() or loss) returns the
 * resource owner's WebGPU invalid object instead of NULL, and reports nothing:
 * WebGPU generates no errors once a device is lost (see native_error). These
 * objects own no backend allocation; every later operation on them rejects
 * through the resource owner because device liveness never returns. Only an
 * allocation failure returns NULL. */
#include "internal.h"
#include <stdlib.h>

bool native_device_dead(native_device *device)
{
    native_lock(device);
    const bool dead = !native_device_alive(device);
    native_unlock(device);
    return dead;
}

/* A lost buffer keeps a payload without a backend handle, so buffer entry
 * points that read their payload (destroy, unmap) need no lost-object branch. */
static void release_lost_buffer(void *userdata, void *handle)
{
    (void)userdata;
    native_buffer *payload = handle;
    free(payload->mapping);
    free(payload);
}

#define LOST_WITH(device, call, label) do { \
        native_lock(device); \
        void *lost_object = (call); \
        native_unlock(device); \
        if (lost_object) native_label(device, lost_object, label); \
        return lost_object; \
    } while (0)

static WGPUStringView no_label(void) { return (WGPUStringView){NULL, 0}; }

WGPUBuffer native_lost_buffer(native_device *device, const WGPUBufferDescriptor *descriptor)
{
    native_buffer *payload = calloc(1, sizeof(*payload));
    if (!payload) return NULL;
    payload->device = device;
    const doe_resource_buffer_desc desc = {descriptor ? descriptor->size : 0, descriptor ? descriptor->usage : 0, false};
    native_lock(device);
    doe_resource_buffer *buffer = doe_resource_buffer_lost(&device->resources, &desc,
        (doe_resource_native){payload, NULL, release_lost_buffer});
    native_unlock(device);
    if (!buffer) {
        free(payload);
        return NULL;
    }
    if (descriptor) native_label(device, buffer, descriptor->label);
    return (WGPUBuffer)buffer;
}

WGPUTexture native_lost_texture(native_device *device, const WGPUTextureDescriptor *descriptor)
{
    doe_resource_texture_desc desc = {0};
    if (descriptor) desc = (doe_resource_texture_desc){
        .dimension=descriptor->dimension ? descriptor->dimension : WGPUTextureDimension_2D,
        .format=descriptor->format, .width=descriptor->size.width, .height=descriptor->size.height,
        .depth_or_layers=descriptor->size.depthOrArrayLayers, .mip_levels=descriptor->mipLevelCount,
        .samples=descriptor->sampleCount, .usage=descriptor->usage};
    LOST_WITH(device, doe_resource_texture_lost(&device->resources, &desc), descriptor ? descriptor->label : no_label());
}

WGPUTextureView native_lost_view(native_device *device, doe_resource_texture *texture, const WGPUTextureViewDescriptor *descriptor)
{
    doe_resource_view_desc desc = {0};
    if (descriptor) desc = (doe_resource_view_desc){.format=descriptor->format, .dimension=descriptor->dimension,
        .base_mip=descriptor->baseMipLevel, .mip_count=descriptor->mipLevelCount,
        .base_layer=descriptor->baseArrayLayer, .layer_count=descriptor->arrayLayerCount,
        .aspect=descriptor->aspect, .usage=descriptor->usage};
    LOST_WITH(device, doe_resource_view_lost(texture, &desc), descriptor ? descriptor->label : no_label());
}

WGPUSampler native_lost_sampler(native_device *device, const WGPUSamplerDescriptor *descriptor)
{
    LOST_WITH(device, doe_resource_sampler_lost(&device->resources), descriptor ? descriptor->label : no_label());
}

WGPUBindGroupLayout native_lost_bind_group_layout(native_device *device, WGPUStringView label)
{
    LOST_WITH(device, doe_resource_layout_lost(&device->resources), label);
}

WGPUPipelineLayout native_lost_pipeline_layout(native_device *device, WGPUStringView label)
{
    LOST_WITH(device, doe_resource_pipeline_layout_lost(&device->resources), label);
}

WGPUBindGroup native_lost_bind_group(native_device *device, WGPUStringView label)
{
    LOST_WITH(device, doe_resource_bind_group_lost(&device->resources), label);
}

WGPUShaderModule native_lost_shader(native_device *device, WGPUStringView label)
{
    LOST_WITH(device, doe_resource_shader_lost(&device->resources), label);
}

WGPUComputePipeline native_lost_compute_pipeline(native_device *device, WGPUStringView label)
{
    LOST_WITH(device, doe_resource_pipeline_lost(&device->resources), label);
}

WGPURenderPipeline native_lost_render_pipeline(native_device *device, WGPUStringView label)
{
    LOST_WITH(device, doe_resource_render_pipeline_lost(&device->resources), label);
}

WGPUQuerySet native_lost_query_set(native_device *device, const WGPUQuerySetDescriptor *descriptor)
{
    LOST_WITH(device, doe_resource_query_set_lost(&device->resources, descriptor ? descriptor->type : WGPUQueryType_Occlusion,
        descriptor ? descriptor->count : 0), descriptor ? descriptor->label : no_label());
}

WGPUCommandEncoder native_lost_encoder(native_device *device, const WGPUCommandEncoderDescriptor *descriptor)
{
    LOST_WITH(device, doe_resource_encoder_lost(&device->resources), descriptor ? descriptor->label : no_label());
}
