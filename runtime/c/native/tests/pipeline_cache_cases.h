/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_TEST_PIPELINE_CACHE_CASES_H
#define DOE_NATIVE_TEST_PIPELINE_CACHE_CASES_H
#include "doe_native_pipeline_cache.h"
static void pipeline_cache_admission(state *s, bool metal_mock)
{
    DoeNativePipelineCacheInfo info, saved;
    memset(&info, 0xa5, sizeof(info)); saved = info;
    assert(doeNativeDeviceConfigurePipelineCache(s->device, 99, "cache", 5, &info, sizeof(info)) ==
        DOE_NATIVE_PIPELINE_CACHE_INVALID_ARGUMENT && !memcmp(&info, &saved, sizeof(info)));
    assert(doeNativeDevicePipelineCacheInfo(s->device, DOE_NATIVE_PIPELINE_CACHE_ABI_VERSION,
        &info, sizeof(info)-1) == DOE_NATIVE_PIPELINE_CACHE_INVALID_ARGUMENT && !memcmp(&info, &saved, sizeof(info)));
    if (metal_mock) { /* This injected Metal adapter deliberately has no archive callbacks. */
        assert(doeNativeDeviceConfigurePipelineCache(s->device, DOE_NATIVE_PIPELINE_CACHE_ABI_VERSION,
            "cache", 5, &info, sizeof(info)) == DOE_NATIVE_PIPELINE_CACHE_UNSUPPORTED);
        assert(info.status == DOE_NATIVE_PIPELINE_CACHE_UNSUPPORTED && !info.enabled && info.message[0]);
    }
    assert(doeNativeDeviceConfigurePipelineCache(s->device, DOE_NATIVE_PIPELINE_CACHE_ABI_VERSION,
        NULL, 0, &info, sizeof(info)) == DOE_NATIVE_PIPELINE_CACHE_OK && !info.enabled);
    uint32_t expected = DOE_NATIVE_PIPELINE_CACHE_OK;
    assert(doeNativeDevicePipelineCacheInfo(s->device, DOE_NATIVE_PIPELINE_CACHE_ABI_VERSION,
        &info, sizeof(info)) == expected && !info.enabled && !info.additions && !info.serializations);
    assert(doeNativeDeviceFlushPipelineCache(s->device, DOE_NATIVE_PIPELINE_CACHE_ABI_VERSION,
        &info, sizeof(info)) == expected);
    assert(doeNativeDeviceConfigurePipelineCache(s->device, DOE_NATIVE_PIPELINE_CACHE_ABI_VERSION,
        "a\0b", 3, &info, sizeof(info)) == DOE_NATIVE_PIPELINE_CACHE_INVALID_ARGUMENT);
    assert(!s->errors);
}
#endif
