/* SPDX-License-Identifier: MIT */
#include "texture_internal.h"
#include "doe_metal_shared.h"

doe_mtl_status doe_mtl_shared_surface_open(doe_mtl_context *context, void *surface,
    void **retained, uint32_t *width, uint32_t *height, doe_mtl_error *error)
{
    if (!context || !surface || !retained || !width || !height)
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "shared IOSurface import", error);
    if (!context->native.shared_surface || !context->native.shared_surface_release)
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "shared IOSurface native boundary", error);
    return context->native.shared_surface(context->native.userdata, surface, retained, width, height, error);
}

void doe_mtl_shared_surface_release(doe_mtl_context *context, void *surface)
{
    if (context && surface) context->native.shared_surface_release(context->native.userdata, surface);
}

doe_mtl_status doe_mtl_shared_texture_import(doe_mtl_context *context, void *surface,
    const doe_mtl_texture_desc *desc, doe_mtl_texture **out, doe_mtl_error *error)
{
    if (!context || !surface || !desc || !out)
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "shared IOSurface texture", error);
    if (!context->native.shared_texture)
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "shared texture native boundary", error);
    /* The same external-plane adopter owns shape, native identity and initial
     * producer contents. No zero upload may overwrite the imported surface. */
    if (desc->format != DOE_MTL_RGBA8_UNORM && desc->format != DOE_MTL_RGBA8_SRGB &&
        desc->format != DOE_MTL_BGRA8_UNORM && desc->format != DOE_MTL_BGRA8_SRGB)
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "shared IOSurface format", error);
    void *native = NULL;
    doe_mtl_status status = context->native.shared_texture(context->native.userdata,
        context->device, surface, desc, &native, error);
    if (status != DOE_MTL_OK) return status;
    status = doe_mtl_texture_adopt_external(context, desc, native, out, error);
    if (status != DOE_MTL_OK && native) context->native.release(context->native.userdata, native);
    return status;
}

doe_mtl_status doe_mtl_shared_event_import(doe_mtl_context *context, void *event,
    void **out, doe_mtl_error *error)
{
    if (!context || !event || !out)
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "shared event import", error);
    if (!context->native.shared_event_retain)
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "shared event native boundary", error);
    return context->native.shared_event_retain(context->native.userdata, event, out, error);
}

doe_mtl_status doe_mtl_shared_event_create(doe_mtl_context *context, void **out,
    doe_mtl_error *error)
{
    if (!context || !out)
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "shared event create", error);
    if (!context->native.timeline_create)
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "shared event native boundary", error);
    return context->native.timeline_create(context->native.userdata, context->device, out, error);
}

void doe_mtl_shared_event_release(doe_mtl_context *context, void *event)
{
    if (context && event) context->native.release(context->native.userdata, event);
}

doe_mtl_status doe_mtl_shared_event_wait(doe_mtl_context *context, void *event,
    uint64_t value, doe_mtl_error *error)
{
    if (!context || !event)
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "shared event wait", error);
    if (!context->native.shared_event_wait)
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "shared event wait native boundary", error);
    return context->native.shared_event_wait(context->native.userdata, event, value, error);
}

doe_mtl_status doe_mtl_commands_signal_shared_event(doe_mtl_commands *commands,
    void *event, uint64_t value, doe_mtl_error *error)
{
    doe_mtl_status status = doe_mtl_recording(commands, error);
    if (status != DOE_MTL_OK) return status;
    if (!event || commands->shared_signal_event)
        return doe_mtl_poison(commands, DOE_MTL_INVALID_ARGUMENT, "shared completion signal", error);
    doe_mtl_native *native = &commands->context->native;
    if (!native->timeline_signal)
        return doe_mtl_poison(commands, DOE_MTL_UNSUPPORTED, "shared completion signal", error);
    doe_mtl_error detail = {0};
    status = doe_mtl_shared_event_import(commands->context, event, &commands->shared_signal_event, &detail);
    if (status == DOE_MTL_OK)
        status = native->timeline_signal(native->userdata, commands->native, event, value, &detail);
    return doe_mtl_native_record_result(commands, status, &detail, error);
}
