/* SPDX-License-Identifier: MIT
 * Browser ABI: pinned Dawn webgpu.h (Chromium 4a6fb385 / Dawn 758e5e6a).
 * The explicitly scoped implementation below is not every Dawn import mode. */
#ifndef DOE_NATIVE_SHARED_MEMORY_H
#define DOE_NATIVE_SHARED_MEMORY_H
#include "doe_native_legacy.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct native_shared_fence *WGPUSharedFence;
typedef struct native_shared_texture_memory *WGPUSharedTextureMemory;
typedef void *WGPUSharedBufferMemory;
enum {
    DOE_SHARED_FEATURE_IOSURFACE = 0x00050024,
    DOE_SHARED_FEATURE_MTL_EVENT = 0x0005002A,
    DOE_SHARED_STYPE_IOSURFACE = 0x00050023,
    DOE_SHARED_STYPE_MTL_EVENT_DESCRIPTOR = 0x00050032,
    DOE_SHARED_STYPE_MTL_EVENT_EXPORT = 0x00050033,
    DOE_SHARED_FENCE_MTL_EVENT = 5,
    /* Linux Vulkan interop (shared_memory_vulkan.c). Slots are private feature
     * bit positions, distinct from every core feature value. */
    DOE_SHARED_FEATURE_OPAQUE_FD = 0x00050020,
    DOE_SHARED_FEATURE_SYNC_FD = 0x00050027,
    DOE_SHARED_FEATURE_SLOT_OPAQUE_FD = 26,
    DOE_SHARED_FEATURE_SLOT_SYNC_FD = 27,
    DOE_SHARED_FEATURE_OPAQUE_SEMAPHORE = 0x00050026,
    DOE_SHARED_FEATURE_SLOT_OPAQUE_SEMAPHORE = 28,
    DOE_SHARED_STYPE_OPAQUE_SEMAPHORE_DESCRIPTOR = 0x0005002A,
    DOE_SHARED_STYPE_OPAQUE_SEMAPHORE_EXPORT = 0x0005002B,
    DOE_SHARED_FENCE_OPAQUE_SEMAPHORE = 1,
    DOE_SHARED_STYPE_OPAQUE_FD = 0x0005001F,
    DOE_SHARED_STYPE_VK_LAYOUT_BEGIN = 0x00050027,
    DOE_SHARED_STYPE_VK_LAYOUT_END = 0x00050028,
    DOE_SHARED_STYPE_SYNC_FD_DESCRIPTOR = 0x0005002C,
    DOE_SHARED_STYPE_SYNC_FD_EXPORT = 0x0005002D,
    DOE_SHARED_FENCE_SYNC_FD = 2
};
typedef struct WGPUSharedFenceDescriptor {
    const WGPUChainedStruct *nextInChain;
    WGPUStringView label;
} WGPUSharedFenceDescriptor;
typedef struct WGPUSharedFenceMTLSharedEventDescriptor {
    WGPUChainedStruct chain;
    void *sharedEvent;
} WGPUSharedFenceMTLSharedEventDescriptor;
typedef struct WGPUSharedFenceExportInfo {
    WGPUChainedStruct *nextInChain;
    uint32_t type;
} WGPUSharedFenceExportInfo;
typedef struct WGPUSharedFenceMTLSharedEventExportInfo {
    WGPUChainedStruct chain;
    void *sharedEvent;
} WGPUSharedFenceMTLSharedEventExportInfo;
typedef struct WGPUSharedTextureMemoryDescriptor {
    const WGPUChainedStruct *nextInChain;
    WGPUStringView label;
} WGPUSharedTextureMemoryDescriptor;
typedef struct WGPUSharedTextureMemoryIOSurfaceDescriptor {
    WGPUChainedStruct chain;
    void *ioSurface;
    WGPUBool allowStorageBinding;
} WGPUSharedTextureMemoryIOSurfaceDescriptor;
typedef struct WGPUSharedFenceSyncFDDescriptor {
    WGPUChainedStruct chain;
    int handle;
} WGPUSharedFenceSyncFDDescriptor;
typedef struct WGPUSharedFenceVkSemaphoreOpaqueFDDescriptor {
    WGPUChainedStruct chain;
    int handle;
} WGPUSharedFenceVkSemaphoreOpaqueFDDescriptor;
typedef struct WGPUSharedFenceVkSemaphoreOpaqueFDExportInfo {
    WGPUChainedStruct chain;
    int handle;
} WGPUSharedFenceVkSemaphoreOpaqueFDExportInfo;
typedef struct WGPUSharedFenceSyncFDExportInfo {
    WGPUChainedStruct chain;
    int handle;
} WGPUSharedFenceSyncFDExportInfo;
typedef struct WGPUSharedTextureMemoryOpaqueFDDescriptor {
    WGPUChainedStruct chain;
    const void *vkImageCreateInfo;
    int memoryFD;
    uint32_t memoryTypeIndex;
    uint64_t allocationSize;
    WGPUBool dedicatedAllocation;
} WGPUSharedTextureMemoryOpaqueFDDescriptor;
typedef struct WGPUSharedTextureMemoryVkImageLayoutBeginState {
    WGPUChainedStruct chain;
    int32_t oldLayout;
    int32_t newLayout;
} WGPUSharedTextureMemoryVkImageLayoutBeginState;
typedef struct WGPUSharedTextureMemoryVkImageLayoutEndState {
    WGPUChainedStruct chain;
    int32_t oldLayout;
    int32_t newLayout;
} WGPUSharedTextureMemoryVkImageLayoutEndState;
typedef struct WGPUSharedTextureMemoryBeginAccessDescriptor {
    WGPUChainedStruct *nextInChain;
    WGPUBool concurrentRead, initialized;
    size_t fenceCount;
    const WGPUSharedFence *fences;
    size_t signaledValueCount;
    const uint64_t *signaledValues;
} WGPUSharedTextureMemoryBeginAccessDescriptor;
typedef struct WGPUSharedTextureMemoryEndAccessState {
    WGPUChainedStruct *nextInChain;
    WGPUBool initialized;
    size_t fenceCount;
    const WGPUSharedFence *fences;
    size_t signaledValueCount;
    const uint64_t *signaledValues;
} WGPUSharedTextureMemoryEndAccessState;
typedef struct WGPUSharedTextureMemoryProperties {
    WGPUChainedStruct *nextInChain;
    WGPUTextureUsage usage;
    WGPUExtent3D size;
    WGPUTextureFormat format;
} WGPUSharedTextureMemoryProperties;
typedef struct WGPUSharedBufferMemoryDescriptor {
    const WGPUChainedStruct *nextInChain;
    WGPUStringView label;
} WGPUSharedBufferMemoryDescriptor;
typedef struct WGPUSharedBufferMemoryBeginAccessDescriptor {
    WGPUChainedStruct *nextInChain;
    WGPUBool initialized;
    size_t fenceCount;
    const WGPUSharedFence *fences;
    size_t signaledValueCount;
    const uint64_t *signaledValues;
} WGPUSharedBufferMemoryBeginAccessDescriptor;
typedef WGPUSharedTextureMemoryEndAccessState WGPUSharedBufferMemoryEndAccessState;
typedef struct WGPUSharedBufferMemoryProperties {
    WGPUChainedStruct *nextInChain;
    WGPUBufferUsage usage;
    uint64_t size;
} WGPUSharedBufferMemoryProperties;
/* Imports borrow input native handles and return +1 objects. Fence export
 * borrows its unchanged MTLSharedEvent identity until the fence is released.
 * Texture resources and their views/commands use the canonical resource owner;
 * each imported texture retains its shared-memory provenance.
 * Compatibility is the retained Doe subset, not all Dawn shared-image policy:
 * Begin ignores concurrentRead/initialized/nextInChain and serializes one access;
 * producer synchronization comes from equal-length fence/value spans. End(NULL) merely
 * closes access, while End(state) emits a fresh native signal and initialized=true.
 * No undefined-content initialization or concurrent-read guarantee is implied.
 * allowStorageBinding is retained ABI only; storage usage is not admitted. */
DOE_LEGACY_API WGPUSharedFence wgpuDeviceImportSharedFence(WGPUDevice device, const WGPUSharedFenceDescriptor *descriptor);
DOE_LEGACY_API void wgpuSharedFenceExportInfo(WGPUSharedFence fence, WGPUSharedFenceExportInfo *info);
DOE_LEGACY_API void wgpuSharedFenceAddRef(WGPUSharedFence fence);
DOE_LEGACY_API void wgpuSharedFenceRelease(WGPUSharedFence fence);
DOE_LEGACY_API void wgpuSharedFenceSetLabel(WGPUSharedFence fence, WGPUStringView label);
DOE_LEGACY_API WGPUSharedTextureMemory wgpuDeviceImportSharedTextureMemory(WGPUDevice device, const WGPUSharedTextureMemoryDescriptor *descriptor);
DOE_LEGACY_API WGPUStatus wgpuSharedTextureMemoryBeginAccess(WGPUSharedTextureMemory memory, WGPUTexture texture, const WGPUSharedTextureMemoryBeginAccessDescriptor *descriptor);
DOE_LEGACY_API WGPUTexture wgpuSharedTextureMemoryCreateTexture(WGPUSharedTextureMemory memory, const WGPUTextureDescriptor *descriptor);
DOE_LEGACY_API WGPUStatus wgpuSharedTextureMemoryEndAccess(WGPUSharedTextureMemory memory, WGPUTexture texture, WGPUSharedTextureMemoryEndAccessState *state);
DOE_LEGACY_API WGPUStatus wgpuSharedTextureMemoryGetProperties(WGPUSharedTextureMemory memory, WGPUSharedTextureMemoryProperties *properties);
DOE_LEGACY_API WGPUBool wgpuSharedTextureMemoryIsDeviceLost(WGPUSharedTextureMemory memory);
DOE_LEGACY_API void wgpuSharedTextureMemorySetLabel(WGPUSharedTextureMemory memory, WGPUStringView label);
DOE_LEGACY_API void wgpuSharedTextureMemoryAddRef(WGPUSharedTextureMemory memory);
DOE_LEGACY_API void wgpuSharedTextureMemoryRelease(WGPUSharedTextureMemory memory);
DOE_LEGACY_API void wgpuSharedTextureMemoryEndAccessStateFreeMembers(WGPUSharedTextureMemoryEndAccessState state);
/* Retained shared-buffer APIs are explicitly unsupported: no object or buffer
 * can be created. EndAccess/GetProperties clear only their documented fields. */
DOE_LEGACY_API WGPUSharedBufferMemory wgpuDeviceImportSharedBufferMemory(WGPUDevice device, const WGPUSharedBufferMemoryDescriptor *descriptor);
DOE_LEGACY_API WGPUStatus wgpuSharedBufferMemoryBeginAccess(WGPUSharedBufferMemory memory, WGPUBuffer buffer, const WGPUSharedBufferMemoryBeginAccessDescriptor *descriptor);
DOE_LEGACY_API WGPUBuffer wgpuSharedBufferMemoryCreateBuffer(WGPUSharedBufferMemory memory, const WGPUBufferDescriptor *descriptor);
DOE_LEGACY_API WGPUStatus wgpuSharedBufferMemoryEndAccess(WGPUSharedBufferMemory memory, WGPUBuffer buffer, WGPUSharedBufferMemoryEndAccessState *state);
DOE_LEGACY_API WGPUStatus wgpuSharedBufferMemoryGetProperties(WGPUSharedBufferMemory memory, WGPUSharedBufferMemoryProperties *properties);
DOE_LEGACY_API WGPUBool wgpuSharedBufferMemoryIsDeviceLost(WGPUSharedBufferMemory memory);
DOE_LEGACY_API void wgpuSharedBufferMemorySetLabel(WGPUSharedBufferMemory memory, WGPUStringView label);
DOE_LEGACY_API void wgpuSharedBufferMemoryAddRef(WGPUSharedBufferMemory memory);
DOE_LEGACY_API void wgpuSharedBufferMemoryRelease(WGPUSharedBufferMemory memory);
DOE_LEGACY_API void wgpuSharedBufferMemoryEndAccessStateFreeMembers(WGPUSharedBufferMemoryEndAccessState state);
#ifdef __cplusplus
}
#endif
#endif
