/* SPDX-License-Identifier: MIT
 * Frame acquisition and presentation. One frame is in flight per surface; the
 * public frame texture aliases the resource owner over the backend's borrowed
 * swapchain texture. Foreign error callbacks run only after both mutexes drop. */
#include "surface.h"
#include <stdlib.h>

static void release_frame(void *userdata, void *handle)
{
    native_surface_frame *frame = handle;
    native_surface *surface = frame->texture.surface;
    native_texture_release(userdata, &frame->texture); /* Frees the frame (first member). */
    native_surface_release_lease(surface);
}

/* Caller holds both mutexes. Zero-initializes the acquired image through the
 * canonical private submission, then publishes a resource-owned texture. */
static native_failure publish_frame(native_surface *surface, doe_vk_texture *frame)
{
    native_device *device = surface->device;
    doe_vk_texture_desc native = {.dimension=DOE_VK_TEXTURE_2D,.width=surface->width,
        .height=surface->height,.depth_or_layers=1,.mip_levels=1,.sample_count=1,
        .usage=DOE_VK_TEXTURE_COLOR_ATTACHMENT}; /* Attachment clear, never a CPU image. */
    /* Attachment-only frames take the clear path; these upload facts are unused. */
    const doe_vk_format_caps facts = {.texel_bytes=4,.aspects=1};
    native_failure failure = native_initialize_texture(device, frame, &native, &facts);
    if (failure.status != NATIVE_OK) return failure;
    native_surface_frame *payload = calloc(1, sizeof(*payload));
    if (!payload) return native_failure_status(NATIVE_OUT_OF_MEMORY);
    doe_vk_status status = doe_vk_texture_retain(frame);
    if (status != DOE_VK_OK) {
        free(payload);
        return native_failure_status(native_from_vulkan(status));
    }
    payload->texture = (native_texture){.device=device,.texture=frame,.surface=surface};
    native_retain_count(&surface->leases);
    const WGPULimits *device_limits = &device->backend.limits;
    doe_resource_texture_limits limits = {device_limits->maxTextureDimension1D, device_limits->maxTextureDimension2D,
        device_limits->maxTextureDimension3D, device_limits->maxTextureArrayLayers};
    doe_resource_texture_desc desc = {.dimension=WGPUTextureDimension_2D,.format=surface->format,
        .width=surface->width,.height=surface->height,.depth_or_layers=1,.mip_levels=1,.samples=1,
        .usage=surface->usage};
    doe_resource_texture *texture = NULL;
    doe_resource_status adopted = doe_resource_texture_create(&device->resources, &desc, &limits,
        (doe_resource_native){payload, NULL, release_frame}, &texture);
    if (adopted != DOE_RESOURCE_OK) {
        release_frame(NULL, payload);
        return native_failure_status(adopted == DOE_RESOURCE_OUT_OF_MEMORY ? NATIVE_OUT_OF_MEMORY : NATIVE_INVALID_STATE);
    }
    surface->current = texture;
    return native_failure_status(NATIVE_OK);
}

static WGPUSurfaceGetCurrentTextureStatus acquisition_status(doe_vk_status status, bool suboptimal,
    WGPUErrorType *type, const char **message)
{
    switch (status) {
    case DOE_VK_OK:
        return suboptimal ? WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal :
            WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal;
    case DOE_VK_TIMEOUT:
        return WGPUSurfaceGetCurrentTextureStatus_Timeout; /* Pending acquire retained. */
    case DOE_VK_SURFACE_OUTDATED: return WGPUSurfaceGetCurrentTextureStatus_Outdated;
    case DOE_VK_SURFACE_LOST: return WGPUSurfaceGetCurrentTextureStatus_Lost;
    case DOE_VK_DEVICE_LOST: return WGPUSurfaceGetCurrentTextureStatus_Error; /* Loss reports itself. */
    case DOE_VK_BUSY:
        *type = WGPUErrorType_Validation;
        *message = "previous surface frame texture or views are still referenced";
        return WGPUSurfaceGetCurrentTextureStatus_Error;
    case DOE_VK_PENDING:
    case DOE_VK_INVALID_ARGUMENT:
    case DOE_VK_OUT_OF_MEMORY:
    case DOE_VK_LOADER_UNAVAILABLE:
    case DOE_VK_UNSUPPORTED:
    case DOE_VK_NATIVE_FAILURE:
    case DOE_VK_INVALID_STATE:
        break;
    }
    *type = native_backend_error(status);
    *message = "surface frame acquisition or zero initialization failed";
    return WGPUSurfaceGetCurrentTextureStatus_Error;
}

void wgpuSurfaceGetCurrentTexture(WGPUSurface handle, WGPUSurfaceTexture *out)
{
    native_surface *surface = (native_surface *)handle;
    if (!out) return;
    out->texture = NULL;
    out->status = WGPUSurfaceGetCurrentTextureStatus_Error;
    if (!surface || out->nextInChain) return;
    if (surface->source.platform == NATIVE_SURFACE_WINDOWS_HWND) {
        native_d3d12_surface_acquire(surface, out);
        return;
    }
    if (surface->source.platform == NATIVE_SURFACE_METAL_LAYER) {
        native_metal_surface_acquire(surface, out);
        return;
    }
    if (doe_mutex_lock(&surface->mutex).status != DOE_HOST_OK) abort();
    native_device *device = surface->device;
    if (!surface->configured) {
        if (doe_mutex_unlock(&surface->mutex).status != DOE_HOST_OK) abort();
        return; /* Unconfigured: Error and NULL, no device to report to. */
    }
    if (surface->presented != NATIVE_OK) {
        out->status = surface->presented == NATIVE_SURFACE_LOST ? WGPUSurfaceGetCurrentTextureStatus_Lost :
            WGPUSurfaceGetCurrentTextureStatus_Outdated;
        if (doe_mutex_unlock(&surface->mutex).status != DOE_HOST_OK) abort();
        return;
    }
    if (surface->current) { /* Same frame until presented, with a new reference. */
        doe_resource_texture_retain(surface->current);
        out->texture = (WGPUTexture)surface->current;
        out->status = surface->current_status;
        if (doe_mutex_unlock(&surface->mutex).status != DOE_HOST_OK) abort();
        return;
    }
    native_device_retain(device);
    native_lock(device);
    native_failure failure = native_failure_status(native_device_result(device));
    /* Finish a presentation whose retirement previously timed out; never start
     * or discard an acquisition here (a timed-out acquire is waited by acquire). */
    doe_vk_error detail = {0};
    if (failure.status == NATIVE_OK) {
        doe_vk_status status = doe_vk_surface_wait(surface->vulkan, NATIVE_SURFACE_TIMEOUT_NS, &detail);
        failure = native_failure_vulkan(status, &detail);
    }
    if (failure.status == NATIVE_OK) failure = native_settle_pending(device).failure;
    doe_vk_texture *frame = NULL;
    bool suboptimal = false;
    if (failure.status == NATIVE_OK) {
        doe_vk_status status = doe_vk_surface_acquire(surface->vulkan, NATIVE_SURFACE_TIMEOUT_NS, &frame, &suboptimal, &detail);
        failure = native_failure_vulkan(status, &detail);
    }
    if (frame) {
        native_failure published = publish_frame(surface, frame);
        /* Unpublished acquisitions are released with maintenance1, not presented.
         * Cleanup cannot replace the original initialization failure snapshot. */
        if (published.status != NATIVE_OK)
            (void)doe_vk_surface_retire_frame(surface->vulkan, NATIVE_SURFACE_TIMEOUT_NS, NULL);
        failure = published;
    }
    doe_vk_status status = native_to_vulkan(failure.status);
    WGPUErrorType type = WGPUErrorType_NoError;
    const char *message = NULL;
    out->status = acquisition_status(status, suboptimal, &type, &message);
    if (status == DOE_VK_SURFACE_OUTDATED || status == DOE_VK_SURFACE_LOST) surface->presented = native_from_vulkan(status);
    if (surface->current) {
        surface->current_status = out->status;
        doe_resource_texture_retain(surface->current); /* Returned with ownership. */
        out->texture = (WGPUTexture)surface->current;
    }
    native_unlock(device);
    if (doe_mutex_unlock(&surface->mutex).status != DOE_HOST_OK) abort();
    if (message) {
        char formatted[NATIVE_FAILURE_MESSAGE_CAPACITY];
        native_failure_format(&failure, message, formatted);
        native_error(device, type, formatted);
    }
    native_device_release(device);
}

WGPUStatus wgpuSurfacePresent(WGPUSurface handle)
{
    native_surface *surface = (native_surface *)handle;
    if (!surface) return WGPUStatus_Error;
    if (surface->source.platform == NATIVE_SURFACE_WINDOWS_HWND)
        return native_d3d12_surface_present(surface);
    if (surface->source.platform == NATIVE_SURFACE_METAL_LAYER)
        return native_metal_surface_present(surface);
    if (doe_mutex_lock(&surface->mutex).status != DOE_HOST_OK) abort();
    if (!surface->configured || !surface->current) {
        if (doe_mutex_unlock(&surface->mutex).status != DOE_HOST_OK) abort();
        return WGPUStatus_Error; /* No current texture. */
    }
    native_device *device = surface->device;
    native_device_retain(device);
    native_lock(device);
    native_presentation result = {.submission = native_failure_status(native_device_result(device)),
        .presentation = {.status = NATIVE_INVALID_STATE}};
    if (result.submission.status == NATIVE_OK) result.submission = native_flush_initialization(device);
    if (result.submission.status == NATIVE_OK) result.submission = native_settle_pending(device).failure;
    doe_vk_commands *commands = NULL;
    doe_vk_error detail = {0};
    if (result.submission.status == NATIVE_OK) {
        doe_vk_status status = doe_vk_commands_create(device->backend.value.vulkan.context, &commands, &detail);
        result.submission = native_failure_vulkan(status, &detail);
    }
    bool submitted = false;
    if (result.submission.status == NATIVE_OK) {
        doe_vk_status status = doe_vk_surface_prepare_present(surface->vulkan, commands, &detail);
        result.submission = native_failure_vulkan(status, &detail);
        if (result.submission.status == NATIVE_OK) {
            result = native_submit_present(device, commands, surface->vulkan); /* Retires the frame. */
            submitted = true;
        } else if (doe_vk_commands_release(commands) != DOE_VK_OK) abort();
    }
    /* Never enqueued: release the acquired image through maintenance1 discard. */
    if (!submitted) (void)doe_vk_surface_retire_frame(surface->vulkan, NATIVE_SURFACE_TIMEOUT_NS, NULL);
    /* The public frame is consumed either way: presented, or discarded. */
    native_surface_drop_current(surface);
    if (result.presentation.status == NATIVE_SURFACE_OUTDATED || result.presentation.status == NATIVE_SURFACE_LOST)
        surface->presented = result.presentation.status;
    native_unlock(device);
    if (doe_mutex_unlock(&surface->mutex).status != DOE_HOST_OK) abort();
    WGPUStatus outcome = WGPUStatus_Success;
    char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
    if (result.submission.status != NATIVE_OK) {
        native_failure_format(&result.submission, "surface present submission failed; unresolved work stays retained", message);
        native_error(device, native_result_error(result.submission.status), message);
        outcome = WGPUStatus_Error;
    } else if (result.presentation.status != NATIVE_OK && result.presentation.status != NATIVE_SURFACE_OUTDATED &&
        result.presentation.status != NATIVE_SURFACE_LOST && result.presentation.status != NATIVE_TIMEOUT) {
        native_failure_format(&result.presentation, "surface presentation failed", message);
        native_error(device, native_result_error(result.presentation.status), message);
        outcome = WGPUStatus_Error;
    }
    native_device_release(device);
    return outcome;
}
