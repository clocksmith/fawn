/* SPDX-License-Identifier: MIT
 * Explicit inventory of implemented public entrypoints. This table does not
 * manufacture missing API symbols or resolve through another provider. */
#include "internal.h"
#include <string.h>

typedef struct native_procedure { const char *name; WGPUProc procedure; } native_procedure;
static const native_procedure procedures[] = {
    {"wgpuAdapterAddRef", (WGPUProc)wgpuAdapterAddRef},
    {"wgpuAdapterGetFeatures", (WGPUProc)wgpuAdapterGetFeatures},
    {"wgpuAdapterGetInfo", (WGPUProc)wgpuAdapterGetInfo},
    {"wgpuAdapterGetLimits", (WGPUProc)wgpuAdapterGetLimits},
    {"wgpuAdapterHasFeature", (WGPUProc)wgpuAdapterHasFeature},
    {"wgpuAdapterInfoFreeMembers", (WGPUProc)wgpuAdapterInfoFreeMembers},
    {"wgpuAdapterRelease", (WGPUProc)wgpuAdapterRelease},
    {"wgpuAdapterRequestDevice", (WGPUProc)wgpuAdapterRequestDevice},
    {"wgpuBindGroupAddRef", (WGPUProc)wgpuBindGroupAddRef},
    {"wgpuBindGroupLayoutAddRef", (WGPUProc)wgpuBindGroupLayoutAddRef},
    {"wgpuBindGroupLayoutRelease", (WGPUProc)wgpuBindGroupLayoutRelease},
    {"wgpuBindGroupRelease", (WGPUProc)wgpuBindGroupRelease},
    {"wgpuBufferAddRef", (WGPUProc)wgpuBufferAddRef},
    {"wgpuBufferDestroy", (WGPUProc)wgpuBufferDestroy},
    {"wgpuBufferGetConstMappedRange", (WGPUProc)wgpuBufferGetConstMappedRange},
    {"wgpuBufferGetMapState", (WGPUProc)wgpuBufferGetMapState},
    {"wgpuBufferGetMappedRange", (WGPUProc)wgpuBufferGetMappedRange},
    {"wgpuBufferGetSize", (WGPUProc)wgpuBufferGetSize},
    {"wgpuBufferGetUsage", (WGPUProc)wgpuBufferGetUsage},
    {"wgpuBufferMapAsync", (WGPUProc)wgpuBufferMapAsync},
    {"wgpuBufferReadMappedRange", (WGPUProc)wgpuBufferReadMappedRange},
    {"wgpuBufferRelease", (WGPUProc)wgpuBufferRelease},
    {"wgpuBufferUnmap", (WGPUProc)wgpuBufferUnmap},
    {"wgpuBufferWriteMappedRange", (WGPUProc)wgpuBufferWriteMappedRange},
    {"wgpuCommandBufferAddRef", (WGPUProc)wgpuCommandBufferAddRef},
    {"wgpuCommandBufferRelease", (WGPUProc)wgpuCommandBufferRelease},
    {"wgpuCommandEncoderAddRef", (WGPUProc)wgpuCommandEncoderAddRef},
    {"wgpuCommandEncoderBeginComputePass", (WGPUProc)wgpuCommandEncoderBeginComputePass},
    {"wgpuCommandEncoderClearBuffer", (WGPUProc)wgpuCommandEncoderClearBuffer},
    {"wgpuCommandEncoderCopyBufferToBuffer", (WGPUProc)wgpuCommandEncoderCopyBufferToBuffer},
    {"wgpuCommandEncoderFinish", (WGPUProc)wgpuCommandEncoderFinish},
    {"wgpuCommandEncoderRelease", (WGPUProc)wgpuCommandEncoderRelease},
    {"wgpuComputePassEncoderAddRef", (WGPUProc)wgpuComputePassEncoderAddRef},
    {"wgpuComputePassEncoderDispatchWorkgroups", (WGPUProc)wgpuComputePassEncoderDispatchWorkgroups},
    {"wgpuComputePassEncoderEnd", (WGPUProc)wgpuComputePassEncoderEnd},
    {"wgpuComputePassEncoderRelease", (WGPUProc)wgpuComputePassEncoderRelease},
    {"wgpuComputePassEncoderSetBindGroup", (WGPUProc)wgpuComputePassEncoderSetBindGroup},
    {"wgpuComputePassEncoderSetPipeline", (WGPUProc)wgpuComputePassEncoderSetPipeline},
    {"wgpuComputePipelineAddRef", (WGPUProc)wgpuComputePipelineAddRef},
    {"wgpuComputePipelineGetBindGroupLayout", (WGPUProc)wgpuComputePipelineGetBindGroupLayout},
    {"wgpuComputePipelineRelease", (WGPUProc)wgpuComputePipelineRelease},
    {"wgpuCreateInstance", (WGPUProc)wgpuCreateInstance},
    {"wgpuDeviceAddRef", (WGPUProc)wgpuDeviceAddRef},
    {"wgpuDeviceCreateBindGroup", (WGPUProc)wgpuDeviceCreateBindGroup},
    {"wgpuDeviceCreateBindGroupLayout", (WGPUProc)wgpuDeviceCreateBindGroupLayout},
    {"wgpuDeviceCreateBuffer", (WGPUProc)wgpuDeviceCreateBuffer},
    {"wgpuDeviceCreateCommandEncoder", (WGPUProc)wgpuDeviceCreateCommandEncoder},
    {"wgpuDeviceCreateComputePipeline", (WGPUProc)wgpuDeviceCreateComputePipeline},
    {"wgpuDeviceCreatePipelineLayout", (WGPUProc)wgpuDeviceCreatePipelineLayout},
    {"wgpuDeviceCreateShaderModule", (WGPUProc)wgpuDeviceCreateShaderModule},
    {"wgpuDeviceDestroy", (WGPUProc)wgpuDeviceDestroy},
    {"wgpuDeviceGetAdapterInfo", (WGPUProc)wgpuDeviceGetAdapterInfo},
    {"wgpuDeviceGetFeatures", (WGPUProc)wgpuDeviceGetFeatures},
    {"wgpuDeviceGetLimits", (WGPUProc)wgpuDeviceGetLimits},
    {"wgpuDeviceGetQueue", (WGPUProc)wgpuDeviceGetQueue},
    {"wgpuDeviceHasFeature", (WGPUProc)wgpuDeviceHasFeature},
    {"wgpuDevicePopErrorScope", (WGPUProc)wgpuDevicePopErrorScope},
    {"wgpuDevicePushErrorScope", (WGPUProc)wgpuDevicePushErrorScope},
    {"wgpuDeviceRelease", (WGPUProc)wgpuDeviceRelease},
    {"wgpuGetProcAddress", (WGPUProc)wgpuGetProcAddress},
    {"wgpuInstanceAddRef", (WGPUProc)wgpuInstanceAddRef},
    {"wgpuInstanceProcessEvents", (WGPUProc)wgpuInstanceProcessEvents},
    {"wgpuInstanceRelease", (WGPUProc)wgpuInstanceRelease},
    {"wgpuInstanceRequestAdapter", (WGPUProc)wgpuInstanceRequestAdapter},
    {"wgpuInstanceWaitAny", (WGPUProc)wgpuInstanceWaitAny},
    {"wgpuPipelineLayoutAddRef", (WGPUProc)wgpuPipelineLayoutAddRef},
    {"wgpuPipelineLayoutRelease", (WGPUProc)wgpuPipelineLayoutRelease},
    {"wgpuQueueAddRef", (WGPUProc)wgpuQueueAddRef},
    {"wgpuQueueOnSubmittedWorkDone", (WGPUProc)wgpuQueueOnSubmittedWorkDone},
    {"wgpuQueueRelease", (WGPUProc)wgpuQueueRelease},
    {"wgpuQueueSubmit", (WGPUProc)wgpuQueueSubmit},
    {"wgpuQueueWriteBuffer", (WGPUProc)wgpuQueueWriteBuffer},
    {"wgpuShaderModuleAddRef", (WGPUProc)wgpuShaderModuleAddRef},
    {"wgpuShaderModuleRelease", (WGPUProc)wgpuShaderModuleRelease},
    {"wgpuSupportedFeaturesFreeMembers", (WGPUProc)wgpuSupportedFeaturesFreeMembers},
};
WGPUProc wgpuGetProcAddress(WGPUStringView name)
{
    size_t size;
    if (!native_string_size(name, &size) || !name.data || !size) return NULL;
    for (size_t i = 0; i < sizeof(procedures) / sizeof(procedures[0]); ++i)
        if (strlen(procedures[i].name) == size && !memcmp(procedures[i].name, name.data, size))
            return procedures[i].procedure;
    return NULL;
}
