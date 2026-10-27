/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "doe_native_external_texture.h"
#include <stddef.h>

#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(WGPUExternalTextureDescriptor) == 112, "selected Dawn external descriptor ABI");
_Static_assert(offsetof(WGPUExternalTextureDescriptor, cropOrigin) == 40, "external crop ABI");
_Static_assert(offsetof(WGPUExternalTextureDescriptor, rotation) == 108, "external rotation ABI");
#endif

static bool identity(const float *values, const float *expected, size_t count)
{
    if (!values) return false;
    for (size_t i = 0; i < count; ++i)
        if (values[i] != expected[i]) return false;
    return true;
}

/* The existing shader lowering samples plane zero directly. Admit only the
 * descriptor subset for which that operation implements the requested math.
 * Nonidentity descriptors must never silently render unconverted pixels. */
static doe_resource_status browser_plane(const WGPUExternalTextureDescriptor *desc,
    doe_resource_view **out)
{
    if (!desc || desc->nextInChain || !desc->plane0 || desc->plane1 ||
        !desc->srcTransferFunctionParameters || !desc->dstTransferFunctionParameters ||
        !desc->gamutConversionMatrix || desc->mirrored ||
        desc->rotation != WGPUExternalTextureRotation_Rotate0Degrees)
        return DOE_RESOURCE_INVALID_ARGUMENT;
    static const float transfer[7] = {1, 1, 0, 1, 0, 0, 0};
    static const float gamut[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    if (!desc->doYuvToRgbConversionOnly &&
        (!identity(desc->srcTransferFunctionParameters, transfer, 7) ||
         !identity(desc->dstTransferFunctionParameters, transfer, 7) ||
         !identity(desc->gamutConversionMatrix, gamut, 9)))
        return DOE_RESOURCE_UNSUPPORTED;
    doe_resource_view *view = (doe_resource_view *)desc->plane0;
    const doe_resource_view_desc *view_desc = doe_resource_view_description(view);
    const doe_resource_texture_desc *texture = doe_resource_texture_description(doe_resource_view_texture(view));
    if (!view_desc || !texture || view_desc->base_mip >= 32) return DOE_RESOURCE_INVALID_ARGUMENT;
    uint32_t width = texture->width >> view_desc->base_mip;
    uint32_t height = texture->height >> view_desc->base_mip;
    if (!width) width = 1;
    if (!height) height = 1;
    if (desc->cropOrigin.x || desc->cropOrigin.y || desc->cropSize.width != width ||
        desc->cropSize.height != height || desc->apparentSize.width != width ||
        desc->apparentSize.height != height) return DOE_RESOURCE_UNSUPPORTED;
    uint32_t components = 0;
    WGPUTextureSampleType sample = WGPUTextureSampleType_Undefined;
    doe_resource_status status = doe_resource_format_color_components(view_desc->format, &components);
    if (status == DOE_RESOURCE_OK)
        status = doe_resource_format_sample_type(view_desc->format, view_desc->aspect, &sample);
    if (status != DOE_RESOURCE_OK) return status;
    if (components != 4 || sample != WGPUTextureSampleType_Float) return DOE_RESOURCE_UNSUPPORTED;
    *out = view;
    return DOE_RESOURCE_OK;
}

WGPUExternalTexture wgpuDeviceCreateExternalTexture(WGPUDevice handle,
    const WGPUExternalTextureDescriptor *descriptor)
{
    native_device *device = (native_device *)handle;
    if (!device) return NULL;
    native_device_retain(device);
    doe_resource_external_texture *texture = NULL;
    doe_resource_view *plane = NULL;
    native_lock(device);
    doe_resource_status status = browser_plane(descriptor, &plane);
    if (status == DOE_RESOURCE_OK)
        status = doe_resource_external_texture_create(&device->resources, plane, NULL, &texture);
    if (status != DOE_RESOURCE_OK) {
        texture = doe_resource_external_texture_error(&device->resources);
        if (!texture) status = DOE_RESOURCE_OUT_OF_MEMORY;
    }
    native_unlock(device);
    if (status != DOE_RESOURCE_OK)
        native_error(device, native_resource_error(status), "external texture descriptor or conversion unsupported");
    if (texture && descriptor) native_label(device, texture, descriptor->label);
    native_device_release(device);
    return (WGPUExternalTexture)texture;
}
