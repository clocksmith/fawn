/* SPDX-License-Identifier: MIT
 * Doe fused compute command extensions (doe_native_commands.h).
 * The constructors record through the same resource encoder, compute pass and
 * copy rules as the public encoder calls, under one device lock, and publish
 * only a successful Finish: a rejected recording is never returned as an
 * invalid command buffer. */
#include "internal.h"
#include "doe_native_commands.h"
#include <stdio.h>
#include <stdlib.h>

static const char *status_name(doe_resource_status status)
{
    static const char *const names[] = {"OK", "InvalidArgument", "OutOfMemory", "Overflow", "Unsupported",
        "DeviceMismatch", "Unavailable", "Usage", "Alignment", "Range", "Alias", "Layout", "Aspect", "State",
        "Pending", "ExecutionFailed"};
    return (size_t)status < sizeof(names) / sizeof(names[0]) ? names[status] : "Unknown";
}

static WGPUCommandBuffer construction_failure(native_device *device, doe_resource_status status)
{
    char text[128];
    snprintf(text, sizeof(text), "compute command construction failed: %s", status_name(status));
    /* Out of memory is reported as such; every other rejection is validation. */
    native_error(device, status == DOE_RESOURCE_OUT_OF_MEMORY ? WGPUErrorType_OutOfMemory : WGPUErrorType_Validation, text);
    return NULL;
}

typedef struct fused_dispatch {
    WGPUComputePipeline pipeline;
    WGPUBindGroup *groups; /* group_count entries; NULL entries leave the slot unbound. */
    uint32_t group_count;
    const uint32_t *workgroups;
} fused_dispatch;

static doe_resource_status record_dispatch(native_device *device, doe_resource_compute_pass *pass,
    const fused_dispatch *dispatch)
{
    if (!dispatch->pipeline || dispatch->group_count > DOE_NATIVE_DISPATCH_BIND_GROUP_STRIDE ||
            (dispatch->group_count && !dispatch->groups))
        return DOE_RESOURCE_INVALID_ARGUMENT;
    doe_resource_status status = doe_resource_compute_pass_set_pipeline(pass, (doe_resource_pipeline *)dispatch->pipeline);
    for (uint32_t index = 0; status == DOE_RESOURCE_OK && index < dispatch->group_count; ++index) {
        if (!dispatch->groups[index]) continue;
        const doe_resource_dispatch_group binding = {(doe_resource_bind_group *)dispatch->groups[index], NULL, 0};
        status = doe_resource_compute_pass_set_group(pass, index, &binding);
    }
    if (status != DOE_RESOURCE_OK) return status;
    /* The dispatchWorkgroups device limit, as wgpuComputePassEncoderDispatchWorkgroups. */
    const uint32_t bound = device->backend.limits.maxComputeWorkgroupsPerDimension;
    const uint32_t *size = dispatch->workgroups;
    if (size[0] > bound || size[1] > bound || size[2] > bound) return DOE_RESOURCE_RANGE;
    return doe_resource_compute_pass_dispatch(pass, size[0], size[1], size[2]);
}

static doe_resource_status record(native_device *device, size_t count, const fused_dispatch *dispatches,
    const doe_resource_buffer_copy *copy, doe_resource_commands **out)
{
    doe_resource_encoder *encoder = NULL;
    doe_resource_compute_pass *pass = NULL;
    doe_resource_status status = doe_resource_encoder_create(&device->resources, &encoder);
    if (status == DOE_RESOURCE_OK) status = doe_resource_encoder_begin_compute(encoder, &pass);
    for (size_t index = 0; status == DOE_RESOURCE_OK && index < count; ++index)
        status = record_dispatch(device, pass, &dispatches[index]);
    if (status == DOE_RESOURCE_OK) status = doe_resource_compute_pass_end(pass);
    if (status == DOE_RESOURCE_OK) status = doe_resource_encoder_copy_buffer(encoder, copy, DOE_RESOURCE_DISTINCT_BUFFERS);
    if (status == DOE_RESOURCE_OK) status = doe_resource_encoder_finish(encoder, out);
    /* The finished commands hold their own leases on every recorded input. */
    if (pass) doe_resource_compute_pass_release(pass);
    if (encoder) doe_resource_encoder_release(encoder);
    return status;
}

static WGPUCommandBuffer construct(native_device *device, size_t count, const fused_dispatch *dispatches,
    WGPUBuffer source, uint64_t source_offset, WGPUBuffer destination, uint64_t destination_offset, uint64_t size)
{
    if (!source || !destination) return construction_failure(device, DOE_RESOURCE_INVALID_ARGUMENT);
    const doe_resource_buffer_copy copy = {(doe_resource_buffer *)source, (doe_resource_buffer *)destination,
        source_offset, destination_offset, size};
    doe_resource_commands *commands = NULL;
    native_lock(device);
    const doe_resource_status status = record(device, count, dispatches, &copy, &commands);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) return construction_failure(device, status);
    return (WGPUCommandBuffer)commands;
}

WGPUCommandBuffer doeNativeCreateComputeDispatchCopyCommandBuffer(WGPUDevice handle, WGPUComputePipeline pipeline,
    WGPUBindGroup *groups, uint32_t group_count, uint32_t x, uint32_t y, uint32_t z, WGPUBuffer source,
    uint64_t source_offset, WGPUBuffer destination, uint64_t destination_offset, uint64_t size)
{
    native_device *device = (native_device *)handle;
    if (!device) return NULL;
    const uint32_t workgroups[3] = {x, y, z};
    const fused_dispatch dispatch = {pipeline, groups, group_count, workgroups};
    return construct(device, 1, &dispatch, source, source_offset, destination, destination_offset, size);
}

WGPUCommandBuffer doeNativeCreateComputeDispatchBatchCopyCommandBuffer(WGPUDevice handle, size_t dispatch_count,
    WGPUComputePipeline *pipelines, WGPUBindGroup *groups, const uint32_t *group_counts, const uint32_t *dimensions,
    WGPUBuffer source, uint64_t source_offset, WGPUBuffer destination, uint64_t destination_offset, uint64_t size)
{
    native_device *device = (native_device *)handle;
    if (!device || !dispatch_count) return NULL;
    if (!pipelines || !groups || !group_counts || !dimensions ||
            dispatch_count > SIZE_MAX / sizeof(fused_dispatch) / DOE_NATIVE_DISPATCH_BIND_GROUP_STRIDE)
        return construction_failure(device, DOE_RESOURCE_INVALID_ARGUMENT);
    fused_dispatch *dispatches = malloc(dispatch_count * sizeof(*dispatches));
    if (!dispatches) return construction_failure(device, DOE_RESOURCE_OUT_OF_MEMORY);
    for (size_t index = 0; index < dispatch_count; ++index)
        dispatches[index] = (fused_dispatch){pipelines[index], groups + index * DOE_NATIVE_DISPATCH_BIND_GROUP_STRIDE,
            group_counts[index], dimensions + 3 * index};
    WGPUCommandBuffer commands = construct(device, dispatch_count, dispatches, source, source_offset,
        destination, destination_offset, size);
    free(dispatches);
    return commands;
}
