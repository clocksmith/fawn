/* SPDX-License-Identifier: MIT
 * Private shared fence/memory representation shared by the Metal (shared_memory.c)
 * and Linux Vulkan (shared_memory_vulkan.c) import owners. */
#ifndef DOE_NATIVE_SHARED_MEMORY_INTERNAL_H
#define DOE_NATIVE_SHARED_MEMORY_INTERNAL_H
#include "internal.h"
#include "doe_native_shared_memory.h"
#include "../../backends/d3d12/include/doe_d3d12_shared.h"

#define SHARED_MEMORY_MAGIC UINT32_C(0xD0E10020)
#define SHARED_FENCE_MAGIC UINT32_C(0xD0E10021)
typedef enum shared_access { SHARED_IDLE, SHARED_WAITING, SHARED_ACTIVE, SHARED_ENDING } shared_access;
struct native_shared_texture_memory {
    uint32_t magic;
    atomic_uint references;
    native_device *device;
    doe_dx_texture *d3d12; /* DXGI import; canonical backend texture shared by views. */
    void *surface;           /* Metal IOSurface lease. */
    uint64_t metal_acquire_value; /* Accepted queue wait, including accesses with no texture work. */
    doe_vk_texture *vulkan;  /* Vulkan OPAQUE_FD import; one image for every texture. */
    WGPUTextureUsage usage;  /* Vulkan: the producer image's WebGPU-visible usage. */
    bool mutable_format;     /* Vulkan: the producer admits its srgb/unorm view pair. */
    uint32_t width, height;
    WGPUTextureFormat format;
    shared_access access;
    doe_resource_texture *active_texture; /* Weak; texture finalization clears it. */
};
struct native_shared_fence {
    uint32_t magic;
    atomic_uint references;
    native_device *device;
    doe_dx_shared_fence *d3d12;
    void *event;  /* Metal shared event. */
    int sync_fd;  /* Vulkan: owned sync file (-1: already signaled) or opaque semaphore fd. */
    bool opaque;  /* Vulkan: sync_fd is an OPAQUE_FD binary semaphore. */
    /* Vulkan EndAccess state storage: one fence (this one) and its value, freed
     * with the fence by FreeMembers instead of separate arrays. */
    WGPUSharedFence end_fence;
    uint64_t end_value;
};
typedef struct shared_texture_payload {
    native_texture texture; /* Canonical payload prefix, never another resource. */
    doe_resource_texture *identity;
} shared_texture_payload;

/* The pinned public descriptor defaults Undefined to 2D for every import route.
 * Preserve explicit dimensions so each shared-image admission still rejects
 * unsupported 1D/3D shapes without changing the caller's descriptor. */
static inline WGPUTextureDimension native_shared_texture_dimension(WGPUTextureDimension dimension)
{
    return dimension == WGPUTextureDimension_Undefined ? WGPUTextureDimension_2D : dimension;
}

const WGPUChainedStruct *native_shared_find_chain(const WGPUChainedStruct *chain, uint32_t type);
void native_shared_error(native_device *device, native_failure failure, const char *operation);
WGPUSharedFence native_shared_allocate_fence(native_device *device);
void native_shared_texture_finalize(void *userdata, void *handle);
bool native_shared_texture_accessible(doe_resource_texture *);
bool native_shared_matches_texture(WGPUSharedTextureMemory memory, doe_resource_texture *texture);

/* Windows DXGI owner; non-Windows entry points fail closed. */
WGPUSharedFence native_d3d12_import_shared_fence(native_device *,const WGPUSharedFenceDescriptor *);
WGPUSharedTextureMemory native_d3d12_import_shared_memory(native_device *,const WGPUSharedTextureMemoryDescriptor *);
WGPUTexture native_d3d12_shared_create_texture(WGPUSharedTextureMemory,const WGPUTextureDescriptor *);
WGPUStatus native_d3d12_shared_begin(WGPUSharedTextureMemory,doe_resource_texture *,const WGPUSharedTextureMemoryBeginAccessDescriptor *);
WGPUStatus native_d3d12_shared_end(WGPUSharedTextureMemory,doe_resource_texture *,WGPUSharedTextureMemoryEndAccessState *);

/* Linux Vulkan owner, selected when the device backend is Vulkan. */
WGPUSharedFence native_vulkan_import_shared_fence(native_device *device, const WGPUSharedFenceDescriptor *descriptor);
WGPUSharedTextureMemory native_vulkan_import_shared_memory(native_device *device,
    const WGPUSharedTextureMemoryDescriptor *descriptor);
WGPUTexture native_vulkan_shared_create_texture(WGPUSharedTextureMemory memory, const WGPUTextureDescriptor *descriptor);
WGPUStatus native_vulkan_shared_begin(WGPUSharedTextureMemory memory, doe_resource_texture *texture,
    const WGPUSharedTextureMemoryBeginAccessDescriptor *descriptor);
WGPUStatus native_vulkan_shared_end(WGPUSharedTextureMemory memory, doe_resource_texture *texture,
    WGPUSharedTextureMemoryEndAccessState *state);
#endif
