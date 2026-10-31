/* SPDX-License-Identifier: MIT
 * Linux Vulkan shared images: an OPAQUE_FD import (one backend image shared by
 * every texture created from the memory) and SYNC_FD fences. Begin and End
 * submit backend ownership transfers through the existing pending owner without
 * a CPU wait; producer fences become queue waits and End exports a sync file
 * signaled after all earlier queue work. No CPU copy or substitute path. */
#include "shared_memory_internal.h"
#include "doe_vk_external.h"
#include <stdlib.h>

#if UINTPTR_MAX == UINT64_MAX
_Static_assert(offsetof(WGPUSharedTextureMemoryOpaqueFDDescriptor, vkImageCreateInfo) == 16, "pinned opaque fd ABI");
_Static_assert(offsetof(WGPUSharedTextureMemoryOpaqueFDDescriptor, memoryFD) == 24, "pinned opaque fd ABI");
_Static_assert(offsetof(WGPUSharedTextureMemoryOpaqueFDDescriptor, memoryTypeIndex) == 28, "pinned opaque fd ABI");
_Static_assert(offsetof(WGPUSharedTextureMemoryOpaqueFDDescriptor, allocationSize) == 32, "pinned opaque fd ABI");
_Static_assert(offsetof(WGPUSharedTextureMemoryOpaqueFDDescriptor, dedicatedAllocation) == 40, "pinned opaque fd ABI");
_Static_assert(sizeof(WGPUSharedTextureMemoryOpaqueFDDescriptor) == 48, "pinned opaque fd ABI");
_Static_assert(offsetof(WGPUSharedTextureMemoryVkImageLayoutBeginState, oldLayout) == 16, "pinned layout ABI");
_Static_assert(sizeof(WGPUSharedTextureMemoryVkImageLayoutEndState) == 24, "pinned layout ABI");
_Static_assert(offsetof(WGPUSharedFenceSyncFDDescriptor, handle) == 16, "pinned sync fd ABI");
_Static_assert(offsetof(WGPUSharedFenceSyncFDExportInfo, handle) == 16, "pinned sync fd ABI");
#endif

static bool interop_enabled(native_device *device)
{
    return native_feature_enabled(device, (WGPUFeatureName)DOE_SHARED_FEATURE_OPAQUE_FD) &&
        native_feature_enabled(device, (WGPUFeatureName)DOE_SHARED_FEATURE_SYNC_FD);
}
#if UINTPTR_MAX == UINT64_MAX
_Static_assert(offsetof(WGPUSharedFenceVkSemaphoreOpaqueFDDescriptor, handle) == 16, "pinned opaque semaphore ABI");
#endif
static bool fence_valid(WGPUSharedFence fence)
{
    return fence && fence->magic == SHARED_FENCE_MAGIC;
}

WGPUSharedFence native_vulkan_import_shared_fence(native_device *device, const WGPUSharedFenceDescriptor *descriptor)
{
    /* Same layout: chain then one int descriptor. */
    const WGPUSharedFenceSyncFDDescriptor *source = (const void *)native_shared_find_chain(
        descriptor->nextInChain, DOE_SHARED_STYPE_SYNC_FD_DESCRIPTOR);
    const WGPUSharedFenceVkSemaphoreOpaqueFDDescriptor *opaque = (const void *)native_shared_find_chain(
        descriptor->nextInChain, DOE_SHARED_STYPE_OPAQUE_SEMAPHORE_DESCRIPTOR);
    bool admitted = (source != NULL) != (opaque != NULL) &&
        (source ? native_feature_enabled(device, (WGPUFeatureName)DOE_SHARED_FEATURE_SYNC_FD) :
            native_feature_enabled(device, (WGPUFeatureName)DOE_SHARED_FEATURE_OPAQUE_SEMAPHORE) &&
            opaque->handle >= 0);
    if (!admitted) {
        native_error(device, WGPUErrorType_Validation,
            "shared fence needs one enabled SYNC_FD or OPAQUE_FD semaphore descriptor");
        return NULL;
    }
    const int handle = source ? source->handle : opaque->handle;
    WGPUSharedFence fence = native_shared_allocate_fence(device);
    if (!fence) return NULL;
    /* Imports borrow the caller's descriptor; a -1 sync file is already signaled. */
    fence->opaque = opaque != NULL;
    fence->sync_fd = doe_vk_external_duplicate_fd(handle);
    if (handle >= 0 && fence->sync_fd < 0) {
        native_error(device, WGPUErrorType_Internal, "shared fence sync file duplicate failed");
        wgpuSharedFenceRelease(fence);
        return NULL;
    }
    native_label(device, fence, descriptor->label);
    return fence;
}

static bool public_format(doe_vk_texture_format native, WGPUTextureFormat *out)
{
    switch (native) {
    case DOE_VK_FORMAT_RGBA8_UNORM: *out = WGPUTextureFormat_RGBA8Unorm; return true;
    case DOE_VK_FORMAT_BGRA8_UNORM: *out = WGPUTextureFormat_BGRA8Unorm; return true;
    case DOE_VK_FORMAT_RGBA16_FLOAT: *out = WGPUTextureFormat_RGBA16Float; return true;
    default: return false;
    }
}
static WGPUTextureUsage public_usage(uint32_t native)
{
    WGPUTextureUsage usage = 0;
    if (native & DOE_VK_TEXTURE_COPY_SRC) usage |= WGPUTextureUsage_CopySrc;
    if (native & DOE_VK_TEXTURE_COPY_DST) usage |= WGPUTextureUsage_CopyDst;
    if (native & DOE_VK_TEXTURE_SAMPLED) usage |= WGPUTextureUsage_TextureBinding;
    if (native & DOE_VK_TEXTURE_STORAGE) usage |= WGPUTextureUsage_StorageBinding;
    if (native & DOE_VK_TEXTURE_COLOR_ATTACHMENT) usage |= WGPUTextureUsage_RenderAttachment;
    return usage;
}

WGPUSharedTextureMemory native_vulkan_import_shared_memory(native_device *device,
    const WGPUSharedTextureMemoryDescriptor *descriptor)
{
    const WGPUSharedTextureMemoryOpaqueFDDescriptor *source = (const void *)native_shared_find_chain(
        descriptor->nextInChain, DOE_SHARED_STYPE_OPAQUE_FD);
    doe_vk_external_image_desc desc;
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    if (!source || !interop_enabled(device) || source->memoryFD < 0 ||
        doe_vk_external_image_desc_parse(source->vkImageCreateInfo, &desc) != DOE_VK_OK ||
        !public_format(desc.format, &format)) {
        native_error(device, WGPUErrorType_Validation, "shared OPAQUE_FD memory descriptor unsupported");
        return NULL;
    }
    desc.memory_fd = source->memoryFD;
    desc.allocation_size = source->allocationSize;
    desc.memory_type_index = source->memoryTypeIndex;
    desc.dedicated = source->dedicatedAllocation != 0;
    WGPUSharedTextureMemory memory = calloc(1, sizeof(*memory));
    if (!memory) {
        native_error(device, WGPUErrorType_OutOfMemory, "shared memory allocation failed");
        return NULL;
    }
    memory->magic = SHARED_MEMORY_MAGIC;
    atomic_init(&memory->references, 1);
    memory->device = device;
    native_device_retain(device);
    native_lock(device);
    native_failure failure = native_failure_status(native_device_result(device));
    if (failure.status == NATIVE_OK) {
        doe_vk_error detail = {0};
        doe_vk_status status = doe_vk_texture_import_opaque_fd(device->backend.value.vulkan.context, &desc,
            &memory->vulkan, &detail);
        failure = native_failure_vulkan(status, &detail);
    }
    native_unlock(device);
    if (failure.status != NATIVE_OK) {
        native_shared_error(device, failure, "shared OPAQUE_FD import failed");
        memory->vulkan = NULL;
        wgpuSharedTextureMemoryRelease(memory);
        return NULL;
    }
    memory->width = desc.width;
    memory->height = desc.height;
    memory->format = format;
    memory->usage = public_usage(desc.usage);
    memory->mutable_format = (desc.flags & 8u) != 0; /* VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT */
    native_label(device, memory, descriptor->label);
    return memory;
}

static bool view_format_admitted(WGPUSharedTextureMemory memory, WGPUTextureFormat view)
{
    if (view == memory->format) return true;
    if (!memory->mutable_format) return false;
    return (memory->format == WGPUTextureFormat_RGBA8Unorm && view == WGPUTextureFormat_RGBA8UnormSrgb) ||
        (memory->format == WGPUTextureFormat_BGRA8Unorm && view == WGPUTextureFormat_BGRA8UnormSrgb);
}

WGPUTexture native_vulkan_shared_create_texture(WGPUSharedTextureMemory memory, const WGPUTextureDescriptor *descriptor)
{
    native_device *device = memory->device;
    bool shape = !descriptor->nextInChain && descriptor->dimension == WGPUTextureDimension_2D &&
        descriptor->format == memory->format && descriptor->size.width == memory->width &&
        descriptor->size.height == memory->height && descriptor->size.depthOrArrayLayers == 1 &&
        descriptor->mipLevelCount == 1 && descriptor->sampleCount == 1 &&
        !(descriptor->usage & ~memory->usage) && (!descriptor->viewFormatCount || descriptor->viewFormats);
    for (size_t i = 0; shape && i < descriptor->viewFormatCount; ++i)
        shape = view_format_admitted(memory, descriptor->viewFormats[i]);
    doe_resource_texture_desc desc = {.dimension=WGPUTextureDimension_2D,.format=descriptor->format,
        .width=descriptor->size.width,.height=descriptor->size.height,.depth_or_layers=1,.mip_levels=1,
        .samples=1,.usage=descriptor->usage,.view_formats=descriptor->viewFormats,
        .view_format_count=descriptor->viewFormatCount};
    doe_resource_texture_limits limits = {device->backend.limits.maxTextureDimension1D,
        device->backend.limits.maxTextureDimension2D,device->backend.limits.maxTextureDimension3D,
        device->backend.limits.maxTextureArrayLayers};
    if (!shape || doe_resource_texture_validate_with_features(&desc, &limits,
            device->resources.format_features) != DOE_RESOURCE_OK) {
        native_error(device, WGPUErrorType_Validation, "shared OPAQUE_FD texture descriptor rejected");
        return NULL;
    }
    shared_texture_payload *payload = calloc(1, sizeof(*payload));
    if (!payload) {
        native_error(device, WGPUErrorType_OutOfMemory, "shared texture payload allocation failed");
        return NULL;
    }
    payload->texture.device = device;
    payload->texture.shared_memory = memory;
    wgpuSharedTextureMemoryAddRef(memory);
    doe_resource_texture *texture = NULL;
    native_lock(device);
    native_failure failure = native_failure_status(native_device_result(device));
    doe_resource_status adopted = DOE_RESOURCE_INVALID_ARGUMENT;
    if (failure.status == NATIVE_OK) {
        failure = native_failure_status(native_from_vulkan(doe_vk_texture_retain(memory->vulkan)));
        if (failure.status == NATIVE_OK) payload->texture.texture = memory->vulkan;
    }
    if (failure.status == NATIVE_OK)
        adopted = doe_resource_texture_create(&device->resources, &desc, &limits,
            (doe_resource_native){payload,NULL,native_shared_texture_finalize}, &texture);
    if (texture) payload->identity = texture;
    native_unlock(device);
    if (!texture) {
        native_shared_texture_finalize(NULL, payload);
        if (failure.status != NATIVE_OK) native_shared_error(device, failure, "shared OPAQUE_FD texture creation failed");
        else native_error(device, native_resource_error(adopted), "shared OPAQUE_FD texture creation rejected");
        return NULL;
    }
    native_label(device, texture, descriptor->label);
    return (WGPUTexture)texture;
}

/* Exactly one layout state of the matching direction is admitted. */
static bool begin_layout(const WGPUSharedTextureMemoryBeginAccessDescriptor *descriptor, uint32_t *old_layout,
    uint32_t *new_layout)
{
    *old_layout = 0;
    *new_layout = 0;
    const WGPUChainedStruct *chain = descriptor ? descriptor->nextInChain : NULL;
    if (!chain) return true;
    if (chain->sType != (WGPUSType)DOE_SHARED_STYPE_VK_LAYOUT_BEGIN || chain->next) return false;
    const WGPUSharedTextureMemoryVkImageLayoutBeginState *state = (const void *)chain;
    if (state->oldLayout < 0 || state->newLayout < 0) return false;
    *old_layout = (uint32_t)state->oldLayout;
    *new_layout = (uint32_t)state->newLayout;
    return true;
}

WGPUStatus native_vulkan_shared_begin(WGPUSharedTextureMemory memory, doe_resource_texture *texture,
    const WGPUSharedTextureMemoryBeginAccessDescriptor *descriptor)
{
    native_device *device = memory->device;
    size_t count = descriptor ? descriptor->fenceCount : 0;
    uint32_t old_layout, new_layout;
    if ((descriptor && count != descriptor->signaledValueCount) || (count && !descriptor->fences) ||
        count > DOE_VK_EXTERNAL_WAITS || !begin_layout(descriptor, &old_layout, &new_layout)) {
        native_error(device, WGPUErrorType_Validation, "shared OPAQUE_FD begin descriptor rejected");
        return WGPUStatus_Error;
    }
    doe_vk_external_wait waits[DOE_VK_EXTERNAL_WAITS];
    for (size_t i = 0; i < count; ++i) {
        WGPUSharedFence fence = descriptor->fences[i];
        if (!fence_valid(fence) || fence->device != device) {
            native_error(device, WGPUErrorType_Validation, "shared begin fence is not a fence of this device");
            return WGPUStatus_Error;
        }
    }
    wgpuSharedTextureMemoryAddRef(memory);
    for (size_t i = 0; i < count; ++i) wgpuSharedFenceAddRef(descriptor->fences[i]);
    doe_resource_texture_retain(texture);
    native_lock(device);
    native_failure failure = native_failure_status(native_device_result(device));
    bool admitted = failure.status == NATIVE_OK && memory->access == SHARED_IDLE &&
        native_shared_matches_texture(memory, texture);
    if (failure.status == NATIVE_OK && !admitted) failure = native_failure_status(NATIVE_INVALID_STATE);
    native_command_handle command = {0};
    if (admitted) failure = native_command_create(&device->backend, &command);
    if (admitted && failure.status == NATIVE_OK) {
        for (size_t i = 0; i < count; ++i)
            waits[i] = (doe_vk_external_wait){descriptor->fences[i]->sync_fd, descriptor->fences[i]->opaque};
        doe_vk_status status = doe_vk_commands_external_acquire(command.value.vulkan, memory->vulkan, waits,
            (uint32_t)count, old_layout, new_layout, descriptor && descriptor->initialized);
        failure = native_failure_status(native_from_vulkan(status));
        if (failure.status == NATIVE_OK) failure = native_submit_backend_commands_async(device, command);
        else if (native_command_release(&command) != NATIVE_OK) abort();
    }
    bool success = admitted && failure.status == NATIVE_OK;
    if (success) {
        memory->access = SHARED_ACTIVE;
        memory->active_texture = texture;
    }
    native_unlock(device);
    for (size_t i = 0; i < count; ++i) wgpuSharedFenceRelease(descriptor->fences[i]);
    if (!success) native_shared_error(device, failure, "shared OPAQUE_FD acquire failed");
    doe_resource_texture_release(texture);
    wgpuSharedTextureMemoryRelease(memory);
    return success ? WGPUStatus_Success : WGPUStatus_Error;
}

WGPUStatus native_vulkan_shared_end(WGPUSharedTextureMemory memory, doe_resource_texture *texture,
    WGPUSharedTextureMemoryEndAccessState *state)
{
    native_device *device = memory->device;
    /* A release without an exported fence would hand back contents with no
     * completion to wait for, so a state is required. */
    WGPUSharedTextureMemoryVkImageLayoutEndState *layout_out = NULL;
    if (state && state->nextInChain) {
        if (state->nextInChain->sType != (WGPUSType)DOE_SHARED_STYPE_VK_LAYOUT_END || state->nextInChain->next) {
            native_error(device, WGPUErrorType_Validation, "shared OPAQUE_FD end state chain rejected");
            return WGPUStatus_Error;
        }
        layout_out = (void *)state->nextInChain;
    }
    if (!state) {
        native_error(device, WGPUErrorType_Validation, "shared OPAQUE_FD end requires an output state");
        return WGPUStatus_Error;
    }
    WGPUSharedFence *fences = calloc(1, sizeof(*fences));
    uint64_t *values = calloc(1, sizeof(*values));
    WGPUSharedFence fence = fences && values ? native_shared_allocate_fence(device) : NULL;
    wgpuSharedTextureMemoryAddRef(memory);
    doe_resource_texture_retain(texture);
    native_lock(device);
    native_failure failure = native_failure_status(fence ? native_device_result(device) : NATIVE_OUT_OF_MEMORY);
    bool admitted = failure.status == NATIVE_OK && memory->access == SHARED_ACTIVE &&
        memory->active_texture == texture && native_shared_matches_texture(memory, texture);
    if (failure.status == NATIVE_OK && !admitted) failure = native_failure_status(NATIVE_INVALID_STATE);
    uint32_t layout = 0;
    bool initialized = false;
    native_command_handle command = {0};
    if (admitted) failure = native_command_create(&device->backend, &command);
    if (admitted && failure.status == NATIVE_OK) {
        doe_vk_commands *recording = command.value.vulkan;
        doe_vk_status status = doe_vk_commands_external_release(recording, memory->vulkan, &layout, &initialized);
        failure = native_failure_status(native_from_vulkan(status));
        if (failure.status == NATIVE_OK) {
            /* The pending owner adopts the recording; the device lock keeps it
             * linked (and its semaphore alive) for the export below. */
            failure = native_submit_backend_commands_async(device, command);
            if (failure.status == NATIVE_OK) {
                doe_vk_error detail = {0};
                failure = native_failure_vulkan(doe_vk_commands_export_sync_fd(recording, &fence->sync_fd, &detail),
                    &detail);
            }
        } else if (native_command_release(&command) != NATIVE_OK) abort();
    }
    bool success = admitted && failure.status == NATIVE_OK;
    if (success) {
        memory->access = SHARED_IDLE;
        memory->active_texture = NULL;
    }
    native_unlock(device);
    if (success) {
        fences[0] = fence;
        values[0] = 1;
        state->initialized = initialized ? WGPU_TRUE : WGPU_FALSE;
        state->fenceCount = 1;
        state->fences = fences;
        state->signaledValueCount = 1;
        state->signaledValues = values;
        if (layout_out) {
            layout_out->oldLayout = (int32_t)layout;
            layout_out->newLayout = (int32_t)layout;
        }
    } else {
        wgpuSharedFenceRelease(fence);
        free(values);
        free(fences);
        native_shared_error(device, failure, "shared OPAQUE_FD release failed");
    }
    doe_resource_texture_release(texture);
    wgpuSharedTextureMemoryRelease(memory);
    return success ? WGPUStatus_Success : WGPUStatus_Error;
}
