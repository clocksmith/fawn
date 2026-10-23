/* SPDX-License-Identifier: MIT
 * Frame acquisition and presentation. One frame is in flight per surface; the
 * public frame texture aliases the resource owner over the backend's borrowed
 * swapchain texture. Foreign error callbacks run only after both mutexes drop. */
#include "surface.h"
#include <stdlib.h>

static void release_frame(void *userdata, void *handle)
{
    native_surface_frame *frame = handle;
    native_surface *surface = frame->surface;
    native_texture_release(userdata, &frame->texture); /* Frees the frame (first member). */
    native_surface_release_lease(surface);
}

/* Caller holds both mutexes. Zero-initializes the acquired image through the
 * canonical private submission, then publishes a resource-owned texture. */
static doe_vk_status publish_frame(native_surface *surface, doe_vk_texture *frame)
{
    native_device *device = surface->device;
    doe_vk_texture_desc native = {.dimension=DOE_VK_TEXTURE_2D,.width=surface->width,
        .height=surface->height,.depth_or_layers=1,.mip_levels=1,.sample_count=1,
        .usage=DOE_VK_TEXTURE_COLOR_ATTACHMENT}; /* Attachment clear, never a CPU image. */
    /* Attachment-only frames take the clear path; these upload facts are unused. */
    const doe_vk_format_caps facts = {.texel_bytes=4,.aspects=1};
    doe_vk_status status = native_initialize_texture(device, frame, &native, &facts);
    if (status != DOE_VK_OK) return status;
    native_surface_frame *payload = calloc(1, sizeof(*payload));
    if (!payload) return DOE_VK_OUT_OF_MEMORY;
    status = doe_vk_texture_retain(frame);
    if (status != DOE_VK_OK) {
        free(payload);
        return status;
    }
    payload->texture = (native_texture){device, frame};
    payload->surface = surface;
    native_retain_count(&surface->leases);
    const doe_vk_caps *caps = &device->backend.value.vulkan.caps;
    doe_resource_texture_limits limits = {caps->max_texture_dimension_1d, caps->max_texture_dimension_2d,
        caps->max_texture_dimension_3d, caps->max_texture_array_layers};
    doe_resource_texture_desc desc = {.dimension=WGPUTextureDimension_2D,.format=surface->format,
        .width=surface->width,.height=surface->height,.depth_or_layers=1,.mip_levels=1,.samples=1,
        .usage=surface->usage};
    doe_resource_texture *texture = NULL;
    doe_resource_status adopted = doe_resource_texture_create(&device->resources, &desc, &limits,
        (doe_resource_native){payload, NULL, release_frame}, &texture);
    if (adopted != DOE_RESOURCE_OK) {
        release_frame(NULL, payload);
        return adopted == DOE_RESOURCE_OUT_OF_MEMORY ? DOE_VK_OUT_OF_MEMORY : DOE_VK_INVALID_STATE;
    }
    surface->current = texture;
    return DOE_VK_OK;
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
    if (pthread_mutex_lock(&surface->mutex)) abort();
    native_device *device = surface->device;
    if (!surface->configured) {
        if (pthread_mutex_unlock(&surface->mutex)) abort();
        return; /* Unconfigured: Error and NULL, no device to report to. */
    }
    if (surface->presented != NATIVE_OK) {
        out->status = surface->presented == NATIVE_SURFACE_LOST ? WGPUSurfaceGetCurrentTextureStatus_Lost :
            WGPUSurfaceGetCurrentTextureStatus_Outdated;
        if (pthread_mutex_unlock(&surface->mutex)) abort();
        return;
    }
    if (surface->current) { /* Same frame until presented, with a new reference. */
        doe_resource_texture_retain(surface->current);
        out->texture = (WGPUTexture)surface->current;
        out->status = surface->current_status;
        if (pthread_mutex_unlock(&surface->mutex)) abort();
        return;
    }
    native_device_retain(device);
    native_lock(device);
    doe_vk_status status = native_device_status(device);
    if (status == DOE_VK_OK) status = native_to_vulkan(native_settle_pending(device).status);
    /* Finish a presentation whose retirement previously timed out; never start
     * or discard an acquisition here (a timed-out acquire is waited by acquire). */
    if (status == DOE_VK_OK) status = doe_vk_surface_wait(surface->vulkan, NATIVE_SURFACE_TIMEOUT_NS, NULL);
    if (status == DOE_VK_OK) status = native_to_vulkan(native_settle_pending(device).status);
    doe_vk_texture *frame = NULL;
    bool suboptimal = false;
    if (status == DOE_VK_OK)
        status = doe_vk_surface_acquire(surface->vulkan, NATIVE_SURFACE_TIMEOUT_NS, &frame, &suboptimal, NULL);
    if (frame) {
        doe_vk_status published = publish_frame(surface, frame);
        /* Unpublished acquisitions are released with maintenance1, not presented. */
        if (published != DOE_VK_OK) (void)doe_vk_surface_retire_frame(surface->vulkan, NATIVE_SURFACE_TIMEOUT_NS, NULL);
        status = published;
    }
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
    if (pthread_mutex_unlock(&surface->mutex)) abort();
    if (message) native_error(device, type, message);
    native_device_release(device);
}

WGPUStatus wgpuSurfacePresent(WGPUSurface handle)
{
    native_surface *surface = (native_surface *)handle;
    if (!surface) return WGPUStatus_Error;
    if (pthread_mutex_lock(&surface->mutex)) abort();
    if (!surface->configured || !surface->current) {
        if (pthread_mutex_unlock(&surface->mutex)) abort();
        return WGPUStatus_Error; /* No current texture. */
    }
    native_device *device = surface->device;
    native_device_retain(device);
    native_lock(device);
    native_presentation result = {native_device_status(device), DOE_VK_INVALID_STATE};
    if (result.submission == DOE_VK_OK) result.submission = native_to_vulkan(native_settle_pending(device).status);
    doe_vk_commands *commands = NULL;
    if (result.submission == DOE_VK_OK)
        result.submission = doe_vk_commands_create(device->backend.value.vulkan.context, &commands, NULL);
    bool submitted = false;
    if (result.submission == DOE_VK_OK) {
        result.submission = doe_vk_surface_prepare_present(surface->vulkan, commands, NULL);
        if (result.submission == DOE_VK_OK) {
            result = native_submit_present(device, commands, surface->vulkan); /* Retires the frame. */
            submitted = true;
        } else if (doe_vk_commands_release(commands) != DOE_VK_OK) abort();
    }
    /* Never enqueued: release the acquired image through maintenance1 discard. */
    if (!submitted) (void)doe_vk_surface_retire_frame(surface->vulkan, NATIVE_SURFACE_TIMEOUT_NS, NULL);
    /* The public frame is consumed either way: presented, or discarded. */
    native_surface_drop_current(surface);
    if (result.presentation == DOE_VK_SURFACE_OUTDATED || result.presentation == DOE_VK_SURFACE_LOST)
        surface->presented = native_from_vulkan(result.presentation);
    native_unlock(device);
    if (pthread_mutex_unlock(&surface->mutex)) abort();
    WGPUStatus outcome = WGPUStatus_Success;
    if (result.submission != DOE_VK_OK) {
        native_error(device, native_backend_error(result.submission), "surface present submission failed; unresolved work stays retained");
        outcome = WGPUStatus_Error;
    } else if (result.presentation != DOE_VK_OK && result.presentation != DOE_VK_SURFACE_OUTDATED &&
        result.presentation != DOE_VK_SURFACE_LOST && result.presentation != DOE_VK_TIMEOUT) {
        native_error(device, native_backend_error(result.presentation), "surface presentation failed");
        outcome = WGPUStatus_Error;
    }
    native_device_release(device);
    return outcome;
}
