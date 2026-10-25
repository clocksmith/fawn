/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "metal_frontend.h"
#include "d3d12_texture.h"
#include <stdlib.h>

void native_view_release(void *userdata, void *handle)
{
    (void)userdata;
    native_view *payload = handle;
    native_lock(payload->device);
    if (payload->device->backend.kind == WGPUBackendType_Metal) doe_mtl_texture_view_release(payload->metal);
    else if (payload->device->backend.kind != WGPUBackendType_D3D12) doe_vk_texture_view_release(payload->view);
    native_unlock(payload->device);
    free(payload);
}
/* WebGPU per-channel swizzle to native sources (Undefined is the channel's own
 * component). A non-identity swizzle needs texture-component-swizzle. Its view
 * may carry RenderAttachment or StorageBinding usage: WebGPU rejects such a view
 * where it is used as an attachment, resolve target or storage binding (the
 * resource view's swizzled flag), not at creation. */
static bool view_swizzle(const native_device *device, const WGPUChainedStruct *chain, doe_vk_swizzle out[4])
{
    for (uint32_t i = 0; i < 4; ++i) out[i] = DOE_VK_SWIZZLE_IDENTITY;
    if (!chain) return true;
    if (chain->sType != WGPUSType_TextureComponentSwizzleDescriptor || chain->next) return false;
    const WGPUTextureComponentSwizzle *swizzle = &((const WGPUTextureComponentSwizzleDescriptor *)chain)->swizzle;
    const WGPUComponentSwizzle sources[4] = {swizzle->r, swizzle->g, swizzle->b, swizzle->a};
    const WGPUComponentSwizzle own[4] = {WGPUComponentSwizzle_R, WGPUComponentSwizzle_G,
        WGPUComponentSwizzle_B, WGPUComponentSwizzle_A};
    bool identity = true;
    for (uint32_t i = 0; i < 4; ++i) {
        switch (sources[i]) {
        case WGPUComponentSwizzle_Undefined: out[i] = DOE_VK_SWIZZLE_IDENTITY; break;
        case WGPUComponentSwizzle_Zero: out[i] = DOE_VK_SWIZZLE_ZERO; break;
        case WGPUComponentSwizzle_One: out[i] = DOE_VK_SWIZZLE_ONE; break;
        case WGPUComponentSwizzle_R: out[i] = DOE_VK_SWIZZLE_R; break;
        case WGPUComponentSwizzle_G: out[i] = DOE_VK_SWIZZLE_G; break;
        case WGPUComponentSwizzle_B: out[i] = DOE_VK_SWIZZLE_B; break;
        case WGPUComponentSwizzle_A: out[i] = DOE_VK_SWIZZLE_A; break;
        case WGPUComponentSwizzle_Force32:
        default: return false;
        }
        if (sources[i] == own[i]) out[i] = DOE_VK_SWIZZLE_IDENTITY;
        identity &= out[i] == DOE_VK_SWIZZLE_IDENTITY;
    }
    return identity || native_feature_enabled(device, WGPUFeatureName_TextureComponentSwizzle);
}

WGPUTextureView wgpuTextureCreateView(WGPUTexture handle, const WGPUTextureViewDescriptor *descriptor)
{
    doe_resource_texture *texture = (doe_resource_texture *)handle;
    native_device *device = doe_resource_texture_device(texture);
    if (!device) return NULL;
    if (native_device_dead(device)) return native_lost_view(device, texture, descriptor);
    if (device->backend.kind == WGPUBackendType_Metal) return native_metal_create_view(texture, descriptor);
    if (device->backend.kind == WGPUBackendType_D3D12) return native_d3d12_create_view(texture, descriptor);
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
    doe_resource_status status = !descriptor->mipLevelCount || !descriptor->arrayLayerCount ? DOE_RESOURCE_RANGE :
        doe_resource_view_validate(texture, &desc, &resolved);
    doe_vk_texture_view_desc native = {0};
    if (status == DOE_RESOURCE_OK && !view_swizzle(device, descriptor->nextInChain, native.swizzle))
        status = DOE_RESOURCE_UNSUPPORTED;
    if (status == DOE_RESOURCE_OK) {
        for (uint32_t i = 0; i < 4; ++i) resolved.swizzled |= native.swizzle[i] != DOE_VK_SWIZZLE_IDENTITY;
        /* A DepthOnly/StencilOnly view resolves to its aspect format; the backend
         * view selects that native aspect over the same parent image. */
        WGPUTextureFormat aspect_format;
        if (doe_resource_format_aspect_format(doe_resource_texture_description(texture)->format,
                resolved.aspect, &aspect_format) != DOE_RESOURCE_OK ||
            !native_texture_aspect(resolved.aspect, &native.aspect)) status = DOE_RESOURCE_UNSUPPORTED;
        /* A different full-aspect format is a resource-validated viewFormats
         * entry; the backend admits it only from the image's srgb/unorm list. */
        else if (resolved.format != aspect_format && (resolved.aspect != WGPUTextureAspect_All ||
            !native_texture_format(device->features, resolved.format, &native.format))) status = DOE_RESOURCE_UNSUPPORTED;
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
        case WGPUTextureViewDimension_CubeArray: native.dimension = DOE_VK_VIEW_CUBE_ARRAY; break;
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
                (doe_resource_native){payload, NULL, native_view_release}, &view);
    } else if (status == DOE_RESOURCE_OK) status = DOE_RESOURCE_OUT_OF_MEMORY;
    native_unlock(device);
    if (!view) {
        if (payload) native_view_release(NULL, payload);
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

void native_sampler_release(void *userdata, void *handle)
{
    (void)userdata;
    native_sampler *payload = handle;
    native_lock(payload->device);
    if (payload->device->backend.kind == WGPUBackendType_Metal) doe_mtl_sampler_release(payload->metal);
    else if (payload->device->backend.kind != WGPUBackendType_D3D12) doe_vk_sampler_release(payload->sampler);
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
    if (native_device_dead(device)) return native_lost_sampler(device, descriptor);
    if (device->backend.kind == WGPUBackendType_Metal) return native_metal_create_sampler(device, descriptor);
    if (device->backend.kind == WGPUBackendType_D3D12) return native_d3d12_create_sampler(device, descriptor);
    if (device->backend.kind != WGPUBackendType_Vulkan) {
        native_error(device, WGPUErrorType_Validation, "operation not yet composed for selected backend");
        return NULL;
    }
    WGPUSamplerDescriptor defaults = WGPU_SAMPLER_DESCRIPTOR_INIT;
    if (!descriptor) descriptor = &defaults;
    WGPUSamplerDescriptor resolved;
    if (doe_resource_sampler_validate(descriptor, &resolved) != DOE_RESOURCE_OK) {
        native_error(device, WGPUErrorType_Validation, "sampler descriptor invalid");
        return NULL;
    }
    /* Anisotropy uses the device's enabled samplerAnisotropy and clamps to its
     * maximum; a device without the feature rejects instead of silently using 1. */
    if (resolved.maxAnisotropy > 1 && !device->backend.value.vulkan.caps.sampler_anisotropy) {
        native_error(device, WGPUErrorType_Validation, "sampler anisotropy unsupported by this device");
        return NULL;
    }
    doe_vk_sampler_desc native = {
        .min_filter=resolved.minFilter == WGPUFilterMode_Linear ? DOE_VK_FILTER_LINEAR : DOE_VK_FILTER_NEAREST,
        .mag_filter=resolved.magFilter == WGPUFilterMode_Linear ? DOE_VK_FILTER_LINEAR : DOE_VK_FILTER_NEAREST,
        .mipmap_filter=resolved.mipmapFilter == WGPUMipmapFilterMode_Linear ? DOE_VK_FILTER_LINEAR : DOE_VK_FILTER_NEAREST,
        .address_u=address_mode(resolved.addressModeU), .address_v=address_mode(resolved.addressModeV),
        .address_w=address_mode(resolved.addressModeW), .min_lod=resolved.lodMinClamp, .max_lod=resolved.lodMaxClamp,
        .compare_enabled=resolved.compare != WGPUCompareFunction_Undefined,
        .max_anisotropy=resolved.maxAnisotropy
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
            (doe_resource_native){payload, NULL, native_sampler_release}, &sampler);
    native_unlock(device);
    if (!sampler) {
        native_sampler_release(NULL, payload);
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
