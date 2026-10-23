/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <stdlib.h>

static void release_texture(void *userdata, void *handle)
{
    (void)userdata;
    native_texture *payload = handle;
    native_lock(payload->device);
    doe_vk_texture_release(payload->texture);
    native_unlock(payload->device);
    free(payload);
}

/* Real eager initialization, using the same native queue obligation owner as
 * public submissions. Never publish allocator contents or infer completion. */
static doe_vk_status initialize_texture(native_device *device, doe_vk_texture *texture,
    const doe_vk_texture_desc *desc, uint32_t texel_bytes)
{
    uint64_t size = desc->width;
    if (size > SIZE_MAX / desc->height) return DOE_VK_OUT_OF_MEMORY;
    size *= desc->height;
    if (size > SIZE_MAX / desc->depth_or_layers) return DOE_VK_OUT_OF_MEMORY;
    size *= desc->depth_or_layers;
    if (size > SIZE_MAX / texel_bytes) return DOE_VK_OUT_OF_MEMORY;
    size *= texel_bytes;
    doe_vk_buffer *zero = NULL;
    doe_vk_commands *commands = NULL;
    doe_vk_error error = {0};
    doe_vk_status status = doe_vk_buffer_create(device->context, size,
        DOE_VK_BUFFER_COPY_SRC, DOE_VK_MEMORY_HOST, &zero, &error);
    if (status == DOE_VK_OK) status = doe_vk_commands_create(device->context, &commands, &error);
    for (uint32_t mip = 0; status == DOE_VK_OK && mip < desc->mip_levels; ++mip) {
        uint32_t width = desc->width >> mip;
        uint32_t height = desc->height >> mip;
        uint32_t depth = desc->dimension == DOE_VK_TEXTURE_3D ? desc->depth_or_layers >> mip : desc->depth_or_layers;
        doe_vk_texture_region region = {.mip=mip,
            .extent={width ? width : 1, height ? height : 1, depth ? depth : 1}};
        status = doe_vk_commands_upload_texture(commands, zero,
            (doe_vk_texture_buffer_layout){0}, texture, region);
    }
    if (status == DOE_VK_OK) status = native_submit_commands(device, commands);
    else if (doe_vk_commands_release(commands) != DOE_VK_OK) abort();
    doe_vk_buffer_release(zero);
    return status;
}

WGPUTexture wgpuDeviceCreateTexture(WGPUDevice handle, const WGPUTextureDescriptor *descriptor)
{
    native_device *device = (native_device *)handle;
    if (!device) return NULL;
    const WGPUTextureUsage supported = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst |
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding | WGPUTextureUsage_RenderAttachment;
    if (!descriptor || descriptor->nextInChain || descriptor->sampleCount != 1 ||
        descriptor->usage & ~supported || (descriptor->viewFormatCount && !descriptor->viewFormats)) {
        native_error(device, WGPUErrorType_Validation, "texture descriptor or multisampling unsupported");
        return NULL;
    }
    for (size_t i = 0; i < descriptor->viewFormatCount; ++i)
        if (descriptor->viewFormats[i] != descriptor->format) {
            native_error(device, WGPUErrorType_Validation, "texture format reinterpretation unsupported");
            return NULL;
        }
    doe_resource_texture_desc desc = {
        .dimension=descriptor->dimension ? descriptor->dimension : WGPUTextureDimension_2D,
        .format=descriptor->format, .width=descriptor->size.width, .height=descriptor->size.height,
        .depth_or_layers=descriptor->size.depthOrArrayLayers, .mip_levels=descriptor->mipLevelCount,
        .samples=descriptor->sampleCount, .usage=descriptor->usage,
        .view_formats=descriptor->viewFormats, .view_format_count=descriptor->viewFormatCount
    };
    doe_resource_texture_limits limits = {device->caps.max_texture_dimension_1d,
        device->caps.max_texture_dimension_2d, device->caps.max_texture_dimension_3d,
        device->caps.max_texture_array_layers};
    doe_vk_texture_desc native = {.width=desc.width, .height=desc.height,
        .depth_or_layers=desc.depth_or_layers, .mip_levels=desc.mip_levels,
        .usage=DOE_VK_TEXTURE_COPY_DST}; /* Initialization is internal usage only. */
    if (doe_resource_texture_validate(&desc, &limits) != DOE_RESOURCE_OK ||
        !native_texture_format(desc.format, &native.format)) {
        native_error(device, WGPUErrorType_Validation, "texture shape or format unsupported");
        return NULL;
    }
    switch (desc.dimension) {
    case WGPUTextureDimension_1D: native.dimension = DOE_VK_TEXTURE_1D; break;
    case WGPUTextureDimension_2D: native.dimension = DOE_VK_TEXTURE_2D; break;
    case WGPUTextureDimension_3D: native.dimension = DOE_VK_TEXTURE_3D; break;
    default: abort(); /* Canonical preflight admitted exactly these dimensions. */
    }
    native.cube_compatible = desc.dimension == WGPUTextureDimension_2D &&
        desc.width == desc.height && desc.depth_or_layers >= 6;
    if (desc.usage & WGPUTextureUsage_CopySrc) native.usage |= DOE_VK_TEXTURE_COPY_SRC;
    if (desc.usage & WGPUTextureUsage_TextureBinding) native.usage |= DOE_VK_TEXTURE_SAMPLED;
    if (desc.usage & WGPUTextureUsage_StorageBinding) native.usage |= DOE_VK_TEXTURE_STORAGE;
    if (desc.usage & WGPUTextureUsage_RenderAttachment) native.usage |= DOE_VK_TEXTURE_COLOR_ATTACHMENT;
    native_texture *payload = calloc(1, sizeof(*payload));
    if (!payload) {
        native_error(device, WGPUErrorType_OutOfMemory, "texture payload allocation failed");
        return NULL;
    }
    payload->device = device;
    doe_resource_texture *texture = NULL;
    doe_vk_error error = {0};
    doe_vk_format_caps caps;
    native_lock(device);
    doe_vk_status status = native_device_alive(device) && native_settle_pending(device) ?
        doe_vk_texture_format_caps(device->context, native.format, &caps) : DOE_VK_INVALID_STATE;
    if (status == DOE_VK_OK) status = doe_vk_texture_create(device->context, &native, &payload->texture, &error);
    if (status == DOE_VK_OK) status = initialize_texture(device, payload->texture, &native, caps.texel_bytes);
    doe_resource_status adopted = DOE_RESOURCE_INVALID_ARGUMENT;
    if (status == DOE_VK_OK)
        adopted = doe_resource_texture_create(&device->resources, &desc, &limits,
            (doe_resource_native){payload, NULL, release_texture}, &texture);
    native_unlock(device);
    if (!texture) {
        release_texture(NULL, payload);
        native_error(device, status == DOE_VK_OUT_OF_MEMORY || adopted == DOE_RESOURCE_OUT_OF_MEMORY ?
            WGPUErrorType_OutOfMemory : WGPUErrorType_Validation,
            "texture creation or proven zero initialization failed");
    } else native_label(device, texture, descriptor->label);
    return (WGPUTexture)texture;
}

void wgpuTextureAddRef(WGPUTexture texture) { doe_resource_texture_retain((doe_resource_texture *)texture); }
void wgpuTextureRelease(WGPUTexture texture) { doe_resource_texture_release((doe_resource_texture *)texture); }
void wgpuTextureDestroy(WGPUTexture handle)
{
    doe_resource_texture *texture = (doe_resource_texture *)handle;
    native_device *device = doe_resource_texture_device(texture);
    if (!device) return;
    native_lock(device);
    doe_resource_texture_destroy(texture);
    native_unlock(device);
}
void wgpuTextureSetLabel(WGPUTexture texture, WGPUStringView label)
{
    native_label(doe_resource_texture_device((doe_resource_texture *)texture), texture, label);
}
uint32_t wgpuTextureGetWidth(WGPUTexture texture)
{
    const doe_resource_texture_desc *desc = doe_resource_texture_description((doe_resource_texture *)texture);
    return desc ? desc->width : 0;
}
uint32_t wgpuTextureGetHeight(WGPUTexture texture)
{
    const doe_resource_texture_desc *desc = doe_resource_texture_description((doe_resource_texture *)texture);
    return desc ? desc->height : 0;
}
uint32_t wgpuTextureGetDepthOrArrayLayers(WGPUTexture texture)
{
    const doe_resource_texture_desc *desc = doe_resource_texture_description((doe_resource_texture *)texture);
    return desc ? desc->depth_or_layers : 0;
}
uint32_t wgpuTextureGetMipLevelCount(WGPUTexture texture)
{
    const doe_resource_texture_desc *desc = doe_resource_texture_description((doe_resource_texture *)texture);
    return desc ? desc->mip_levels : 0;
}
uint32_t wgpuTextureGetSampleCount(WGPUTexture texture)
{
    const doe_resource_texture_desc *desc = doe_resource_texture_description((doe_resource_texture *)texture);
    return desc ? desc->samples : 0;
}
WGPUTextureDimension wgpuTextureGetDimension(WGPUTexture texture)
{
    const doe_resource_texture_desc *desc = doe_resource_texture_description((doe_resource_texture *)texture);
    return desc ? desc->dimension : WGPUTextureDimension_Undefined;
}
WGPUTextureFormat wgpuTextureGetFormat(WGPUTexture texture)
{
    const doe_resource_texture_desc *desc = doe_resource_texture_description((doe_resource_texture *)texture);
    return desc ? desc->format : WGPUTextureFormat_Undefined;
}
WGPUTextureUsage wgpuTextureGetUsage(WGPUTexture texture)
{
    const doe_resource_texture_desc *desc = doe_resource_texture_description((doe_resource_texture *)texture);
    return desc ? desc->usage : WGPUTextureUsage_None;
}
