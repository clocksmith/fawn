/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_PIPELINE_CACHE_H
#define DOE_NATIVE_PIPELINE_CACHE_H
#include "doe_native_programs.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define DOE_NATIVE_PIPELINE_CACHE_ABI_VERSION 1u
typedef enum DoeNativePipelineCacheStatus {
    DOE_NATIVE_PIPELINE_CACHE_OK = 0,
    DOE_NATIVE_PIPELINE_CACHE_UNSUPPORTED = 1,
    DOE_NATIVE_PIPELINE_CACHE_INVALID_ARGUMENT = 2,
    DOE_NATIVE_PIPELINE_CACHE_ERROR = 3
} DoeNativePipelineCacheStatus;
typedef struct DoeNativePipelineCacheInfo {
    uint32_t status, enabled, dirty, reserved;
    uint64_t load_attempts, load_successes, additions, addition_failures;
    uint64_t serializations, serialization_failures;
    int64_t native_code;
    char message[256];
} DoeNativePipelineCacheInfo;
/* Optional backend-owned binary archive, Metal binary archive or Vulkan driver cache. NULL/0 disables
 * it without writing. No environment-variable mutation or default directory is
 * introduced. Source libraries still compile; native descriptors select exact
 * device binaries. Counters are archive operations, not inferred cache hits.
 * Valid output storage receives status/diagnostics even on backend failure;
 * invalid ABI/record size leaves output untouched. No WebGPU error callback is
 * fired for an optional cache failure. Existing pipeline semantics are unchanged.
 * Directory bytes need not be NUL terminated and are borrowed only for this call.
 */
DOE_NATIVE_EXTENSION uint32_t doeNativeDeviceConfigurePipelineCache(void *device,
    uint32_t abi_version, const char *directory, size_t length,
    DoeNativePipelineCacheInfo *out, size_t out_size);
DOE_NATIVE_EXTENSION uint32_t doeNativeDeviceFlushPipelineCache(void *device,
    uint32_t abi_version, DoeNativePipelineCacheInfo *out, size_t out_size);
DOE_NATIVE_EXTENSION uint32_t doeNativeDevicePipelineCacheInfo(void *device,
    uint32_t abi_version, DoeNativePipelineCacheInfo *out, size_t out_size);
/* Embedder-owned pipeline blob store, for example a browser that persists bytes
 * behind its GPU sandbox. Doe performs no file access for it and holds no Doe
 * lock while calling it. Callbacks run synchronously on the thread creating a
 * compute or render pipeline of this device and never after that call returns; the
 * embedder keeps userdata valid until the device is released. A lookup may
 * precede reuse of an identical pipeline already held in device memory; then
 * nothing is loaded into the driver or stored. load either calls
 * receive once with the stored bytes (copied before receive returns) or not at
 * all. Keys only locate blobs: each value is an enveloped driver cache whose
 * device, driver, version and digest are checked, and the driver validates its
 * own contents, so a wrong or corrupt value is a miss, never another pipeline.
 * Keys bind the WGSL source, entry point and constants, the loaded compiler and
 * runtime build IDs, the exact SPIR-V and the adapter/driver identity.
 * Configure once per Vulkan device; it is exclusive with the directory cache
 * and independent of doeNativeDeviceDisablePersistentCache, which governs only
 * Doe-owned files. Info counters: load_attempts = lookups, load_successes =
 * driver-accepted loads, serializations = values handed to store. Other
 * backends, or a runtime without ELF build IDs, return UNSUPPORTED. */
typedef void (*DoeNativePipelineBlobReceive)(void *receiver, const void *value, size_t size);
typedef struct DoeNativePipelineBlobStore {
    void *userdata;
    void (*load)(void *userdata, const void *key, size_t key_size,
        DoeNativePipelineBlobReceive receive, void *receiver);
    void (*store)(void *userdata, const void *key, size_t key_size, const void *value, size_t size);
} DoeNativePipelineBlobStore;
DOE_NATIVE_EXTENSION uint32_t doeNativeDeviceConfigurePipelineBlobStore(void *device,
    uint32_t abi_version, const DoeNativePipelineBlobStore *store, size_t store_size,
    DoeNativePipelineCacheInfo *out, size_t out_size);
#ifdef __cplusplus
}
#endif
#endif
