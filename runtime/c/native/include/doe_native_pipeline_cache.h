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
#ifdef __cplusplus
}
#endif
#endif
