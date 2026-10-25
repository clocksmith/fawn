/* SPDX-License-Identifier: MIT */
#include "objc_abi.h"
#if !defined(_WIN32)
#include <dlfcn.h>

static bool external_symbol(void *library, const char *name, void *out, size_t size)
{
    void *symbol = dlsym(library, name);
    if (!symbol || size != sizeof(symbol)) return false;
    memcpy(out, &symbol, size);
    return true;
}
#endif

void doe_mtl_objc_close_external(doe_mtl_objc *runtime)
{
#if !defined(_WIN32)
    if (runtime->external.corevideo_library) dlclose(runtime->external.corevideo_library);
    if (runtime->external.iosurface_library) dlclose(runtime->external.iosurface_library);
#endif
    runtime->external = (doe_mtl_objc_external){0};
}

void doe_mtl_objc_load_external(doe_mtl_objc *runtime, doe_objc_message message)
{
#if !defined(_WIN32)
    doe_mtl_objc_external *external = &runtime->external;
    external->iosurface_library = dlopen("/System/Library/Frameworks/IOSurface.framework/IOSurface", RTLD_NOW | RTLD_LOCAL);
    external->corevideo_library = dlopen("/System/Library/Frameworks/CoreVideo.framework/CoreVideo", RTLD_NOW | RTLD_LOCAL);
    if (!external->iosurface_library || !external->corevideo_library) goto unavailable;
    void *surface = external->iosurface_library, *pixel = external->corevideo_library;
    if (!external_symbol(surface, "IOSurfaceGetPlaneCount", &external->surface_planes, sizeof(external->surface_planes)) ||
        !external_symbol(surface, "IOSurfaceGetWidth", &external->surface_width, sizeof(external->surface_width)) ||
        !external_symbol(surface, "IOSurfaceGetHeight", &external->surface_height, sizeof(external->surface_height)) ||
        !external_symbol(surface, "IOSurfaceGetWidthOfPlane", &external->surface_plane_width, sizeof(external->surface_plane_width)) ||
        !external_symbol(surface, "IOSurfaceGetHeightOfPlane", &external->surface_plane_height, sizeof(external->surface_plane_height)) ||
        !external_symbol(surface, "IOSurfaceGetPixelFormat", &external->surface_format, sizeof(external->surface_format)) ||
        !external_symbol(pixel, "CVPixelBufferGetIOSurface", &external->pixel_surface, sizeof(external->pixel_surface)) ||
        !external_symbol(pixel, "CVPixelBufferGetPlaneCount", &external->pixel_planes, sizeof(external->pixel_planes)) ||
        !external_symbol(pixel, "CVPixelBufferGetWidth", &external->pixel_width, sizeof(external->pixel_width)) ||
        !external_symbol(pixel, "CVPixelBufferGetHeight", &external->pixel_height, sizeof(external->pixel_height)) ||
        !external_symbol(pixel, "CVPixelBufferGetWidthOfPlane", &external->pixel_plane_width, sizeof(external->pixel_plane_width)) ||
        !external_symbol(pixel, "CVPixelBufferGetHeightOfPlane", &external->pixel_plane_height, sizeof(external->pixel_plane_height)) ||
        !external_symbol(pixel, "CVPixelBufferGetPixelFormatType", &external->pixel_format, sizeof(external->pixel_format)))
        goto unavailable;
    external->new_texture = runtime->selector("newTextureWithDescriptor:iosurface:plane:");
    external->texture_call = (doe_objc_external_texture_call)message;
    if (external->new_texture && external->texture_call) return;
unavailable:
    doe_mtl_objc_close_external(runtime);
#else
    (void)runtime;
    (void)message;
#endif
}
