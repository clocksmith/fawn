/* SPDX-License-Identifier: MIT
 * Retained Doe ABI additions from runtime/zig/src/dropin; not a newer webgpu.h.
 * In particular the uncaptured callback omits the pinned callback's device arg. */
#ifndef DOE_NATIVE_LEGACY_H
#define DOE_NATIVE_LEGACY_H
#include "webgpu.h"
#ifdef __cplusplus
extern "C" {
#endif
#if defined(__GNUC__) || defined(__clang__)
#define DOE_LEGACY_API __attribute__((visibility("default")))
#else
#define DOE_LEGACY_API
#endif
typedef void (*DoeLegacyUncapturedErrorCallback)(uint32_t type, WGPUStringView message,
    void *userdata1, void *userdata2);
/* Returned instance/adapter/device handles carry one caller-owned reference. */
DOE_LEGACY_API WGPUDevice wgpuAdapterCreateDevice(WGPUAdapter adapter, const WGPUDeviceDescriptor *descriptor);
DOE_LEGACY_API WGPUInstance wgpuAdapterGetInstance(WGPUAdapter adapter);
DOE_LEGACY_API WGPUAdapter wgpuDeviceGetAdapter(WGPUDevice device);
/* Replacement preserves the descriptor's callback mode. Loss already observed
 * cannot be replaced. Uncaptured NULL disables both descriptor and legacy handlers. */
DOE_LEGACY_API void wgpuDeviceSetDeviceLostCallback(WGPUDevice device, WGPUDeviceLostCallback callback,
    void *userdata1, void *userdata2);
DOE_LEGACY_API void wgpuDeviceSetUncapturedErrorCallback(WGPUDevice device,
    DoeLegacyUncapturedErrorCallback callback, void *userdata1, void *userdata2);
/* Nonblocking progress; retained ABI returns false, not a completion verdict. */
DOE_LEGACY_API WGPUBool wgpuDeviceTick(WGPUDevice device);
DOE_LEGACY_API WGPUExternalTexture wgpuDeviceCreateExternalTexture(WGPUDevice device, const void *descriptor);
DOE_LEGACY_API void wgpuExternalTextureDestroy(WGPUExternalTexture texture);
DOE_LEGACY_API void wgpuExternalTextureExpire(WGPUExternalTexture texture);
DOE_LEGACY_API void wgpuExternalTextureRefresh(WGPUExternalTexture texture);
/* Unadvertised Dawn capability ABI: rejects without modifying caller output.
 * These default-empty array outputs have no producer in Doe. */
typedef struct DoeLegacyFormatCapabilities { void *nextInChain; } DoeLegacyFormatCapabilities;
typedef struct DoeLegacyCapabilityArray {
    WGPUChainedStruct chain;
    size_t count;
    const void *members;
} DoeLegacyCapabilityArray;
DOE_LEGACY_API WGPUStatus wgpuAdapterGetFormatCapabilities(WGPUAdapter adapter,
    WGPUTextureFormat format, DoeLegacyFormatCapabilities *capabilities);
DOE_LEGACY_API void wgpuAdapterPropertiesMemoryHeapsFreeMembers(DoeLegacyCapabilityArray properties);
DOE_LEGACY_API void wgpuAdapterPropertiesSubgroupMatrixConfigsFreeMembers(DoeLegacyCapabilityArray properties);
DOE_LEGACY_API void wgpuDawnDrmFormatCapabilitiesFreeMembers(DoeLegacyCapabilityArray properties);
DOE_LEGACY_API void wgpuDeviceValidateTextureDescriptor(WGPUDevice device, const WGPUTextureDescriptor *descriptor);
/* Invalid objects own normal resource identity but never allocate native storage. */
DOE_LEGACY_API WGPUBuffer wgpuDeviceCreateErrorBuffer(WGPUDevice device, const WGPUBufferDescriptor *descriptor);
DOE_LEGACY_API WGPUTexture wgpuDeviceCreateErrorTexture(WGPUDevice device, const WGPUTextureDescriptor *descriptor);
/* Historical immediate mapped write, NOT a recorded command. Requires a writable
 * mapped range under the ordinary buffer owner; encoder is not consumed. */
DOE_LEGACY_API void wgpuCommandEncoderWriteBuffer(WGPUCommandEncoder encoder, WGPUBuffer buffer,
    uint64_t offset, const uint8_t *data, uint64_t size);
/* Retained rejecting compatibility: descriptor timestampWrites is the supported
 * pass-boundary route. These invalidate an active parent without recording. */
DOE_LEGACY_API void wgpuComputePassEncoderWriteTimestamp(WGPUComputePassEncoder pass, WGPUQuerySet set, uint32_t index);
DOE_LEGACY_API void wgpuRenderPassEncoderWriteTimestamp(WGPURenderPassEncoder pass, WGPUQuerySet set, uint32_t index);
typedef struct DoeLegacyCopyTextureForBrowserOptions {
    const WGPUChainedStruct *nextInChain;
    WGPUBool flipY;
    WGPUBool needsColorSpaceConversion;
    uint32_t srcAlphaMode;
    const float *srcTransferFunctionParameters;
    const float *conversionMatrix;
    const float *dstTransferFunctionParameters;
    uint32_t dstAlphaMode;
    WGPUBool internalUsage;
} DoeLegacyCopyTextureForBrowserOptions;
typedef struct DoeLegacyImageCopyExternalTexture {
    const WGPUChainedStruct *nextInChain;
    WGPUExternalTexture externalTexture;
    WGPUOrigin3D origin;
    struct { uint32_t width, height; } naturalSize;
} DoeLegacyImageCopyExternalTexture;
/* Exact retained byte-copy/flipY contract; no YUV/color conversion is performed.
 * Unsupported conversion requests fail, rather than masquerading as conversion. */
DOE_LEGACY_API void wgpuQueueCopyTextureForBrowser(WGPUQueue queue,
    const WGPUTexelCopyTextureInfo *source, const WGPUTexelCopyTextureInfo *destination,
    const WGPUExtent3D *size, const DoeLegacyCopyTextureForBrowserOptions *options);
DOE_LEGACY_API void wgpuQueueCopyExternalTextureForBrowser(WGPUQueue queue,
    const DoeLegacyImageCopyExternalTexture *source, const WGPUTexelCopyTextureInfo *destination,
    const WGPUExtent3D *size, const DoeLegacyCopyTextureForBrowserOptions *options);
DOE_LEGACY_API void wgpuQueueCopyExternalImageToTexture(WGPUQueue queue,
    const DoeLegacyImageCopyExternalTexture *source, const WGPUTexelCopyTextureInfo *destination,
    const WGPUExtent3D *size);
#ifdef __cplusplus
}
#endif
#endif
