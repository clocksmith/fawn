/* SPDX-License-Identifier: MIT
 * Retained IOSurface/shared-MTL-event ABI composed with the canonical texture,
 * device, label and pending-command owners. No parallel resource queue. */
#include "internal.h"
#include "metal_frontend.h"
#include "doe_native_shared_memory.h"
#include "doe_metal_shared.h"
#include <stddef.h>
#include <stdlib.h>

#define SHARED_MEMORY_MAGIC UINT32_C(0xD0E10020)
#define SHARED_FENCE_MAGIC UINT32_C(0xD0E10021)
typedef enum shared_access { SHARED_IDLE, SHARED_WAITING, SHARED_ACTIVE } shared_access;
struct native_shared_texture_memory {
    uint32_t magic;
    atomic_uint references;
    native_device *device;
    void *surface;
    uint32_t width, height;
    WGPUTextureFormat format;
    shared_access access;
    doe_resource_texture *active_texture; /* Weak; texture finalization clears it. */
};
struct native_shared_fence {
    uint32_t magic;
    atomic_uint references;
    native_device *device;
    void *event;
};
typedef struct shared_texture_payload {
    native_texture texture; /* Canonical payload prefix, never another resource. */
    doe_resource_texture *identity;
} shared_texture_payload;
typedef struct shared_wait {
    WGPUSharedFence fence;
    uint64_t value;
} shared_wait;
static const WGPUTextureUsage shared_usage = WGPUTextureUsage_TextureBinding |
    WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
#if UINTPTR_MAX == UINT64_MAX
_Static_assert(offsetof(WGPUSharedTextureMemoryBeginAccessDescriptor, fenceCount) == 16, "retained begin ABI");
_Static_assert(offsetof(WGPUSharedTextureMemoryIOSurfaceDescriptor, ioSurface) == 16, "retained IOSurface ABI");
_Static_assert(offsetof(WGPUSharedFenceMTLSharedEventDescriptor, sharedEvent) == 16, "retained fence ABI");
_Static_assert(offsetof(WGPUSharedTextureMemoryProperties, size) == 16, "retained properties ABI");
_Static_assert(sizeof(WGPUSharedTextureMemoryBeginAccessDescriptor) == 48, "pinned begin ABI");
_Static_assert(offsetof(WGPUSharedTextureMemoryBeginAccessDescriptor, signaledValueCount) == 32, "pinned begin count ABI");
_Static_assert(offsetof(WGPUSharedTextureMemoryBeginAccessDescriptor, signaledValues) == 40, "pinned begin values ABI");
_Static_assert(sizeof(WGPUSharedTextureMemoryEndAccessState) == 48, "pinned end ABI");
_Static_assert(offsetof(WGPUSharedTextureMemoryEndAccessState, signaledValueCount) == 32, "pinned end count ABI");
_Static_assert(offsetof(WGPUSharedTextureMemoryEndAccessState, signaledValues) == 40, "pinned end values ABI");
#endif

static bool valid_memory(WGPUSharedTextureMemory memory)
{
    return memory && memory->magic == SHARED_MEMORY_MAGIC;
}
static bool valid_fence(WGPUSharedFence fence)
{
    return fence && fence->magic == SHARED_FENCE_MAGIC;
}
static doe_mtl_context *metal_context(native_device *device)
{
    return device->backend.value.metal.context;
}
static const WGPUChainedStruct *find_chain(const WGPUChainedStruct *chain, uint32_t type)
{
    /* Find the first matching retained extension, but reject a cyclic chain
     * rather than traversing caller-controlled input forever. */
    const WGPUChainedStruct *slow = chain, *fast = chain;
    while (chain) {
        if ((uint32_t)chain->sType == type) return chain;
        chain = chain->next;
        slow = slow ? slow->next : NULL;
        fast = fast && fast->next ? fast->next->next : NULL;
        if (fast && slow == fast) return NULL;
    }
    return NULL;
}
static void shared_error(native_device *device, native_failure failure, const char *operation)
{
    if (failure.status == NATIVE_OK) return;
    char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
    native_failure_format(&failure, operation, message);
    native_error(device, native_result_error(failure.status), message);
}
static WGPUSharedFence allocate_fence(native_device *device)
{
    WGPUSharedFence fence = calloc(1, sizeof(*fence));
    if (!fence) return NULL;
    fence->magic = SHARED_FENCE_MAGIC;
    atomic_init(&fence->references, 1);
    fence->device = device;
    native_device_retain(device);
    return fence;
}
void wgpuSharedFenceAddRef(WGPUSharedFence fence)
{
    if (valid_fence(fence)) native_retain_count(&fence->references);
}
void wgpuSharedFenceRelease(WGPUSharedFence fence)
{
    if (!valid_fence(fence) || !native_release_count(&fence->references)) return;
    native_device *device = fence->device;
    native_lock(device);
    if (doe_label_store_remove(device->labels, fence) != DOE_LABEL_OK) abort();
    doe_mtl_shared_event_release(metal_context(device), fence->event);
    native_unlock(device);
    fence->magic = 0;
    free(fence);
    native_device_release(device);
}
void wgpuSharedFenceSetLabel(WGPUSharedFence fence, WGPUStringView label)
{
    if (!valid_fence(fence)) return;
    wgpuSharedFenceAddRef(fence);
    native_label(fence->device, fence, label);
    wgpuSharedFenceRelease(fence);
}
WGPUSharedFence wgpuDeviceImportSharedFence(WGPUDevice handle,
    const WGPUSharedFenceDescriptor *descriptor)
{
    native_device *device = (native_device *)handle;
    if (!device || device->backend.kind != WGPUBackendType_Metal || !descriptor) return NULL;
    const WGPUSharedFenceMTLSharedEventDescriptor *source = (const void *)find_chain(
        descriptor->nextInChain, DOE_SHARED_STYPE_MTL_EVENT_DESCRIPTOR);
    if (!source || !source->sharedEvent) return NULL;
    WGPUSharedFence fence = allocate_fence(device);
    if (!fence) return NULL;
    native_lock(device);
    native_failure failure = native_failure_status(native_device_result(device));
    if (failure.status == NATIVE_OK) {
        doe_mtl_error detail = {0};
        doe_mtl_status status = doe_mtl_shared_event_import(metal_context(device), source->sharedEvent,
            &fence->event, &detail);
        failure = native_failure_metal(status, &detail);
    }
    native_unlock(device);
    if (failure.status != NATIVE_OK) {
        shared_error(device, failure, "shared fence import failed");
        wgpuSharedFenceRelease(fence);
        return NULL;
    }
    native_label(device, fence, descriptor->label);
    return fence;
}
void wgpuSharedFenceExportInfo(WGPUSharedFence fence, WGPUSharedFenceExportInfo *info)
{
    if (!valid_fence(fence) || !info) return;
    info->type = DOE_SHARED_FENCE_MTL_EVENT;
    WGPUSharedFenceMTLSharedEventExportInfo *output = (void *)find_chain(
        info->nextInChain, DOE_SHARED_STYPE_MTL_EVENT_EXPORT);
    if (output) output->sharedEvent = fence->event;
}

void wgpuSharedTextureMemoryAddRef(WGPUSharedTextureMemory memory)
{
    if (valid_memory(memory)) native_retain_count(&memory->references);
}
void wgpuSharedTextureMemoryRelease(WGPUSharedTextureMemory memory)
{
    if (!valid_memory(memory) || !native_release_count(&memory->references)) return;
    native_device *device = memory->device;
    native_lock(device);
    if (doe_label_store_remove(device->labels, memory) != DOE_LABEL_OK) abort();
    doe_mtl_shared_surface_release(metal_context(device), memory->surface);
    native_unlock(device);
    memory->magic = 0;
    free(memory);
    native_device_release(device);
}
void wgpuSharedTextureMemorySetLabel(WGPUSharedTextureMemory memory, WGPUStringView label)
{
    if (!valid_memory(memory)) return;
    wgpuSharedTextureMemoryAddRef(memory);
    native_label(memory->device, memory, label);
    wgpuSharedTextureMemoryRelease(memory);
}
WGPUSharedTextureMemory wgpuDeviceImportSharedTextureMemory(WGPUDevice handle,
    const WGPUSharedTextureMemoryDescriptor *descriptor)
{
    native_device *device = (native_device *)handle;
    if (!device || device->backend.kind != WGPUBackendType_Metal || !descriptor) return NULL;
    const WGPUSharedTextureMemoryIOSurfaceDescriptor *source = (const void *)find_chain(
        descriptor->nextInChain, DOE_SHARED_STYPE_IOSURFACE);
    if (!source || !source->ioSurface) return NULL;
    WGPUSharedTextureMemory memory = calloc(1, sizeof(*memory));
    if (!memory) return NULL;
    memory->magic = SHARED_MEMORY_MAGIC;
    atomic_init(&memory->references, 1);
    memory->device = device;
    native_device_retain(device);
    native_lock(device);
    native_failure failure = native_failure_status(native_device_result(device));
    if (failure.status == NATIVE_OK) {
        doe_mtl_error detail = {0};
        doe_mtl_shared_surface_info info;
        doe_mtl_status status = doe_mtl_shared_surface_open_info(metal_context(device), source->ioSurface,
            &memory->surface, &info, &detail);
        if (status == DOE_MTL_OK) {
            memory->width = info.width;
            memory->height = info.height;
            memory->format = info.format == DOE_MTL_RGBA8_UNORM ?
                WGPUTextureFormat_RGBA8Unorm : WGPUTextureFormat_BGRA8Unorm;
        }
        failure = native_failure_metal(status, &detail);
    }
    native_unlock(device);
    if (failure.status != NATIVE_OK) {
        shared_error(device, failure, "shared IOSurface import failed");
        wgpuSharedTextureMemoryRelease(memory);
        return NULL;
    }
    native_label(device, memory, descriptor->label);
    return memory;
}

static void release_shared_texture(void *userdata, void *handle)
{
    (void)userdata;
    shared_texture_payload *payload = handle;
    WGPUSharedTextureMemory memory = payload->texture.shared_memory;
    native_lock(memory->device);
    if (payload->identity && memory->active_texture == payload->identity) {
        memory->active_texture = NULL;
        memory->access = SHARED_IDLE;
    }
    native_unlock(memory->device);
    native_texture_release(NULL, &payload->texture);
    /* The canonical resource still has its device lease during finalization,
     * so this drop cannot finalize that device under a caller's resource lock. */
    wgpuSharedTextureMemoryRelease(memory);
}
static bool shared_format(WGPUTextureFormat format, doe_mtl_format *out)
{
    switch (format) {
    case WGPUTextureFormat_RGBA8Unorm: *out = DOE_MTL_RGBA8_UNORM; return true;
    case WGPUTextureFormat_RGBA8UnormSrgb: *out = DOE_MTL_RGBA8_SRGB; return true;
    case WGPUTextureFormat_BGRA8Unorm: *out = DOE_MTL_BGRA8_UNORM; return true;
    case WGPUTextureFormat_BGRA8UnormSrgb: *out = DOE_MTL_BGRA8_SRGB; return true;
    default: return false;
    }
}
WGPUTexture wgpuSharedTextureMemoryCreateTexture(WGPUSharedTextureMemory memory,
    const WGPUTextureDescriptor *descriptor)
{
    if (!valid_memory(memory) || !descriptor) return NULL;
    wgpuSharedTextureMemoryAddRef(memory);
    native_device *device = memory->device;
    doe_resource_texture *texture = NULL;
    doe_resource_texture_desc desc = {.dimension=descriptor->dimension,.format=descriptor->format,
        .width=descriptor->size.width,.height=descriptor->size.height,
        .depth_or_layers=descriptor->size.depthOrArrayLayers,.mip_levels=descriptor->mipLevelCount,
        .samples=descriptor->sampleCount,.usage=descriptor->usage,
        .view_formats=descriptor->viewFormats,.view_format_count=descriptor->viewFormatCount};
    doe_resource_texture_limits limits = {device->backend.limits.maxTextureDimension1D,
        device->backend.limits.maxTextureDimension2D,device->backend.limits.maxTextureDimension3D,
        device->backend.limits.maxTextureArrayLayers};
    doe_mtl_texture_desc native = {.type=DOE_MTL_TEXTURE_2D,.width=desc.width,.height=desc.height,
        .depth_or_layers=1,.mip_levels=1,.sample_count=1,.allow_format_views=desc.view_format_count != 0};
    if (desc.usage & WGPUTextureUsage_TextureBinding) native.usage |= DOE_MTL_TEXTURE_SAMPLED;
    if (desc.usage & WGPUTextureUsage_RenderAttachment) native.usage |= DOE_MTL_TEXTURE_RENDER_TARGET;
    if (desc.usage & WGPUTextureUsage_CopySrc) native.usage |= DOE_MTL_TEXTURE_COPY_SRC;
    if (desc.usage & WGPUTextureUsage_CopyDst) native.usage |= DOE_MTL_TEXTURE_COPY_DST;
    native_lock(device);
    native_failure failure = native_failure_status(native_device_result(device));
    bool shape = !descriptor->nextInChain && shared_format(desc.format, &native.format) &&
        memory->format == desc.format &&
        desc.dimension == WGPUTextureDimension_2D && desc.width == memory->width && desc.height == memory->height &&
        desc.depth_or_layers == 1 && desc.mip_levels == 1 && desc.samples == 1 && !(desc.usage & ~shared_usage);
    doe_resource_status valid = shape ? doe_resource_texture_validate_with_features(&desc, &limits,
        device->resources.format_features) : DOE_RESOURCE_INVALID_ARGUMENT;
    shared_texture_payload *payload = NULL;
    if (failure.status == NATIVE_OK && valid == DOE_RESOURCE_OK) {
        payload = calloc(1, sizeof(*payload));
        if (!payload) failure = native_failure_status(NATIVE_OUT_OF_MEMORY);
    }
    if (payload) {
        payload->texture.device = device;
        payload->texture.shared_memory = memory;
        wgpuSharedTextureMemoryAddRef(memory);
        doe_mtl_error detail = {0};
        doe_mtl_status status = doe_mtl_shared_texture_import(metal_context(device), memory->surface,
            &native, &payload->texture.metal, &detail);
        failure = native_failure_metal(status, &detail);
        if (failure.status == NATIVE_OK)
            valid = doe_resource_texture_create(&device->resources, &desc, &limits,
                (doe_resource_native){payload,NULL,release_shared_texture}, &texture);
        if (texture) {
            payload->identity = texture;
        }
    }
    native_unlock(device);
    if (!texture) {
        if (payload) release_shared_texture(NULL, payload);
        if (failure.status != NATIVE_OK) shared_error(device, failure, "shared IOSurface texture creation failed");
        else native_error(device, native_resource_error(valid), "shared IOSurface texture descriptor rejected");
    } else native_label(device, texture, descriptor->label);
    wgpuSharedTextureMemoryRelease(memory);
    return (WGPUTexture)texture;
}

static bool matches_texture(WGPUSharedTextureMemory memory, doe_resource_texture *texture)
{
    if (!texture || doe_resource_texture_device(texture) != memory->device ||
        !doe_resource_texture_available(texture)) return false;
    native_texture *payload = doe_resource_texture_native(texture);
    return payload && payload->shared_memory == memory && payload->metal;
}
WGPUStatus wgpuSharedTextureMemoryBeginAccess(WGPUSharedTextureMemory memory, WGPUTexture handle,
    const WGPUSharedTextureMemoryBeginAccessDescriptor *descriptor)
{
    if (!valid_memory(memory) || !handle) return WGPUStatus_Error;
    size_t count = descriptor ? descriptor->fenceCount : 0;
    if (descriptor && count != descriptor->signaledValueCount) return WGPUStatus_Error;
    if (count && (!descriptor->fences || !descriptor->signaledValues || count > SIZE_MAX / sizeof(shared_wait)))
        return WGPUStatus_Error;
    shared_wait *waits = count ? calloc(count, sizeof(*waits)) : NULL;
    if (count && !waits) return WGPUStatus_Error;
    size_t retained = 0;
    for (; retained < count; ++retained) {
        WGPUSharedFence fence = descriptor->fences[retained];
        if (!valid_fence(fence)) break;
        wgpuSharedFenceAddRef(fence);
        waits[retained] = (shared_wait){fence,descriptor->signaledValues[retained]};
    }
    wgpuSharedTextureMemoryAddRef(memory);
    doe_resource_texture *texture = (doe_resource_texture *)handle;
    doe_resource_texture_retain(texture);
    native_device *device = memory->device;
    native_lock(device);
    bool admitted = retained == count && memory->access == SHARED_IDLE && matches_texture(memory, texture) &&
        native_device_result(device) == NATIVE_OK;
    if (admitted) memory->access = SHARED_WAITING;
    native_unlock(device);
    native_failure failure = native_failure_status(NATIVE_OK);
    for (size_t i = 0; admitted && failure.status == NATIVE_OK && i < count; ++i) {
        doe_mtl_error detail = {0};
        WGPUSharedFence fence = waits[i].fence;
        /* No device lock spans a producer wait. Fence leases keep its event and
         * original context alive even across another thread's public release. */
        doe_mtl_status status = doe_mtl_shared_event_wait(metal_context(fence->device), fence->event,
            waits[i].value, &detail);
        failure = native_failure_metal(status, &detail);
    }
    native_lock(device);
    bool success = admitted && failure.status == NATIVE_OK && matches_texture(memory, texture) &&
        native_device_result(device) == NATIVE_OK;
    if (admitted) {
        memory->access = success ? SHARED_ACTIVE : SHARED_IDLE;
        memory->active_texture = success ? texture : NULL;
    }
    native_unlock(device);
    for (size_t i = 0; i < retained; ++i) wgpuSharedFenceRelease(waits[i].fence);
    free(waits);
    shared_error(device, failure, "shared IOSurface producer wait failed");
    doe_resource_texture_release(texture);
    wgpuSharedTextureMemoryRelease(memory);
    return success ? WGPUStatus_Success : WGPUStatus_Error;
}

static native_failure completion_fence(native_device *device, WGPUSharedFence fence)
{
    doe_mtl_error detail = {0};
    doe_mtl_status status = doe_mtl_shared_event_create(metal_context(device), &fence->event, &detail);
    native_failure failure = native_failure_metal(status, &detail);
    native_command_handle command = {0};
    if (failure.status == NATIVE_OK) failure = native_command_create(&device->backend, &command);
    if (failure.status == NATIVE_OK) {
        status = doe_mtl_commands_signal_shared_event(command.value.metal, fence->event, 1, &detail);
        failure = native_failure_metal(status, &detail);
    }
    if (failure.status == NATIVE_OK) {
        /* The existing pending owner adopts commands on every submit result,
         * including unknown completion. Its event lease outlives this wrapper. */
        failure = native_submit_backend_commands(device, command);
    } else if (native_command_release(&command) != NATIVE_OK) abort();
    return failure;
}
WGPUStatus wgpuSharedTextureMemoryEndAccess(WGPUSharedTextureMemory memory, WGPUTexture handle,
    WGPUSharedTextureMemoryEndAccessState *state)
{
    if (!valid_memory(memory) || !handle) return WGPUStatus_Error;
    wgpuSharedTextureMemoryAddRef(memory);
    doe_resource_texture *texture = (doe_resource_texture *)handle;
    doe_resource_texture_retain(texture);
    native_device *device = memory->device;
    WGPUSharedFence *fences = state ? calloc(1, sizeof(*fences)) : NULL;
    uint64_t *values = state ? calloc(1, sizeof(*values)) : NULL;
    WGPUSharedFence fence = state && fences && values ? allocate_fence(device) : NULL;
    native_lock(device);
    bool admitted = memory->access == SHARED_ACTIVE && memory->active_texture == texture &&
        matches_texture(memory, texture) && (!state || fence);
    native_failure failure = native_failure_status(NATIVE_OK);
    if (admitted && state) {
        failure = native_failure_status(native_device_result(device));
        if (failure.status == NATIVE_OK) failure = native_settle_pending(device).failure;
        if (failure.status == NATIVE_OK) failure = completion_fence(device, fence);
    }
    bool success = admitted && failure.status == NATIVE_OK;
    if (success) {
        if (state) {
            fences[0] = fence;
            values[0] = 1;
            state->initialized = WGPU_TRUE;
            state->fenceCount = 1;
            state->fences = fences;
            state->signaledValueCount = 1;
            state->signaledValues = values;
        }
        memory->access = SHARED_IDLE;
        memory->active_texture = NULL;
    }
    native_unlock(device);
    if (!success) {
        wgpuSharedFenceRelease(fence);
        free(values);
        free(fences);
    }
    shared_error(device, failure, "shared IOSurface completion fence failed");
    doe_resource_texture_release(texture);
    wgpuSharedTextureMemoryRelease(memory);
    return success ? WGPUStatus_Success : WGPUStatus_Error;
}
WGPUStatus wgpuSharedTextureMemoryGetProperties(WGPUSharedTextureMemory memory,
    WGPUSharedTextureMemoryProperties *properties)
{
    if (!valid_memory(memory)) return WGPUStatus_Error;
    native_lock(memory->device);
    if (properties) {
        properties->usage = shared_usage;
        properties->size = (WGPUExtent3D){memory->width,memory->height,1};
        properties->format = memory->format;
    }
    native_unlock(memory->device);
    return WGPUStatus_Success;
}
WGPUBool wgpuSharedTextureMemoryIsDeviceLost(WGPUSharedTextureMemory memory)
{
    if (!valid_memory(memory)) return WGPU_TRUE;
    native_lock(memory->device);
    bool lost = native_device_result(memory->device) != NATIVE_OK;
    native_unlock(memory->device);
    return lost ? WGPU_TRUE : WGPU_FALSE;
}
void wgpuSharedTextureMemoryEndAccessStateFreeMembers(WGPUSharedTextureMemoryEndAccessState state)
{
    if (state.fences) {
        for (size_t i = 0; i < state.fenceCount; ++i) wgpuSharedFenceRelease(state.fences[i]);
        free((void *)state.fences);
    }
    free((void *)state.signaledValues);
}
