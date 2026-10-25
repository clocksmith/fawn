/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "metal_frontend.h"
#include "d3d12_texture.h"
#include <stdlib.h>

void native_texture_release(void *userdata, void *handle)
{
    (void)userdata;
    native_texture *payload = handle;
    native_lock(payload->device);
    if (payload->device->backend.kind == WGPUBackendType_Metal) doe_mtl_texture_release(payload->metal);
    else if (payload->device->backend.kind == WGPUBackendType_D3D12) {
#if defined(_WIN32)
        doe_dx_texture_release(payload->d3d12);
#endif
    } else doe_vk_texture_release(payload->texture);
    native_unlock(payload->device);
    free(payload);
}

WGPUTexture wgpuDeviceCreateTexture(WGPUDevice handle, const WGPUTextureDescriptor *descriptor)
{
    native_device *device = (native_device *)handle;
    if (!device) return NULL;
    if (device->backend.kind == WGPUBackendType_Metal) return native_metal_create_texture(device, descriptor);
    if (device->backend.kind == WGPUBackendType_D3D12) return native_d3d12_create_texture(device, descriptor);
    if (device->backend.kind != WGPUBackendType_Vulkan) {
        native_error(device, WGPUErrorType_Validation, "operation not yet composed for selected backend");
        return NULL;
    }
    /* Transient attachments are ordinary device-local attachments here; the
     * resource owner enforces their usage and clear/discard rules. */
    const WGPUTextureUsage supported = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst |
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding | WGPUTextureUsage_RenderAttachment |
        WGPUTextureUsage_TransientAttachment;
    /* The header requires accepting WGPUTextureBindingViewDimension. This provider
     * has no compatibility-mode view restrictions (flexible views, as in core
     * mode), so its contents are ignored exactly like on a core device. Other
     * or repeated chain nodes remain unsupported. */
    const WGPUChainedStruct *chain = descriptor ? descriptor->nextInChain : NULL;
    bool chain_supported = !chain || (chain->sType == WGPUSType_TextureBindingViewDimension && !chain->next);
    if (!descriptor || !chain_supported ||
        descriptor->usage & ~supported || (descriptor->viewFormatCount && !descriptor->viewFormats)) {
        native_error(device, WGPUErrorType_Validation, "texture descriptor unsupported");
        return NULL;
    }
    /* viewFormats compatibility is the resource owner's rule; the backend owns
     * the native mutable-format list and admits only srgb/unorm pairs. */
    doe_vk_texture_format view_formats[2];
    uint32_t view_format_count = 0;
    for (size_t i = 0; i < descriptor->viewFormatCount; ++i) {
        doe_vk_texture_format mapped;
        if (descriptor->viewFormats[i] == descriptor->format) continue;
        if (view_format_count == 2 || !native_texture_format(device->features, descriptor->viewFormats[i], &mapped)) {
            native_error(device, WGPUErrorType_Validation, "texture format reinterpretation unsupported");
            return NULL;
        }
        view_formats[view_format_count++] = mapped;
    }
    doe_resource_texture_desc desc = {
        .dimension=descriptor->dimension ? descriptor->dimension : WGPUTextureDimension_2D,
        .format=descriptor->format, .width=descriptor->size.width, .height=descriptor->size.height,
        .depth_or_layers=descriptor->size.depthOrArrayLayers, .mip_levels=descriptor->mipLevelCount,
        .samples=descriptor->sampleCount, .usage=descriptor->usage,
        .view_formats=descriptor->viewFormats, .view_format_count=descriptor->viewFormatCount
    };
    doe_resource_texture_limits limits = {device->backend.limits.maxTextureDimension1D,
        device->backend.limits.maxTextureDimension2D, device->backend.limits.maxTextureDimension3D,
        device->backend.limits.maxTextureArrayLayers};
    doe_vk_texture_desc native = {.width=desc.width, .height=desc.height,
        .depth_or_layers=desc.depth_or_layers, .mip_levels=desc.mip_levels,.sample_count=desc.samples,
        .usage=DOE_VK_TEXTURE_COPY_DST, /* Initialization is internal usage only. */
        .view_formats=view_format_count ? view_formats : NULL,.view_format_count=view_format_count};
    if (doe_resource_texture_validate(&desc, &limits) != DOE_RESOURCE_OK ||
        !native_texture_format(device->features, desc.format, &native.format) ||
        ((desc.usage & WGPUTextureUsage_RenderAttachment) && !native_color_renderable(device->features, desc.format))) {
        native_error(device, WGPUErrorType_Validation, "texture shape or format unsupported");
        return NULL;
    }
    /* 3D BC textures need texture-compression-bc-sliced-3d as well. */
    if (desc.dimension == WGPUTextureDimension_3D && desc.format >= WGPUTextureFormat_BC1RGBAUnorm &&
        desc.format <= WGPUTextureFormat_BC7RGBAUnormSrgb &&
        !native_feature_enabled(device, WGPUFeatureName_TextureCompressionBCSliced3D)) {
        native_error(device, WGPUErrorType_Validation, "3D BC textures need texture-compression-bc-sliced-3d");
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
    bool depth_stencil = native_depth_format(device->features, desc.format, &native.format);
    if (depth_stencil) {
        /* Attachment, copy and sampled usage are admitted; per-format/aspect copy
         * direction and sample types belong to the resource owner. No storage. */
        if (desc.usage & WGPUTextureUsage_StorageBinding) {
            native_error(device, WGPUErrorType_Validation, "depth/stencil storage is unsupported");
            return NULL;
        }
        /* Attachments zero-initialize through real clears; copy-only images need
         * internal transfer-destination usage for their zero uploads instead. */
        native.usage = (desc.usage & WGPUTextureUsage_RenderAttachment) ?
            DOE_VK_TEXTURE_DEPTH_STENCIL_ATTACHMENT : DOE_VK_TEXTURE_COPY_DST;
        if (desc.usage & WGPUTextureUsage_CopyDst) native.usage |= DOE_VK_TEXTURE_COPY_DST;
        /* Cube compatibility only serves sampled cube views of depth. */
        if (!(desc.usage & WGPUTextureUsage_TextureBinding)) native.cube_compatible = false;
    }
    if (desc.samples > 1) {
        /* Resource validation requires RenderAttachment and rejects storage;
         * copy usage serves equal-count whole-subresource texture copies. */
        const WGPUTextureUsage extra = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst |
            WGPUTextureUsage_TransientAttachment;
        if ((desc.usage & ~extra) != WGPUTextureUsage_RenderAttachment) {
            native_error(device, WGPUErrorType_Validation, "multisample textures need attachment usage and no storage");
            return NULL;
        }
        native.usage = depth_stencil ? DOE_VK_TEXTURE_DEPTH_STENCIL_ATTACHMENT : DOE_VK_TEXTURE_COLOR_ATTACHMENT;
        if (desc.usage & WGPUTextureUsage_CopyDst) native.usage |= DOE_VK_TEXTURE_COPY_DST;
        native.cube_compatible = false;
    }
    if (desc.usage & WGPUTextureUsage_CopySrc) native.usage |= DOE_VK_TEXTURE_COPY_SRC;
    if (desc.usage & WGPUTextureUsage_TextureBinding) native.usage |= DOE_VK_TEXTURE_SAMPLED;
    if (desc.usage & WGPUTextureUsage_StorageBinding) native.usage |= DOE_VK_TEXTURE_STORAGE;
    if (!depth_stencil && (desc.usage & WGPUTextureUsage_RenderAttachment)) native.usage |= DOE_VK_TEXTURE_COLOR_ATTACHMENT;
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
    doe_vk_status status = native_device_status(device);
    native_failure failure = native_failure_status(native_from_vulkan(status));
    if (status == DOE_VK_OK) {
        failure = native_settle_routine(device).failure;
        status = native_to_vulkan(failure.status);
    }
    if (status == DOE_VK_OK) status = doe_vk_texture_format_caps(device->backend.value.vulkan.context, native.format, &caps);
    if (status == DOE_VK_OK) {
        status = doe_vk_texture_create(device->backend.value.vulkan.context, &native, &payload->texture, &error);
        failure = native_failure_vulkan(status, &error);
    }
    if (status == DOE_VK_OK) {
        failure = native_queue_texture_initialization(device, payload->texture, &native, &caps);
        status = native_to_vulkan(failure.status);
    }

    doe_resource_status adopted = DOE_RESOURCE_INVALID_ARGUMENT;
    if (status == DOE_VK_OK)
        adopted = doe_resource_texture_create(&device->resources, &desc, &limits,
            (doe_resource_native){payload, NULL, native_texture_release}, &texture);
    native_unlock(device);
    if (!texture) {
        native_texture_release(NULL, payload);
        char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
        if (failure.status == NATIVE_OK) failure = native_failure_status(NATIVE_FAILURE);
        native_failure_format(&failure, "texture creation or proven zero initialization failed", message);
        native_error(device, status != DOE_VK_OK ? native_backend_error(status) : native_resource_error(adopted), message);

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
/* Core-mode semantics: the compatibility textureBindingViewDimension is not
 * retained when texture views are flexible, so every texture reports Undefined
 * (matching the reference core implementation), never a guessed default. */
WGPUTextureViewDimension wgpuTextureGetTextureBindingViewDimension(WGPUTexture texture)
{
    (void)texture;
    return WGPUTextureViewDimension_Undefined;
}
