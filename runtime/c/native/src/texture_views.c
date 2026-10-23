/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <stdlib.h>

static void release_view(void *userdata, void *handle)
{
    (void)userdata;
    native_view *payload = handle;
    native_lock(payload->device);
    doe_vk_texture_view_release(payload->view);
    native_unlock(payload->device);
    free(payload);
}
WGPUTextureView wgpuTextureCreateView(WGPUTexture handle, const WGPUTextureViewDescriptor *descriptor)
{
    doe_resource_texture *texture = (doe_resource_texture *)handle;
    native_device *device = doe_resource_texture_device(texture);
    if (!device) return NULL;
    WGPUTextureViewDescriptor defaults = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    if (!descriptor) descriptor = &defaults;
    doe_resource_view_desc desc = {.format=descriptor->format, .dimension=descriptor->dimension,
        .base_mip=descriptor->baseMipLevel, .mip_count=descriptor->mipLevelCount,
        .base_layer=descriptor->baseArrayLayer, .layer_count=descriptor->arrayLayerCount,
        .aspect=descriptor->aspect, .usage=descriptor->usage};
    doe_resource_view_desc resolved;
    native_lock(device);
    /* Public zero is an explicit invalid count. Only UINT32_MAX is the pinned
     * default sentinel; compact resource callers also permit zero defaults. */
    doe_resource_status status = descriptor->nextInChain ? DOE_RESOURCE_UNSUPPORTED :
        !descriptor->mipLevelCount || !descriptor->arrayLayerCount ? DOE_RESOURCE_RANGE :
        doe_resource_view_validate(texture, &desc, &resolved);
    doe_vk_texture_view_desc native = {0};
    if (status == DOE_RESOURCE_OK) {
        /* A DepthOnly/StencilOnly view resolves to its aspect format; the backend
         * view selects that native aspect over the same parent image. Any other
         * format difference is reinterpretation, still unsupported here. */
        WGPUTextureFormat aspect_format;
        if (doe_resource_format_aspect_format(doe_resource_texture_description(texture)->format,
                resolved.aspect, &aspect_format) != DOE_RESOURCE_OK || resolved.format != aspect_format ||
            !native_texture_aspect(resolved.aspect, &native.aspect)) status = DOE_RESOURCE_UNSUPPORTED;
        native.base_mip = resolved.base_mip;
        native.mip_count = resolved.mip_count;
        native.base_layer = resolved.base_layer;
        native.layer_count = resolved.layer_count;
        switch (resolved.dimension) {
        case WGPUTextureViewDimension_1D: native.dimension = DOE_VK_VIEW_1D; break;
        case WGPUTextureViewDimension_2D: native.dimension = DOE_VK_VIEW_2D; break;
        case WGPUTextureViewDimension_2DArray: native.dimension = DOE_VK_VIEW_2D_ARRAY; break;
        case WGPUTextureViewDimension_3D: native.dimension = DOE_VK_VIEW_3D; break;
        case WGPUTextureViewDimension_Cube: native.dimension = DOE_VK_VIEW_CUBE; break;
        default: status = DOE_RESOURCE_UNSUPPORTED; break;
        }
    }
    native_view *payload = status == DOE_RESOURCE_OK ? calloc(1, sizeof(*payload)) : NULL;
    doe_resource_view *view = NULL;
    doe_vk_status backend = DOE_VK_OK;
    if (payload) {
        payload->device = device;
        native_texture *parent = doe_resource_texture_native(texture);
        doe_vk_error error = {0};
        backend = doe_vk_texture_view_create(parent->texture, &native, &payload->view, &error);
        if (backend == DOE_VK_OK)
            status = doe_resource_view_create(texture, &resolved,
                (doe_resource_native){payload, NULL, release_view}, &view);
    } else if (status == DOE_RESOURCE_OK) status = DOE_RESOURCE_OUT_OF_MEMORY;
    native_unlock(device);
    if (!view) {
        if (payload) release_view(NULL, payload);
        native_error(device, backend != DOE_VK_OK ? native_backend_error(backend) : native_resource_error(status),
            "texture view creation failed or unsupported");
    } else native_label(device, view, descriptor->label);
    return (WGPUTextureView)view;
}
void wgpuTextureViewAddRef(WGPUTextureView view) { doe_resource_view_retain((doe_resource_view *)view); }
void wgpuTextureViewRelease(WGPUTextureView view) { doe_resource_view_release((doe_resource_view *)view); }
void wgpuTextureViewSetLabel(WGPUTextureView view, WGPUStringView label)
{
    native_label(doe_resource_view_device((doe_resource_view *)view), view, label);
}

static void release_sampler(void *userdata, void *handle)
{
    (void)userdata;
    native_sampler *payload = handle;
    native_lock(payload->device);
    doe_vk_sampler_release(payload->sampler);
    native_unlock(payload->device);
    free(payload);
}
static doe_vk_address address_mode(WGPUAddressMode mode)
{
    switch (mode) {
    case WGPUAddressMode_Repeat: return DOE_VK_ADDRESS_REPEAT;
    case WGPUAddressMode_MirrorRepeat: return DOE_VK_ADDRESS_MIRROR;
    default: return DOE_VK_ADDRESS_CLAMP; /* Normalized preflight admitted Clamp. */
    }
}
WGPUSampler wgpuDeviceCreateSampler(WGPUDevice handle, const WGPUSamplerDescriptor *descriptor)
{
    native_device *device = (native_device *)handle;
    if (!device) return NULL;
    if (device->backend.kind != WGPUBackendType_Vulkan) {
        native_error(device, WGPUErrorType_Validation, "operation not yet composed for selected backend");
        return NULL;
    }
    WGPUSamplerDescriptor defaults = WGPU_SAMPLER_DESCRIPTOR_INIT;
    if (!descriptor) descriptor = &defaults;
    WGPUSamplerDescriptor resolved;
    if (doe_resource_sampler_validate(descriptor, &resolved) != DOE_RESOURCE_OK || resolved.maxAnisotropy != 1) {
        native_error(device, WGPUErrorType_Validation, "sampler invalid or anisotropy not enabled");
        return NULL;
    }
    doe_vk_sampler_desc native = {
        .min_filter=resolved.minFilter == WGPUFilterMode_Linear ? DOE_VK_FILTER_LINEAR : DOE_VK_FILTER_NEAREST,
        .mag_filter=resolved.magFilter == WGPUFilterMode_Linear ? DOE_VK_FILTER_LINEAR : DOE_VK_FILTER_NEAREST,
        .mipmap_filter=resolved.mipmapFilter == WGPUMipmapFilterMode_Linear ? DOE_VK_FILTER_LINEAR : DOE_VK_FILTER_NEAREST,
        .address_u=address_mode(resolved.addressModeU), .address_v=address_mode(resolved.addressModeV),
        .address_w=address_mode(resolved.addressModeW), .min_lod=resolved.lodMinClamp, .max_lod=resolved.lodMaxClamp,
        .compare_enabled=resolved.compare != WGPUCompareFunction_Undefined
    };
    if (!native_compare_operation(resolved.compare, 0, &native.compare_operation)) {
        native_error(device, WGPUErrorType_Validation, "sampler comparison invalid");
        return NULL;
    }
    native_sampler *payload = calloc(1, sizeof(*payload));
    if (!payload) { native_error(device, WGPUErrorType_OutOfMemory, "sampler allocation failed"); return NULL; }
    payload->device = device;
    doe_resource_sampler *sampler = NULL;
    doe_vk_error error = {0};
    native_lock(device);
    doe_vk_status status = native_device_status(device);
    if (status == DOE_VK_OK) status = doe_vk_sampler_create(device->backend.value.vulkan.context, &native, &payload->sampler, &error);
    doe_resource_status adopted = DOE_RESOURCE_INVALID_ARGUMENT;
    if (status == DOE_VK_OK)
        adopted = doe_resource_sampler_create(&device->resources, &resolved,
            (doe_resource_native){payload, NULL, release_sampler}, &sampler);
    native_unlock(device);
    if (!sampler) {
        release_sampler(NULL, payload);
        native_error(device, status != DOE_VK_OK ? native_backend_error(status) : native_resource_error(adopted),
            "sampler creation failed");
    } else native_label(device, sampler, descriptor->label);
    return (WGPUSampler)sampler;
}
void wgpuSamplerAddRef(WGPUSampler sampler) { doe_resource_sampler_retain((doe_resource_sampler *)sampler); }
void wgpuSamplerRelease(WGPUSampler sampler) { doe_resource_sampler_release((doe_resource_sampler *)sampler); }
void wgpuSamplerSetLabel(WGPUSampler sampler, WGPUStringView label)
{
    native_label(doe_resource_sampler_device((doe_resource_sampler *)sampler), sampler, label);
}
