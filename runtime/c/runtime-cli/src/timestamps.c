/* SPDX-License-Identifier: MIT
 * One timestamp query lifetime/readback owner for compute and render passes.
 * Public WebGPU query results are nanoseconds; no host-clock substitution. */
#include "runtime_internal.h"

bool runtime_timestamp_begin(doe_runtime *runtime, runtime_timestamp *timestamp, doe_trace_execution *execution)
{
    *timestamp = (runtime_timestamp){0};
    if (!runtime->timestamps_enabled) return true;
    execution->gpu_timestamp_attempted = true;
    WGPUQuerySetDescriptor query = WGPU_QUERY_SET_DESCRIPTOR_INIT;
    query.type = WGPUQueryType_Timestamp;
    query.count = 2;
    timestamp->query = wgpuDeviceCreateQuerySet(runtime->device, &query);
    WGPUBufferDescriptor buffer = WGPU_BUFFER_DESCRIPTOR_INIT;
    buffer.size = 16;
    buffer.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
    timestamp->resolved = wgpuDeviceCreateBuffer(runtime->device, &buffer);
    timestamp->writes = WGPU_PASS_TIMESTAMP_WRITES_INIT;
    timestamp->writes.querySet = timestamp->query;
    timestamp->writes.beginningOfPassWriteIndex = 0;
    timestamp->writes.endOfPassWriteIndex = 1;
    return (timestamp->query && timestamp->resolved) ||
        runtime_fail(execution, "gpu_timestamp_create_failed", "timestamp query resources were not created");
}

void runtime_timestamp_resolve(const runtime_timestamp *timestamp, WGPUCommandEncoder encoder)
{
    if (timestamp->query) wgpuCommandEncoderResolveQuerySet(encoder, timestamp->query, 0, 2, timestamp->resolved, 0);
}

bool runtime_timestamp_finish(doe_runtime *runtime, runtime_timestamp *timestamp, bool ok, doe_trace_execution *execution)
{
    if (ok && timestamp->query) {
        uint64_t values[2] = {0};
        ok = runtime_read_back(runtime, timestamp->resolved, 0, sizeof(values), (uint8_t *)values, execution);
        if (ok && values[1] >= values[0]) {
            execution->gpu_timestamp_ns = values[1] - values[0];
            execution->gpu_timestamp_valid = true;
        } else if (ok) ok = runtime_fail(execution, "gpu_timestamp_invalid", "timestamp end precedes beginning");
    }
    if (timestamp->query) wgpuQuerySetRelease(timestamp->query);
    if (timestamp->resolved) wgpuBufferRelease(timestamp->resolved);
    *timestamp = (runtime_timestamp){0};
    return ok;
}
