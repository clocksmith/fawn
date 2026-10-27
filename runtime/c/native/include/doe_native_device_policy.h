/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_DEVICE_POLICY_H
#define DOE_NATIVE_DEVICE_POLICY_H
#include "doe_native_queue.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define DOE_NATIVE_DEVICE_POLICY_ABI_VERSION 1u
enum {
    DOE_NATIVE_QUEUE_DEFAULT = 0, DOE_NATIVE_QUEUE_PREFER_GRAPHICS_COMPUTE = 1,
    DOE_NATIVE_QUEUE_REQUIRE_COMPUTE_ONLY = 2,
    DOE_NATIVE_SYNC_FENCE_POOL = 0, DOE_NATIVE_SYNC_PREFER_TIMELINE = 1,
    DOE_NATIVE_UPLOAD_DEFAULT = 0, DOE_NATIVE_UPLOAD_STAGED_COPY = 1, DOE_NATIVE_UPLOAD_ALLOW_MAPPED = 2
};
typedef struct DoeNativeDevicePolicy {
    uint32_t queue_selection, synchronization;
    uint32_t upload_policy, subgroup_policy; /* Subgroup values 0..3 follow the backend size policy. */
    uint32_t reserved[4];
} DoeNativeDevicePolicy;
/* Per-request value snapshot. Uses the ordinary request-device callback/loss
 * delivery owner; does not mutate adapter state or select another physical device.
 * Vulkan, Metal and D3D12 policies are backend-specific; unsupported combinations fail
 * creation rather than selecting another backend or mutating the adapter. */
DOE_NATIVE_EXTENSION WGPUFuture doeNativeAdapterRequestDeviceWithPolicy(WGPUAdapter adapter,
    const WGPUDeviceDescriptor *descriptor, WGPURequestDeviceCallbackInfo callback,
    uint32_t version, const DoeNativeDevicePolicy *policy, size_t policy_size);
/* One-way per-device opt-out from Doe-owned persistent shader/driver caches.
 * Call on a live device before its first persistent cache operation. Prior use
 * (including an in-flight attempt) rejects with Error; repeated successful
 * disable is idempotent. Memory caches remain enabled. No environment mutation,
 * foreign callback or GPU work. The caller holds its ordinary device reference.
 * Independent caller-owned cache objects and OS/driver caches are outside this
 * contract. Never appears in the standard wgpuGetProcAddress inventory. */
DOE_NATIVE_EXTENSION WGPUStatus doeNativeDeviceDisablePersistentCache(WGPUDevice device);
#ifdef __cplusplus
}
#endif
#endif
