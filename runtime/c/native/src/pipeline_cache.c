/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "doe_metal_cache.h"
#include "doe_native_pipeline_cache.h"
#include <string.h>

static uint32_t cache_status(doe_mtl_status status)
{
    switch (status) {
    case DOE_MTL_OK: return DOE_NATIVE_PIPELINE_CACHE_OK;
    case DOE_MTL_UNSUPPORTED: case DOE_MTL_LOADER_UNAVAILABLE: return DOE_NATIVE_PIPELINE_CACHE_UNSUPPORTED;
    case DOE_MTL_INVALID_ARGUMENT: case DOE_MTL_RANGE: return DOE_NATIVE_PIPELINE_CACHE_INVALID_ARGUMENT;
    case DOE_MTL_PENDING: case DOE_MTL_COMPLETION_UNKNOWN:
    case DOE_MTL_OUT_OF_MEMORY: case DOE_MTL_NATIVE_FAILURE: case DOE_MTL_INVALID_STATE:
    case DOE_MTL_BUSY: case DOE_MTL_ALIGNMENT: case DOE_MTL_DEVICE_MISMATCH: case DOE_MTL_UNINITIALIZED:
    case DOE_MTL_EXECUTION_FAILED: return DOE_NATIVE_PIPELINE_CACHE_ERROR;
    }
    return DOE_NATIVE_PIPELINE_CACHE_ERROR;
}
static uint32_t cache_operation(void *handle, uint32_t version, unsigned operation,
    const char *directory, size_t length, DoeNativePipelineCacheInfo *out, size_t out_size)
{
    if (!out || out_size != sizeof(*out) || version != DOE_NATIVE_PIPELINE_CACHE_ABI_VERSION)
        return DOE_NATIVE_PIPELINE_CACHE_INVALID_ARGUMENT;
    DoeNativePipelineCacheInfo result = {0};
    native_device *device = handle;
    if (!device || (!directory && length) || (directory && (!length || memchr(directory, 0, length)))) {
        result.status = DOE_NATIVE_PIPELINE_CACHE_INVALID_ARGUMENT;
        memcpy(result.message, "invalid pipeline cache arguments", sizeof("invalid pipeline cache arguments"));
        *out = result;
        return result.status;
    }
    native_lock(device);
    doe_mtl_error error = {0};
    doe_mtl_status status = DOE_MTL_OK;
    doe_mtl_pipeline_cache_stats stats = {0};
    if (!native_device_alive(device)) status = DOE_MTL_INVALID_STATE;
    else if (device->backend.kind != WGPUBackendType_Metal)
        status = operation == 0 && !directory ? DOE_MTL_OK : DOE_MTL_UNSUPPORTED;
    else {
        doe_mtl_context *context = device->backend.value.metal.context;
        if (operation == 0) status = doe_mtl_context_pipeline_cache(context, directory, length, &error);
        else if (operation == 1) status = doe_mtl_context_pipeline_cache_flush(context, &error);
        (void)doe_mtl_context_pipeline_cache_stats(context, &stats);
    }
    native_unlock(device);
    result.status = cache_status(status);
    result.enabled = stats.enabled; result.dirty = stats.dirty;
    result.load_attempts = stats.load_attempts; result.load_successes = stats.load_successes;
    result.additions = stats.additions; result.addition_failures = stats.addition_failures;
    result.serializations = stats.serializations; result.serialization_failures = stats.serialization_failures;
    result.native_code = error.native_code;
    if (status != DOE_MTL_OK) {
        native_failure failure = native_failure_metal(status, &error);
        char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
        native_failure_format(&failure, "pipeline cache operation", message);
        size_t count = strlen(message);
        if (count >= sizeof(result.message)) count = sizeof(result.message) - 1;
        memcpy(result.message, message, count);
    }
    *out = result;
    return result.status;
}
uint32_t doeNativeDeviceConfigurePipelineCache(void *device, uint32_t version,
    const char *directory, size_t length, DoeNativePipelineCacheInfo *out, size_t out_size)
{
    return cache_operation(device, version, 0, directory, length, out, out_size);
}
uint32_t doeNativeDeviceFlushPipelineCache(void *device, uint32_t version,
    DoeNativePipelineCacheInfo *out, size_t out_size)
{
    return cache_operation(device, version, 1, NULL, 0, out, out_size);
}
uint32_t doeNativeDevicePipelineCacheInfo(void *device, uint32_t version,
    DoeNativePipelineCacheInfo *out, size_t out_size)
{
    return cache_operation(device, version, 2, NULL, 0, out, out_size);
}
