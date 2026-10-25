/* SPDX-License-Identifier: MIT */
#include "surface.h"
#include "metal_frontend.h"
#include <stdlib.h>

static const WGPUTextureUsage usages = WGPUTextureUsage_RenderAttachment |
    WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;

WGPUStatus native_metal_surface_capabilities(native_surface *surface, native_adapter *adapter,
    WGPUSurfaceCapabilities *out)
{
    if (!adapter || !out || out->nextInChain || adapter->backend.kind != WGPUBackendType_Metal)
        return WGPUStatus_Error;
    if (doe_mutex_lock(&adapter->mutex).status != DOE_HOST_OK) abort();
    bool supported = doe_mtl_surface_supported(adapter->backend.value.metal.context, surface->source.display);
    if (doe_mutex_unlock(&adapter->mutex).status != DOE_HOST_OK) abort();
    if (!supported) return WGPUStatus_Error;
    WGPUTextureFormat *formats = calloc(2, sizeof(*formats));
    WGPUPresentMode *modes = calloc(2, sizeof(*modes));
    WGPUCompositeAlphaMode *alpha = calloc(2, sizeof(*alpha));
    if (!formats || !modes || !alpha) { free(formats); free(modes); free(alpha); return WGPUStatus_Error; }
    formats[0] = WGPUTextureFormat_BGRA8Unorm;
    formats[1] = WGPUTextureFormat_BGRA8UnormSrgb;
    modes[0] = WGPUPresentMode_Fifo;
    modes[1] = WGPUPresentMode_Immediate;
    alpha[0] = WGPUCompositeAlphaMode_Opaque;
    alpha[1] = WGPUCompositeAlphaMode_Premultiplied;
    *out = (WGPUSurfaceCapabilities){.usages=usages,.formatCount=2,.formats=formats,
        .presentModeCount=2,.presentModes=modes,.alphaModeCount=2,.alphaModes=alpha};
    return WGPUStatus_Success;
}

static bool configuration(const WGPUSurfaceConfiguration *config, native_device *device,
    doe_mtl_surface_desc *out)
{
    if (config->nextInChain || device->backend.kind != WGPUBackendType_Metal ||
        !config->width || !config->height || config->width > device->backend.limits.maxTextureDimension2D ||
        config->height > device->backend.limits.maxTextureDimension2D || !config->usage || (config->usage & ~usages) ||
        (config->viewFormatCount && !config->viewFormats)) return false;
    for (size_t i = 0; i < config->viewFormatCount; ++i)
        if (config->viewFormats[i] != config->format) return false;
    doe_mtl_surface_desc desc = {.width=config->width,.height=config->height,
        .usage=DOE_MTL_TEXTURE_RENDER_TARGET,.synchronized=true,.opaque=true};
    if (config->format == WGPUTextureFormat_BGRA8Unorm) desc.format = DOE_MTL_BGRA8_UNORM;
    else if (config->format == WGPUTextureFormat_BGRA8UnormSrgb) desc.format = DOE_MTL_BGRA8_SRGB;
    else return false;
    if (config->presentMode == WGPUPresentMode_Immediate) desc.synchronized = false;
    else if (config->presentMode != WGPUPresentMode_Fifo && config->presentMode != WGPUPresentMode_Undefined) return false;
    if (config->alphaMode == WGPUCompositeAlphaMode_Premultiplied) desc.opaque = false;
    else if (config->alphaMode != WGPUCompositeAlphaMode_Auto && config->alphaMode != WGPUCompositeAlphaMode_Opaque)
        return false;
    if (config->usage & WGPUTextureUsage_CopySrc) desc.usage |= DOE_MTL_TEXTURE_COPY_SRC;
    if (config->usage & WGPUTextureUsage_CopyDst) desc.usage |= DOE_MTL_TEXTURE_COPY_DST;
    if (config->usage & WGPUTextureUsage_TextureBinding) desc.usage |= DOE_MTL_TEXTURE_SAMPLED;
    *out = desc;
    return true;
}

native_pending *native_metal_surface_unconfigure_locked(native_surface *surface)
{
    surface->configured = false;
    if (!surface->metal) return NULL;
    native_device *device = surface->device;
    native_lock(device);
    native_surface_drop_current(surface);
    native_drain_pending(device);
    doe_mtl_surface_release(surface->metal);
    native_pending *reservation = surface->retirement;
    surface->metal = NULL;
    surface->device = NULL;
    surface->retirement = NULL;
    native_unlock(device);
    return reservation; /* Configuration device lease drops outside both locks. */
}

void native_metal_surface_configure(native_surface *surface, const WGPUSurfaceConfiguration *config)
{
    if (doe_mutex_lock(&surface->mutex).status != DOE_HOST_OK) abort();
    native_device *device = config->device ? (native_device *)config->device : surface->device;
    if (!device) { if (doe_mutex_unlock(&surface->mutex).status != DOE_HOST_OK) abort(); return; }
    native_device_retain(device);
    doe_mtl_surface_desc desc;
    native_failure failure = native_failure_status(config->device && configuration(config, device, &desc) ?
        NATIVE_OK : NATIVE_INVALID_ARGUMENT);
    native_pending *reservation = NULL, *retired = NULL;
    doe_mtl_surface *metal = NULL;
    native_lock(device);
    if (failure.status == NATIVE_OK) failure = native_failure_status(native_device_result(device));
    if (failure.status == NATIVE_OK) failure = native_settle_pending(device).failure;
    if (failure.status == NATIVE_OK) {
        reservation = calloc(1, sizeof(*reservation));
        if (!reservation) failure = native_failure_status(NATIVE_OUT_OF_MEMORY);
    }
    if (failure.status == NATIVE_OK) {
        doe_mtl_error detail = {0};
        failure = native_failure_metal(doe_mtl_surface_create(device->backend.value.metal.context,
            surface->source.display, &desc, &metal, &detail), &detail);
    }
    native_unlock(device);
    if (failure.status == NATIVE_OK) {
        retired = native_metal_surface_unconfigure_locked(surface);
        *reservation = (native_pending){.device=device,.surface=surface};
        surface->retirement = reservation;
        surface->metal = metal;
        surface->device = device;
        surface->configured = true;
        surface->format = config->format;
        surface->usage = config->usage;
        surface->width = config->width;
        surface->height = config->height;
        surface->presented = NATIVE_OK;
    } else free(reservation);
    if (doe_mutex_unlock(&surface->mutex).status != DOE_HOST_OK) abort();
    native_surface_retirement_release(retired);
    if (failure.status == NATIVE_OK) return;
    char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
    native_failure_format(&failure, "Metal surface configuration failed", message);
    native_error(device, native_result_error(failure.status), message);
    native_device_release(device);
}

static void release_frame(void *userdata, void *handle)
{
    native_texture *texture = handle;
    native_surface *surface = texture->surface;
    native_texture_release(userdata, handle);
    native_surface_release_lease(surface);
}

static native_failure publish(native_surface *surface, doe_mtl_texture *metal)
{
    native_device *device = surface->device;
    native_texture *payload = calloc(1, sizeof(*payload));
    if (!payload) { doe_mtl_texture_release(metal); return native_failure_status(NATIVE_OUT_OF_MEMORY); }
    payload->device = device;
    payload->metal = metal;
    native_failure failure = native_metal_initialize_texture(device, payload, doe_mtl_texture_description(metal));
    if (failure.status != NATIVE_OK) { native_texture_release(NULL, payload); return failure; }
    payload->surface = surface;
    native_retain_count(&surface->leases);
    const WGPULimits *limits = &device->backend.limits;
    doe_resource_texture_limits bounds = {limits->maxTextureDimension1D,limits->maxTextureDimension2D,
        limits->maxTextureDimension3D,limits->maxTextureArrayLayers};
    doe_resource_texture_desc desc = {.dimension=WGPUTextureDimension_2D,.format=surface->format,
        .width=surface->width,.height=surface->height,.depth_or_layers=1,.mip_levels=1,.samples=1,.usage=surface->usage};
    doe_resource_status status = doe_resource_texture_create(&device->resources, &desc, &bounds,
        (doe_resource_native){payload,NULL,release_frame}, &surface->current);
    if (status != DOE_RESOURCE_OK) {
        release_frame(NULL, payload);
        return native_failure_status(status == DOE_RESOURCE_OUT_OF_MEMORY ? NATIVE_OUT_OF_MEMORY : NATIVE_INVALID_STATE);
    }
    return failure;
}

void native_metal_surface_acquire(native_surface *surface, WGPUSurfaceTexture *out)
{
    if (doe_mutex_lock(&surface->mutex).status != DOE_HOST_OK) abort();
    if (!surface->configured) { if (doe_mutex_unlock(&surface->mutex).status != DOE_HOST_OK) abort(); return; }
    native_device *device = surface->device;
    native_device_retain(device);
    native_lock(device);
    native_failure failure = native_failure_status(native_device_result(device));
    if (failure.status == NATIVE_OK && !surface->current) {
        failure = native_settle_pending(device).failure;
        doe_mtl_texture *texture = NULL;
        if (failure.status == NATIVE_OK) {
            doe_mtl_error detail = {0};
            failure = native_failure_metal(doe_mtl_surface_acquire(surface->metal, &texture, &detail), &detail);
        }
        if (texture) failure = publish(surface, texture);
    }
    if (failure.status == NATIVE_OK && surface->current) {
        doe_resource_texture_retain(surface->current);
        out->texture = (WGPUTexture)surface->current;
        out->status = WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal;
    } else if (failure.status == NATIVE_PENDING) out->status = WGPUSurfaceGetCurrentTextureStatus_Timeout;
    native_unlock(device);
    if (doe_mutex_unlock(&surface->mutex).status != DOE_HOST_OK) abort();
    if (failure.status != NATIVE_OK && failure.status != NATIVE_PENDING) {
        char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
        native_failure_format(&failure, "Metal drawable acquisition failed", message);
        native_error(device, native_result_error(failure.status), message);
    }
    native_device_release(device);
}

WGPUStatus native_metal_surface_present(native_surface *surface)
{
    if (doe_mutex_lock(&surface->mutex).status != DOE_HOST_OK) abort();
    if (!surface->configured || !surface->current) {
        if (doe_mutex_unlock(&surface->mutex).status != DOE_HOST_OK) abort();
        return WGPUStatus_Error;
    }
    native_device *device = surface->device;
    native_device_retain(device);
    native_lock(device);
    native_failure failure = native_failure_status(native_device_result(device));
    if (failure.status == NATIVE_OK) failure = native_settle_pending(device).failure;
    native_command_handle commands = {0};
    if (failure.status == NATIVE_OK) failure = native_command_create(&device->backend, &commands);
    if (failure.status == NATIVE_OK) {
        native_texture *texture = doe_resource_texture_native(surface->current);
        doe_mtl_error detail = {0};
        failure = native_failure_metal(doe_mtl_commands_present(commands.value.metal, texture->metal, &detail), &detail);
        if (failure.status == NATIVE_OK) failure = native_submit_backend_commands(device, commands);
        else if (native_command_release(&commands) != NATIVE_OK) abort();
    }
    native_surface_drop_current(surface);
    native_unlock(device);
    if (doe_mutex_unlock(&surface->mutex).status != DOE_HOST_OK) abort();
    if (failure.status != NATIVE_OK) {
        char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
        native_failure_format(&failure, "Metal drawable presentation failed or remains unresolved", message);
        native_error(device, native_result_error(failure.status), message);
    }
    native_device_release(device);
    return failure.status == NATIVE_OK ? WGPUStatus_Success : WGPUStatus_Error;
}
