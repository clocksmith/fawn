/* SPDX-License-Identifier: MIT
 * Optional persistence is owned by the native backend, never a second cache. */
#include "runtime_internal.h"
#include "host_io.h"
#include <inttypes.h>
#include <string.h>

void runtime_pipeline_cache_open(doe_runtime *runtime)
{
    runtime->pipeline_cache_reason = "default";
    if (runtime->options.no_pipeline_cache) {
        runtime->pipeline_cache_reason = "cli-flag";
        return;
    }
    const char *directory = runtime->options.pipeline_cache_dir;
    if (!directory) return; /* Preserve the C runtime's no-persistence default. */
#ifdef DOE_RUNTIME_DAWN
    runtime->pipeline_cache.status = DOE_NATIVE_PIPELINE_CACHE_UNSUPPORTED;
    runtime->pipeline_cache_reason = "non-doe-backend";
#else
    uint32_t status = doeNativeDeviceConfigurePipelineCache(runtime->device,
        DOE_NATIVE_PIPELINE_CACHE_ABI_VERSION, directory, strlen(directory),
        &runtime->pipeline_cache, sizeof(runtime->pipeline_cache));
    runtime->pipeline_cache_reason = status == DOE_NATIVE_PIPELINE_CACHE_UNSUPPORTED ?
        "platform-unsupported" : status == DOE_NATIVE_PIPELINE_CACHE_OK ? "cli-flag" : "unknown";
#endif
}

void doe_runtime_flush_pipeline_cache(doe_runtime *runtime)
{
#ifndef DOE_RUNTIME_DAWN
    if (!runtime || !runtime->pipeline_cache.enabled) return;
    uint64_t start = doe_host_monotonic_ns();
    doeNativeDeviceFlushPipelineCache(runtime->device, DOE_NATIVE_PIPELINE_CACHE_ABI_VERSION,
        &runtime->pipeline_cache, sizeof(runtime->pipeline_cache));
    uint64_t end = doe_host_monotonic_ns();
    runtime->pipeline_cache_flush_ns = end >= start ? end - start : 0;
#else
    (void)runtime;
#endif
}

bool doe_runtime_write_pipeline_cache(FILE *out, const doe_runtime *runtime)
{
    const DoeNativePipelineCacheInfo empty = {0};
    const DoeNativePipelineCacheInfo *info = runtime ? &runtime->pipeline_cache : &empty;
    const char *reason = runtime && runtime->pipeline_cache_reason ? runtime->pipeline_cache_reason : "default";
    return fprintf(out, ",\"pipelineCache\":{\"state\":\"%s\",\"reason\":\"%s\","
        "\"warmupCount\":0,\"warmupNs\":0,\"flushNs\":%" PRIu64 ",\"nativeStatus\":%u,"
        "\"dirty\":%s,\"loadAttempts\":%" PRIu64 ",\"loadSuccesses\":%" PRIu64 ","
        "\"additions\":%" PRIu64 ",\"additionFailures\":%" PRIu64 ","
        "\"serializations\":%" PRIu64 ",\"serializationFailures\":%" PRIu64 ","
        "\"nativeCode\":%" PRId64 ",\"message\":",
        info->enabled ? "enabled" : "disabled", reason, runtime ? runtime->pipeline_cache_flush_ns : 0,
        info->status, info->dirty ? "true" : "false", info->load_attempts, info->load_successes,
        info->additions, info->addition_failures, info->serializations, info->serialization_failures,
        info->native_code) >= 0 && doe_trace_write_string(out, info->message) && fputc('}', out) != EOF;
}
