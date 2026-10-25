/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "metal_frontend.h"
#include "surface.h"
#include "doe_native_external_texture.h"
#include "doe_metal_external.h"
#include <stddef.h>
#include <stdlib.h>

#if UINTPTR_MAX == UINT64_MAX
_Static_assert(offsetof(DoeExternalTextureDescriptor, plane0) == 24, "retained external plane0 ABI");
_Static_assert(offsetof(DoeExternalTextureDescriptor, plane1) == 32, "retained external plane1 ABI");
_Static_assert(offsetof(DoeExternalTextureNativeDescriptor, sourceType) == 16, "retained native source ABI");
_Static_assert(offsetof(DoeExternalTextureNativeDescriptor, handle) == 24, "retained native handle ABI");
#endif

static native_device *external_device(doe_resource_external_texture *texture)
{
    return doe_resource_view_device(doe_resource_external_texture_plane(texture, 0));
}

static doe_resource_status adopt_plane(native_device *device, doe_mtl_texture **imported,
    doe_resource_view **out, native_failure *failure)
{
    const doe_mtl_texture_desc *desc = doe_mtl_texture_description(*imported);
    WGPUTextureFormat format;
    switch (desc->format) {
    case DOE_MTL_R8_UNORM: format = WGPUTextureFormat_R8Unorm; break;
    case DOE_MTL_RG8_UNORM: format = WGPUTextureFormat_RG8Unorm; break;
    case DOE_MTL_RGBA8_UNORM: format = WGPUTextureFormat_RGBA8Unorm; break;
    case DOE_MTL_BGRA8_UNORM: format = WGPUTextureFormat_BGRA8Unorm; break;
    default: return DOE_RESOURCE_UNSUPPORTED;
    }
    native_texture *payload = calloc(1, sizeof(*payload));
    native_view *view_payload = calloc(1, sizeof(*view_payload));
    if (!payload || !view_payload) {
        free(payload);
        free(view_payload);
        return DOE_RESOURCE_OUT_OF_MEMORY;
    }
    payload->device = device;
    payload->metal = *imported;
    *imported = NULL;
    view_payload->device = device;
    doe_resource_texture_desc description = {.dimension=WGPUTextureDimension_2D,.format=format,
        .width=desc->width,.height=desc->height,.depth_or_layers=1,.mip_levels=1,.samples=1,
        .usage=WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc};
    doe_resource_texture_limits limits = {device->backend.limits.maxTextureDimension1D,
        device->backend.limits.maxTextureDimension2D,device->backend.limits.maxTextureDimension3D,
        device->backend.limits.maxTextureArrayLayers};
    doe_resource_texture *texture = NULL;
    doe_resource_view *view = NULL;
    doe_resource_view_desc view_desc = {.format=format,.dimension=WGPUTextureViewDimension_2D,
        .mip_count=1,.layer_count=1,.aspect=WGPUTextureAspect_All,.usage=WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc};
    doe_mtl_texture_view_desc native_desc = {.type=DOE_MTL_TEXTURE_2D,.format=desc->format,
        .mip_count=1,.layer_count=1,.red=DOE_MTL_SWIZZLE_RED,.green=DOE_MTL_SWIZZLE_GREEN,
        .blue=DOE_MTL_SWIZZLE_BLUE,.alpha=DOE_MTL_SWIZZLE_ALPHA};
    native_lock(device);
    doe_resource_status status = doe_resource_texture_create(&device->resources, &description, &limits,
        (doe_resource_native){payload,NULL,native_texture_release}, &texture);
    if (status == DOE_RESOURCE_OK) {
        doe_mtl_error detail = {0};
        doe_mtl_status created = doe_mtl_texture_view_create(payload->metal, &native_desc, &view_payload->metal, &detail);
        *failure = native_failure_metal(created, &detail);
        if (failure->status == NATIVE_OK)
            status = doe_resource_view_create(texture, &view_desc,
                (doe_resource_native){view_payload,NULL,native_view_release}, &view);
    }
    native_unlock(device);
    if (!view) native_view_release(NULL, view_payload);
    if (texture) doe_resource_texture_release(texture);
    else native_texture_release(NULL, payload);
    if (view) *out = view;
    return status;
}

static doe_resource_status import_planes(native_device *device,
    const DoeExternalTextureNativeDescriptor *descriptor, doe_resource_view *views[2],
    native_failure *failure)
{
    if (device->backend.kind != WGPUBackendType_Metal || !descriptor->handle ||
        (descriptor->sourceType != DOE_NATIVE_EXTERNAL_IOSURFACE &&
        descriptor->sourceType != DOE_NATIVE_EXTERNAL_CVPIXELBUFFER)) return DOE_RESOURCE_UNSUPPORTED;
    doe_mtl_external_planes imported = {0};
    doe_mtl_error detail = {0};
    native_lock(device);
    *failure = native_failure_status(native_device_result(device));
    if (failure->status == NATIVE_OK) {
        doe_mtl_status status = doe_mtl_external_texture_import(device->backend.value.metal.context,
            (doe_mtl_external_source)descriptor->sourceType, descriptor->handle, &imported, &detail);
        *failure = native_failure_metal(status, &detail);
    }
    native_unlock(device);
    doe_resource_status status = DOE_RESOURCE_OK;
    for (uint32_t i = 0; failure->status == NATIVE_OK && status == DOE_RESOURCE_OK && i < imported.count; ++i)
        status = adopt_plane(device, &imported.planes[i], &views[i], failure);
    native_lock(device);
    for (uint32_t i = 0; i < imported.count; ++i) doe_mtl_texture_release(imported.planes[i]);
    native_unlock(device);
    return status;
}

WGPUExternalTexture doeNativeDeviceCreateExternalTexture(WGPUDevice handle, const void *raw)
{
    native_device *device = (native_device *)handle;
    if (!device || !raw || native_device_dead(device)) return NULL;
    const DoeExternalTextureDescriptor *descriptor = raw;
    const WGPUChainedStruct *chain = descriptor->nextInChain;
    doe_resource_view *views[2] = {NULL, NULL};
    doe_resource_status status = DOE_RESOURCE_OK;
    native_failure failure = native_failure_status(NATIVE_OK);
    if (chain) {
        if (chain->next || (uint32_t)chain->sType != DOE_NATIVE_STYPE_EXTERNAL_TEXTURE ||
            descriptor->plane0 || descriptor->plane1) status = DOE_RESOURCE_INVALID_ARGUMENT;
        else status = import_planes(device, (const DoeExternalTextureNativeDescriptor *)chain, views, &failure);
    }
    doe_resource_external_texture *texture = NULL;
    native_lock(device);
    if (failure.status == NATIVE_OK && status == DOE_RESOURCE_OK)
        status = doe_resource_external_texture_create(&device->resources,
            chain ? views[0] : (doe_resource_view *)descriptor->plane0,
            chain ? views[1] : (doe_resource_view *)descriptor->plane1, &texture);
    native_unlock(device);
    for (uint32_t i = 0; i < 2; ++i) doe_resource_view_release(views[i]);
    if (!texture) {
        if (failure.status != NATIVE_OK) {
            char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
            native_failure_format(&failure, "external texture import failed", message);
            native_error(device, native_result_error(failure.status), message);
        } else native_error(device, native_resource_error(status), "external texture descriptor, planes or device invalid");
    } else native_label(device, texture, descriptor->label);
    return (WGPUExternalTexture)texture;
}

WGPUExternalTexture doeNativeDeviceImportExternalTexture(WGPUDevice device, const void *descriptor)
{
    return doeNativeDeviceCreateExternalTexture(device, descriptor);
}
void wgpuExternalTextureAddRef(WGPUExternalTexture texture)
{
    doe_resource_external_texture_retain((doe_resource_external_texture *)texture);
}
void wgpuExternalTextureRelease(WGPUExternalTexture handle)
{
    doe_resource_external_texture *texture = (doe_resource_external_texture *)handle;
    native_surface_release_hold holds[2];
    for (uint32_t i = 0; i < 2; ++i)
        holds[i] = native_surface_hold_release(doe_resource_view_texture(doe_resource_external_texture_plane(texture, i)));
    doe_resource_external_texture_release(texture);
    for (uint32_t i = 0; i < 2; ++i) native_surface_finish_release(holds[i]);
}
void wgpuExternalTextureSetLabel(WGPUExternalTexture texture, WGPUStringView label)
{
    native_label(external_device((doe_resource_external_texture *)texture), texture, label);
}
void doeNativeExternalTextureAddRef(WGPUExternalTexture texture) { wgpuExternalTextureAddRef(texture); }
void doeNativeExternalTextureRelease(WGPUExternalTexture texture) { wgpuExternalTextureRelease(texture); }
void doeNativeExternalTextureExpire(WGPUExternalTexture handle)
{
    doe_resource_external_texture *texture = (doe_resource_external_texture *)handle;
    native_device *device = external_device(texture);
    if (!device) return;
    native_lock(device);
    doe_resource_external_texture_expire(texture);
    native_unlock(device);
}
void doeNativeExternalTextureDestroy(WGPUExternalTexture texture) { doeNativeExternalTextureExpire(texture); }
void doeNativeExternalTextureRefresh(WGPUExternalTexture handle)
{
    doe_resource_external_texture *texture = (doe_resource_external_texture *)handle;
    native_device *device = external_device(texture);
    if (!device) return;
    native_lock(device);
    doe_resource_status status = doe_resource_external_texture_refresh(texture);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, native_resource_error(status), "external texture refresh unavailable");
}
void doeNativeExternalTextureSetLabel(WGPUExternalTexture texture, const char *label, size_t length)
{
    wgpuExternalTextureSetLabel(texture, (WGPUStringView){label,length});
}
