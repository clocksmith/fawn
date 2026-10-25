/* SPDX-License-Identifier: MIT */
#include "objc_abi.h"

/* CoreVideo OSType values, checked against the SDK in tests/sdk_abi.m. */
enum { PIXEL_RGBA = 0x52474241, PIXEL_BGRA = 0x42475241,
    PIXEL_420_VIDEO = 0x34323076, PIXEL_420_FULL = 0x34323066 };

static doe_mtl_status inspect_source(doe_mtl_objc_external *external,
    doe_mtl_external_source source, void *handle, void **surface,
    doe_mtl_native_external *result, doe_mtl_error *error)
{
    bool pixel = source == DOE_MTL_EXTERNAL_CVPIXELBUFFER;
    if (source != DOE_MTL_EXTERNAL_IOSURFACE && !pixel)
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "external source type", error);
    *surface = pixel ? external->pixel_surface(handle) : handle;
    if (!*surface) return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "CVPixelBuffer IOSurface backing", error);
    size_t planes = pixel ? external->pixel_planes(handle) : external->surface_planes(handle);
    size_t surface_planes = external->surface_planes(*surface);
    uint32_t format = pixel ? external->pixel_format(handle) : external->surface_format(handle);
    if (planes != surface_planes || (planes != 0 && planes != 1 && planes != 2))
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "external plane layout", error);
    result->count = planes == 0 ? 1 : (uint32_t)planes;
    if ((result->count == 1 && format != PIXEL_RGBA && format != PIXEL_BGRA) ||
        (result->count == 2 && format != PIXEL_420_VIDEO && format != PIXEL_420_FULL))
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "external pixel format", error);
    for (uint32_t i = 0; i < result->count; ++i) {
        size_t width, height;
        if (!planes) {
            width = pixel ? external->pixel_width(handle) : external->surface_width(handle);
            height = pixel ? external->pixel_height(handle) : external->surface_height(handle);
        } else {
            width = pixel ? external->pixel_plane_width(handle, i) : external->surface_plane_width(handle, i);
            height = pixel ? external->pixel_plane_height(handle, i) : external->surface_plane_height(handle, i);
        }
        size_t surface_width = !surface_planes ? external->surface_width(*surface) : external->surface_plane_width(*surface, i);
        size_t surface_height = !surface_planes ? external->surface_height(*surface) : external->surface_plane_height(*surface, i);
        if (!width || !height || width > UINT32_MAX || height > UINT32_MAX ||
            width != surface_width || height != surface_height)
            return doe_mtl_fail(DOE_MTL_RANGE, "external plane dimensions", error);
        doe_mtl_format native_format = result->count == 2 ? (i == 0 ? DOE_MTL_R8_UNORM : DOE_MTL_RG8_UNORM) :
            format == PIXEL_RGBA ? DOE_MTL_RGBA8_UNORM : DOE_MTL_BGRA8_UNORM;
        result->desc[i] = (doe_mtl_texture_desc){.type=DOE_MTL_TEXTURE_2D,
            .format=native_format,.width=(uint32_t)width,.height=(uint32_t)height,
            .depth_or_layers=1,.mip_levels=1,.sample_count=1,.usage=DOE_MTL_TEXTURE_SAMPLED};
    }
    return DOE_MTL_OK;
}

doe_mtl_status doe_mtl_objc_external_import(void *userdata, void *device,
    doe_mtl_external_source source, void *handle, doe_mtl_native_external *out,
    doe_mtl_error *error)
{
    doe_mtl_objc *runtime = userdata;
    if (!runtime || !device || !handle || !out)
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "external texture import", error);
    doe_mtl_objc_external *external = &runtime->external;
    if (!external->texture_call)
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "IOSurface/CoreVideo import APIs", error);
    if (!runtime->responds_call(device, runtime->responds, external->new_texture))
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "Metal IOSurface textures", error);
    doe_mtl_native_external result = {0};
    void *surface = NULL;
    doe_mtl_status status = inspect_source(external, source, handle, &surface, &result, error);
    if (status != DOE_MTL_OK) return status;
    void *pool = doe_mtl_objc_pool(runtime);
    if (!pool) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "external texture autorelease pool", error);
    for (uint32_t i = 0; i < result.count; ++i) {
        void *descriptor = doe_mtl_objc_object(runtime, runtime->texture_class, runtime->new_object);
        if (!descriptor) {
            status = doe_mtl_fail(DOE_MTL_OUT_OF_MEMORY, "external texture descriptor", error);
            break;
        }
        const doe_mtl_texture_desc *desc = &result.desc[i];
        runtime->set_size_call(descriptor, runtime->set_type, DOE_MTL_TEXTURE_2D);
        runtime->set_size_call(descriptor, runtime->set_format, (size_t)desc->format);
        runtime->set_size_call(descriptor, runtime->set_width, desc->width);
        runtime->set_size_call(descriptor, runtime->set_height, desc->height);
        runtime->set_size_call(descriptor, runtime->set_depth, 1);
        runtime->set_size_call(descriptor, runtime->set_layers, 1);
        runtime->set_size_call(descriptor, runtime->set_mips, 1);
        runtime->set_size_call(descriptor, runtime->set_samples, 1);
        runtime->set_size_call(descriptor, runtime->set_storage, 0); /* MTLStorageModeShared */
        runtime->set_size_call(descriptor, runtime->set_usage, 1); /* MTLTextureUsageShaderRead */
        result.planes[i] = external->texture_call(device, external->new_texture, descriptor, surface, i);
        doe_mtl_objc_void(runtime, descriptor, runtime->release);
        if (!result.planes[i]) {
            status = doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "newTextureWithDescriptor:iosurface:plane:", error);
            break;
        }
    }
    if (status != DOE_MTL_OK) {
        for (uint32_t i = 0; i < result.count; ++i)
            if (result.planes[i]) doe_mtl_objc_void(runtime, result.planes[i], runtime->release);
    }
    doe_mtl_objc_drain(runtime, pool);
    if (status != DOE_MTL_OK) return status;
    *out = result;
    return doe_mtl_fail(DOE_MTL_OK, "external texture import", error);
}
