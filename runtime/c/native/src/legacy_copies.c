/* SPDX-License-Identifier: MIT
 * Retained browser copy entrypoints compose ordinary queue/copy/resource owners. */
#include "internal.h"

void wgpuQueueCopyTextureForBrowser(WGPUQueue handle, const WGPUTexelCopyTextureInfo *source,
    const WGPUTexelCopyTextureInfo *destination, const WGPUExtent3D *size,
    const DoeLegacyCopyTextureForBrowserOptions *options)
{
    native_device *device = (native_device *)handle;
    if (!device) return;
    if (!source || !destination || !size || (options && (options->nextInChain ||
        options->needsColorSpaceConversion || options->srcTransferFunctionParameters ||
        options->conversionMatrix || options->dstTransferFunctionParameters ||
        options->srcAlphaMode != options->dstAlphaMode || options->internalUsage))) {
        native_error(device, WGPUErrorType_Validation, "browser copy requires plain texel copy or flipY");
        return;
    }
    bool flip = options && options->flipY && size->height > 1;
    if (flip && (source->origin.y > UINT32_MAX - size->height ||
        destination->origin.y > UINT32_MAX - size->height)) {
        native_error(device, WGPUErrorType_Validation, "browser copy row range overflows");
        return;
    }
    /* Preflight the whole extent through the canonical owner before a row loop.
     * Malicious oversized heights must not turn a rejected copy into long work. */
    doe_resource_texture_blit copy = {
        .source={(doe_resource_texture *)source->texture,source->mipLevel,
            {source->origin.x,source->origin.y,source->origin.z},source->aspect},
        .destination={(doe_resource_texture *)destination->texture,destination->mipLevel,
            {destination->origin.x,destination->origin.y,destination->origin.z},destination->aspect},
        .width=size->width,.height=size->height,.depth_or_layers=size->depthOrArrayLayers};
    doe_resource_texture_blit normalized;
    native_lock(device);
    doe_resource_status status = doe_resource_texture_blit_validate(device, &copy,
        DOE_RESOURCE_COPY_WEBGPU, &normalized);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) {
        native_error(device, WGPUErrorType_Validation, "browser texture copy extent or usage invalid");
        return;
    }
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder((WGPUDevice)device, NULL);
    if (!encoder) return;
    WGPUTexelCopyTextureInfo src = *source, dst = *destination;
    WGPUExtent3D extent = *size;
    uint32_t rows = flip ? size->height : 1;
    if (flip) extent.height = 1;
    for (uint32_t row = 0; row < rows; ++row) {
        if (flip) {
            src.origin.y = source->origin.y + size->height - 1 - row;
            dst.origin.y = destination->origin.y + row;
        }
        wgpuCommandEncoderCopyTextureToTexture(encoder, &src, &dst, &extent);
    }
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuCommandEncoderRelease(encoder);
    if (!command) return;
    wgpuQueueSubmit(handle, 1, &command);
    wgpuCommandBufferRelease(command);
}

void wgpuQueueCopyExternalTextureForBrowser(WGPUQueue handle,
    const DoeLegacyImageCopyExternalTexture *source, const WGPUTexelCopyTextureInfo *destination,
    const WGPUExtent3D *size, const DoeLegacyCopyTextureForBrowserOptions *options)
{
    native_device *device = (native_device *)handle;
    if (!device) return;
    if (!source || source->nextInChain || !size || !destination) {
        native_error(device, WGPUErrorType_Validation, "external copy source missing or chained");
        return;
    }
    doe_resource_external_texture *external = (doe_resource_external_texture *)source->externalTexture;
    native_lock(device);
    doe_resource_view *view = doe_resource_external_texture_plane(external, 0);
    bool available = doe_resource_external_texture_available(external) &&
        doe_resource_view_device(view) == device;
    WGPUTexelCopyTextureInfo copy = {0};
    if (available) {
        const doe_resource_view_desc *desc = doe_resource_view_description(view);
        copy.texture = (WGPUTexture)doe_resource_view_texture(view);
        copy.mipLevel = desc->base_mip;
        copy.origin = source->origin;
        const doe_resource_texture_desc *texture = doe_resource_texture_description(
            doe_resource_view_texture(view));
        uint32_t width = texture->width >> desc->base_mip;
        uint32_t height = texture->height >> desc->base_mip;
        if (!width) width = 1;
        if (!height) height = 1;
        available = copy.origin.x <= width && size->width <= width - copy.origin.x &&
            copy.origin.y <= height && size->height <= height - copy.origin.y &&
            copy.origin.z <= desc->layer_count && size->depthOrArrayLayers <= desc->layer_count - copy.origin.z &&
            copy.origin.z <= UINT32_MAX - desc->base_layer;
        if (available) copy.origin.z += desc->base_layer;
        copy.aspect = WGPUTextureAspect_All;
        if (available) doe_resource_external_texture_retain(external);
    }
    native_unlock(device);
    if (!available) {
        native_error(device, WGPUErrorType_Validation, "external copy source expired, unavailable or wrong device");
        return;
    }
    wgpuQueueCopyTextureForBrowser(handle, &copy, destination, size, options);
    /* Submitted copy owns its actual plane-zero texture through normal commands. */
    doe_resource_external_texture_release(external);
}
void wgpuQueueCopyExternalImageToTexture(WGPUQueue queue,
    const DoeLegacyImageCopyExternalTexture *source, const WGPUTexelCopyTextureInfo *destination,
    const WGPUExtent3D *size)
{
    wgpuQueueCopyExternalTextureForBrowser(queue, source, destination, size, NULL);
}
