/* SPDX-License-Identifier: MIT */
#include "objc_abi.h"
#include <time.h>

bool doe_mtl_objc_surface_supported(void *userdata, void *layer) {
    doe_mtl_objc *runtime = userdata;
    const char *methods[] = {"setDevice:", "setPixelFormat:", "setDrawableSize:",
        "setFramebufferOnly:", "setAllowsNextDrawableTimeout:", "setDisplaySyncEnabled:",
        "setOpaque:", "nextDrawable"};
    if (!layer) return false;
    for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i)
        if (!runtime->responds_call(layer, runtime->responds, runtime->selector(methods[i]))) return false;
    return true;
}

doe_mtl_status doe_mtl_objc_surface_configure(void *userdata, void *device, void *layer,
    const doe_mtl_surface_desc *desc, void **out, doe_mtl_error *error) {
    doe_mtl_objc *runtime = userdata;
    if (!doe_mtl_objc_surface_supported(userdata, layer))
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "CAMetalLayer selectors", error);
    void *pool = doe_mtl_objc_pool(runtime);
    if (!pool) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    runtime->set_object_call(layer, runtime->selector("setDevice:"), device);
    runtime->set_size_call(layer, runtime->selector("setPixelFormat:"), (size_t)desc->format);
    runtime->set_drawable_size_call(layer, runtime->selector("setDrawableSize:"),
        (doe_mtl_drawable_size){(double)desc->width, (double)desc->height});
    runtime->set_bool_call(layer, runtime->selector("setFramebufferOnly:"),
        (doe_objc_bool)(desc->usage == DOE_MTL_TEXTURE_RENDER_TARGET));
    runtime->set_bool_call(layer, runtime->selector("setAllowsNextDrawableTimeout:"), (doe_objc_bool)true);
    runtime->set_bool_call(layer, runtime->selector("setDisplaySyncEnabled:"), (doe_objc_bool)desc->synchronized);
    runtime->set_bool_call(layer, runtime->selector("setOpaque:"), (doe_objc_bool)desc->opaque);
    *out = doe_mtl_objc_object(runtime, layer, runtime->retain);
    doe_mtl_objc_drain(runtime, pool);
    return doe_mtl_fail(*out ? DOE_MTL_OK : DOE_MTL_NATIVE_FAILURE, "CAMetalLayer configuration", error);
}

doe_mtl_status doe_mtl_objc_surface_acquire(void *userdata, void *layer, const doe_mtl_surface_desc *desc,
    void **drawable, void **texture, doe_mtl_error *error) {
    doe_mtl_objc *runtime = userdata;
    void *pool = doe_mtl_objc_pool(runtime);
    if (!pool) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    void *next = doe_mtl_objc_object(runtime, layer, runtime->selector("nextDrawable"));
    void *image = next ? doe_mtl_objc_object(runtime, next, runtime->selector("texture")) : NULL;
    if (image && (runtime->size_call(image, runtime->selector("width")) != desc->width ||
        runtime->size_call(image, runtime->selector("height")) != desc->height ||
        runtime->size_call(image, runtime->selector("pixelFormat")) != (size_t)desc->format)) {
        doe_mtl_objc_drain(runtime, pool);
        return doe_mtl_fail(DOE_MTL_INVALID_STATE, "CAMetalDrawable configuration changed", error);
    }
    if (image) {
        *drawable = doe_mtl_objc_object(runtime, next, runtime->retain);
        *texture = doe_mtl_objc_object(runtime, image, runtime->retain);
    }
    doe_mtl_objc_drain(runtime, pool);
    return doe_mtl_fail(image ? DOE_MTL_OK : next ? DOE_MTL_NATIVE_FAILURE : DOE_MTL_PENDING,
        "CAMetalLayer nextDrawable", error);
}

doe_mtl_status doe_mtl_objc_surface_present(void *userdata, void *command,
    void *drawable, doe_mtl_error *error) {
    doe_mtl_objc *runtime = userdata;
    void *pool = doe_mtl_objc_pool(runtime);
    if (!pool) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    runtime->set_object_call(command, runtime->selector("presentDrawable:"), drawable);
    doe_mtl_objc_drain(runtime, pool);
    return doe_mtl_fail(DOE_MTL_OK, "MTLCommandBuffer presentDrawable", error);
}

size_t doe_mtl_objc_surface_observe(void *userdata, void *command,
    void *drawable, bool wait, doe_mtl_error *error) {
    (void)drawable; /* The command owns its +1 lease until real GPU completion. */
    struct timespec started, now;
    if (timespec_get(&started, TIME_UTC) != TIME_UTC) return SIZE_MAX;
    for (;;) {
        size_t status = doe_mtl_objc_observe(userdata, command, false, error);
        if (status == 4 || status == 5 || status == SIZE_MAX) return status;
        /* presentDrawable registers the native presentation through a scheduled
         * handler. Apple permits releasing the drawable after registration; we
         * conservatively keep it until GPU completion. presentedTime may remain
         * zero for a skipped frame, so it is never a resource-retirement fence.
         * See Apple Metal Best Practices Guide, Drawables. */
        if (!wait) return 3;
        if (timespec_get(&now, TIME_UTC) != TIME_UTC || now.tv_sec < started.tv_sec ||
            now.tv_sec - started.tv_sec >= 5) return 3;
#if defined(__APPLE__)
        const struct timespec pause = {0, 1000000};
        (void)nanosleep(&pause, NULL);
#endif
    }
}
