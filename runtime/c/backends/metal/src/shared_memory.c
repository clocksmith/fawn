/* SPDX-License-Identifier: MIT */
#include "texture_internal.h"
#include "doe_metal_shared.h"
#include <stdlib.h>

doe_mtl_status doe_mtl_shared_surface_open_info(doe_mtl_context *context, void *surface,
    void **retained, doe_mtl_shared_surface_info *info, doe_mtl_error *error)
{
    if (!context || !surface || !retained || !info)
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "shared IOSurface import", error);
    if (!context->native.shared_surface || !context->native.shared_surface_release)
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "shared IOSurface native boundary", error);
    void *reference = NULL;
    doe_mtl_shared_surface_info result = {0};
    doe_mtl_status status = context->native.shared_surface(context->native.userdata,
        surface, &reference, &result.width, &result.height, &result.format, error);
    if (status != DOE_MTL_OK) return status;
    if (!reference || !result.width || !result.height ||
        (result.format != DOE_MTL_RGBA8_UNORM && result.format != DOE_MTL_BGRA8_UNORM)) {
        if (reference) context->native.shared_surface_release(context->native.userdata,reference);
        return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "shared IOSurface metadata", error);
    }
    *retained = reference;
    *info = result;
    return DOE_MTL_OK;
}

doe_mtl_status doe_mtl_shared_surface_open(doe_mtl_context *context, void *surface,
    void **retained, uint32_t *width, uint32_t *height, doe_mtl_error *error)
{
    if (!width || !height || !retained)
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "shared IOSurface import", error);
    void *reference = NULL;
    doe_mtl_shared_surface_info info;
    doe_mtl_status status = doe_mtl_shared_surface_open_info(context,surface,&reference,&info,error);
    if (status != DOE_MTL_OK) return status;
    *retained = reference;
    *width = info.width;
    *height = info.height;
    return DOE_MTL_OK;
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

doe_mtl_status doe_mtl_context_release_point(const doe_mtl_context *context,
    void **event, uint64_t *value)
{
    if (!context || !event || !value) return DOE_MTL_INVALID_ARGUMENT;
    if (!context->timeline) return DOE_MTL_UNSUPPORTED;
    *event = context->timeline;
    *value = context->timeline_committed;
    return DOE_MTL_OK;
}

static doe_mtl_status texture_release_preflight(const doe_mtl_texture *texture) {
    if (!texture) return DOE_MTL_INVALID_ARGUMENT;
    if (texture->poisoned) return DOE_MTL_EXECUTION_FAILED;
    for (const doe_mtl_texture_use *use = texture->uses; use; use = use->older) {
        doe_mtl_status status = doe_mtl_command_submission_status(use->commands);
        if (status != DOE_MTL_OK) return status;
        if (texture->context->timeline &&
            use->commands->timeline_value > texture->context->timeline_committed) return DOE_MTL_INVALID_STATE;
    }
    return DOE_MTL_OK;
}

doe_mtl_status doe_mtl_texture_release_initialized(const doe_mtl_texture *texture, bool *initialized) {
    if (!initialized) return DOE_MTL_INVALID_ARGUMENT;
    doe_mtl_status status = texture_release_preflight(texture);
    if (status != DOE_MTL_OK) return status;
    const uint8_t *snapshot = doe_mtl_texture_initialization(NULL, texture);
    const uint8_t aspects = doe_mtl_format_aspects(texture->desc.format);
    bool complete = snapshot != NULL;
    for (size_t i = 0; complete && i < texture->subresource_count; ++i)
        complete = (snapshot[i] & aspects) == aspects;
    *initialized = complete;
    return DOE_MTL_OK;
}

doe_mtl_status doe_mtl_texture_release_point(const doe_mtl_texture *texture,
    void **event, uint64_t *value)
{
    if (!texture || !event || !value) return DOE_MTL_INVALID_ARGUMENT;
    if (!texture->context->timeline) return DOE_MTL_UNSUPPORTED;
    doe_mtl_status status = texture_release_preflight(texture);
    if (status != DOE_MTL_OK) return status;
    if (texture->owner && !texture->owner->timeline_value) return DOE_MTL_INVALID_STATE;
    if (texture->last_submission > texture->context->timeline_committed) return DOE_MTL_INVALID_STATE;
    *event = texture->context->timeline;
    *value = texture->last_submission;
    return DOE_MTL_OK;
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

doe_mtl_status doe_mtl_commands_wait_shared_events(doe_mtl_commands *commands,
    const doe_mtl_shared_wait *waits, size_t count, doe_mtl_error *error)
{
    doe_mtl_status status = doe_mtl_recording(commands, error);
    if (status != DOE_MTL_OK) return status;
    if ((!waits && count) || count > SIZE_MAX / sizeof(doe_mtl_shared_wait) || commands->shared_wait_events)
        return doe_mtl_poison(commands, DOE_MTL_INVALID_ARGUMENT, "shared producer wait batch", error);
    doe_mtl_native *native = &commands->context->native;
    if (count && (!native->commands_wait_event || !native->shared_event_retain))
        return doe_mtl_poison(commands, DOE_MTL_UNSUPPORTED, "shared producer wait boundary", error);
    for (size_t i = 0; i < count; ++i)
        if (!waits[i].event)
            return doe_mtl_poison(commands, DOE_MTL_INVALID_ARGUMENT, "shared producer event", error);
    doe_mtl_shared_wait *retained = count ? calloc(count, sizeof(*retained)) : NULL;
    if (count && !retained)
        return doe_mtl_poison(commands, DOE_MTL_OUT_OF_MEMORY, "shared producer wait leases", error);
    doe_mtl_error detail = {0};
    size_t acquired = 0;
    while (acquired < count && status == DOE_MTL_OK) {
        status = doe_mtl_shared_event_import(commands->context, waits[acquired].event,
            &retained[acquired].event, &detail);
        if (status == DOE_MTL_OK) {
            retained[acquired].value = waits[acquired].value;
            acquired++;
        }
    }
    if (status != DOE_MTL_OK) {
        for (size_t i = 0; i < acquired; ++i) doe_mtl_shared_event_release(commands->context, retained[i].event);
        free(retained);
        return doe_mtl_native_record_result(commands, status, &detail, error);
    }
    /* Complete lease admission precedes the first native mutation. Failure
     * while encoding retains the whole admitted batch until command cleanup. */
    commands->shared_wait_events = retained;
    commands->shared_wait_count = count;
    for (size_t i = 0; i < count && status == DOE_MTL_OK; ++i)
        status = native->commands_wait_event(native->userdata, commands->native,
            retained[i].event, retained[i].value, &detail);
    return doe_mtl_native_record_result(commands, status, &detail, error);
}

doe_mtl_status doe_mtl_commands_replay_shared_waits(doe_mtl_commands *commands,
    const doe_mtl_commands *source, doe_mtl_error *error)
{
    if (!commands || !source || commands == source || commands->context != source->context ||
        source->state != DOE_MTL_RECORDING)
        return doe_mtl_poison(commands, DOE_MTL_INVALID_ARGUMENT, "shared dependency replay", error);
    return doe_mtl_commands_wait_shared_events(commands, source->shared_wait_events,
        source->shared_wait_count, error);
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
