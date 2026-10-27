/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "doe_metal_cache.h"
#include "doe_vk_cache.h"
#include "../../backends/d3d12/include/doe_d3d12_cache.h"
#include "doe_native_pipeline_cache.h"
#include <string.h>

static uint32_t cache_status(native_result status)
{
    if (status == NATIVE_OK) return DOE_NATIVE_PIPELINE_CACHE_OK;
    if (status == NATIVE_UNSUPPORTED || status == NATIVE_UNAVAILABLE) return DOE_NATIVE_PIPELINE_CACHE_UNSUPPORTED;
    if (status == NATIVE_INVALID_ARGUMENT) return DOE_NATIVE_PIPELINE_CACHE_INVALID_ARGUMENT;
    return DOE_NATIVE_PIPELINE_CACHE_ERROR;
}
#define COPY_CACHE_STATS(result, stats) do { \
    (result).enabled=(stats).enabled; (result).dirty=(stats).dirty; \
    (result).load_attempts=(stats).load_attempts; (result).load_successes=(stats).load_successes; \
    (result).additions=(stats).additions; (result).addition_failures=(stats).addition_failures; \
    (result).serializations=(stats).serializations; (result).serialization_failures=(stats).serialization_failures; \
} while (0)
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
    native_device_retain(device);
    native_lock(device);
    native_failure failure = native_failure_status(NATIVE_OK);
    if (!native_device_alive(device)) failure = native_failure_status(NATIVE_INVALID_STATE);
    else if (operation == 0 &&
        !native_persistent_cache_reserve(&device->persistent_cache))
        failure = native_failure_status(NATIVE_INVALID_STATE);
    else if (device->backend.kind == WGPUBackendType_Vulkan) {
        doe_vk_context *context = device->backend.value.vulkan.context;
        doe_vk_error error = {0}; doe_vk_status status = DOE_VK_OK;
        doe_vk_pipeline_cache_stats stats = {0};
        if (operation == 0) status = doe_vk_context_pipeline_cache(context, directory, length, &error);
        else if (operation == 1) status = doe_vk_context_pipeline_cache_flush(context, &error);
        (void)doe_vk_context_pipeline_cache_stats(context, &stats);
        COPY_CACHE_STATS(result, stats);
        failure = native_failure_vulkan(status, &error);
    } else if (device->backend.kind == WGPUBackendType_Metal) {
        doe_mtl_context *context = device->backend.value.metal.context;
        doe_mtl_error error = {0}; doe_mtl_status status = DOE_MTL_OK;
        doe_mtl_pipeline_cache_stats stats = {0};
        if (operation == 0) status = doe_mtl_context_pipeline_cache(context, directory, length, &error);
        else if (operation == 1) status = doe_mtl_context_pipeline_cache_flush(context, &error);
        (void)doe_mtl_context_pipeline_cache_stats(context, &stats);
        COPY_CACHE_STATS(result, stats);
        failure = native_failure_metal(status, &error);
    } else if (device->backend.kind == WGPUBackendType_D3D12) {
#if defined(_WIN32)
        doe_dx_context *context = device->backend.value.d3d12.context;
        doe_dx_error error = {0}; doe_dx_status status = DOE_DX_OK;
        doe_dx_pipeline_cache_stats stats = {0};
        if (operation == 0) status = doe_dx_context_pipeline_cache(context,directory,length,&error);
        else if (operation == 1) status = doe_dx_context_pipeline_cache_flush(context,&error);
        (void)doe_dx_context_pipeline_cache_stats(context,&stats);
        COPY_CACHE_STATS(result,stats);
        failure = native_failure_d3d12(status,&error);
#else
        failure = native_failure_status(NATIVE_UNSUPPORTED);
#endif
    } else if (operation != 0 || directory) failure = native_failure_status(NATIVE_UNSUPPORTED);
    native_unlock(device);
    native_device_release(device);
    result.status = cache_status(failure.status);
    result.native_code = failure.native_code;
    if (failure.status != NATIVE_OK) {
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
