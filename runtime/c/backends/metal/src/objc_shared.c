/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "objc_abi.h"
#if !defined(_WIN32)
#include <errno.h>
#include <time.h>
#endif

static doe_mtl_status inspect_shared(doe_mtl_objc *runtime, void *surface,
    uint32_t *width, uint32_t *height, doe_mtl_error *error)
{
    doe_mtl_objc_external *external = &runtime->external;
    if (!external->texture_call || !external->cf_retain || !external->cf_release)
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "shared IOSurface APIs", error);
    size_t planes = external->surface_planes(surface);
    if (planes > 1) return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "shared IOSurface plane count", error);
    size_t w = planes ? external->surface_plane_width(surface, 0) : external->surface_width(surface);
    size_t h = planes ? external->surface_plane_height(surface, 0) : external->surface_height(surface);
    if (!w || !h || w > UINT32_MAX || h > UINT32_MAX)
        return doe_mtl_fail(DOE_MTL_RANGE, "shared IOSurface dimensions", error);
    *width = (uint32_t)w;
    *height = (uint32_t)h;
    return DOE_MTL_OK;
}

doe_mtl_status doe_mtl_objc_shared_surface(void *userdata, void *surface,
    void **retained, uint32_t *width, uint32_t *height, doe_mtl_error *error)
{
    doe_mtl_objc *runtime = userdata;
    uint32_t w, h;
    doe_mtl_status status = inspect_shared(runtime, surface, &w, &h, error);
    if (status != DOE_MTL_OK) return status;
    const void *reference = runtime->external.cf_retain(surface);
    if (!reference) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "CFRetain IOSurface", error);
    *retained = (void *)reference;
    *width = w;
    *height = h;
    return doe_mtl_fail(DOE_MTL_OK, "shared IOSurface import", error);
}

void doe_mtl_objc_shared_surface_release(void *userdata, void *surface)
{
    doe_mtl_objc *runtime = userdata;
    if (surface) runtime->external.cf_release(surface);
}

doe_mtl_status doe_mtl_objc_shared_texture(void *userdata, void *device,
    void *surface, const doe_mtl_texture_desc *desc, void **out, doe_mtl_error *error)
{
    doe_mtl_objc *runtime = userdata;
    uint32_t width, height;
    doe_mtl_status status = inspect_shared(runtime, surface, &width, &height, error);
    if (status != DOE_MTL_OK) return status;
    if (desc->type != DOE_MTL_TEXTURE_2D || desc->width != width || desc->height != height ||
        desc->depth_or_layers != 1 || desc->mip_levels != 1 || desc->sample_count != 1 ||
        !desc->usage || (desc->usage & ~(uint32_t)(DOE_MTL_TEXTURE_COPY_SRC | DOE_MTL_TEXTURE_COPY_DST |
            DOE_MTL_TEXTURE_RENDER_TARGET | DOE_MTL_TEXTURE_SAMPLED)))
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "shared IOSurface texture descriptor", error);
    if (!runtime->responds_call(device, runtime->responds, runtime->external.new_texture))
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "Metal IOSurface textures", error);
    void *pool = doe_mtl_objc_pool(runtime);
    if (!pool) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "shared texture autorelease pool", error);
    status = doe_mtl_objc_import_plane(runtime, device, surface, 0, desc, out, error);
    doe_mtl_objc_drain(runtime, pool);
    return status;
}

doe_mtl_status doe_mtl_objc_shared_event_retain(void *userdata, void *event,
    void **out, doe_mtl_error *error)
{
    doe_mtl_objc *runtime = userdata;
    if (!runtime->queries.event_value || !runtime->queries.signaled_value ||
        !runtime->responds_call(event, runtime->responds, runtime->queries.signaled_value))
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "MTLSharedEvent signaledValue", error);
    void *retained = doe_mtl_objc_object(runtime, event, runtime->retain);
    if (!retained) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "retain MTLSharedEvent", error);
    *out = retained;
    return doe_mtl_fail(DOE_MTL_OK, "shared event import", error);
}

doe_mtl_status doe_mtl_objc_shared_event_wait(void *userdata, void *event,
    uint64_t value, doe_mtl_error *error)
{
    doe_mtl_objc *runtime = userdata;
    if (!runtime->queries.event_value || !runtime->queries.signaled_value)
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "MTLSharedEvent signaledValue", error);
    /* Producer waits must not manufacture a queue command or report a timeout
     * as completion. The native event remains retained by the caller. Sleeping
     * polling avoids a process-global listener and works on the minimum SDK. */
    while (runtime->queries.event_value(event, runtime->queries.signaled_value) < value) {
#if !defined(_WIN32)
        struct timespec delay = {0, 1000000};
        while (nanosleep(&delay, &delay) != 0) {
            if (errno != EINTR) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "shared event host wait", error);
        }
#else
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "Metal shared event host wait", error);
#endif
    }
    return doe_mtl_fail(DOE_MTL_OK, "shared event producer completion", error);
}
