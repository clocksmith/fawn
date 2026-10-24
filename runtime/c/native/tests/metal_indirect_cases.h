/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_METAL_INDIRECT_CASES_H
#define DOE_NATIVE_METAL_INDIRECT_CASES_H
#include "../src/metal_indirect.h"

typedef struct metal_indirect_error {
    WGPUComputePassEncoder pass;
    unsigned calls;
} metal_indirect_error;
static void metal_indirect_reentry(WGPUDevice const *device, WGPUErrorType type,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)unused;
    metal_indirect_error *state = userdata;
    assert(type == WGPUErrorType_OutOfMemory && state->calls++ == 0);
    static const char expected[] = "Metal: injected native failure (native=-71): owned fault boundary";
    assert(message.length == sizeof(expected) - 1 && !memcmp(message.data,expected,message.length));
    WGPULimits limits = WGPU_LIMITS_INIT;
    assert(wgpuDeviceGetLimits(*device,&limits) == WGPUStatus_Success);
    /* Drop the last caller pass handle during the originating error callback.
     * The public call must retain its own pass/device leases until return. */
    wgpuComputePassEncoderRelease(state->pass);
    state->pass = NULL;
}
static void metal_indirect_ledger(state *s, mock_native *mock)
{
    native_device *device = (native_device *)s->device;
    assert(!device->metal_dispatch_guard);
    WGPUComputePipeline program = pipeline(s,
        "@group(0) @binding(0) var<storage,read_write> value:array<u32>;"
        "@compute @workgroup_size(2) fn main(@builtin(num_workgroups) n:vec3u,"
        "@builtin(local_invocation_index) lane:u32){if(lane==0u){value[0]=n.x;}}");
    WGPUBuffer arguments = buffer(s,32,WGPUBufferUsage_Indirect|WGPUBufferUsage_CopyDst);
    WGPUBuffer output = buffer(s,16,WGPUBufferUsage_Storage);
    WGPUBindGroupEntry entry = {.binding=0,.buffer=output,.size=16};
    WGPUBindGroup bindings = group(s,program,&entry,1);
    const uint32_t input[8] = {91,2,1,1,92,93,94,95};
    wgpuQueueWriteBuffer(s->queue,arguments,0,input,sizeof(input));
    unsigned created = mock->created;
    for (unsigned test = 0; test < 2; ++test) {
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
        wgpuComputePassEncoderSetPipeline(pass,program);
        wgpuComputePassEncoderSetBindGroup(pass,0,bindings,0,NULL);
        mock->fail = MOCK_PIPELINE;
        if (test == 0) {
            wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Internal);
            wgpuComputePassEncoderDispatchWorkgroupsIndirect(pass,arguments,4);
            pop(s,WGPUErrorType_Internal);
        } else {
            mock->pipeline_failure = DOE_MTL_OUT_OF_MEMORY;
            metal_indirect_error error = {.pass=pass};
            WGPUUncapturedErrorCallbackInfo saved = device->uncaptured;
            device->uncaptured = (WGPUUncapturedErrorCallbackInfo){.callback=metal_indirect_reentry,.userdata1=&error};
            wgpuComputePassEncoderDispatchWorkgroupsIndirect(pass,arguments,4);
            device->uncaptured = saved;
            assert(error.calls == 1 && !error.pass);
            pass = NULL;
        }
        mock->fail = MOCK_NONE;
        mock->pipeline_failure = DOE_MTL_OK;
        assert(!device->metal_dispatch_guard && mock->created == created && !s->errors);
        wgpuDevicePushErrorScope(s->device,test == 0 ? WGPUErrorFilter_Internal : WGPUErrorFilter_OutOfMemory);
        if (pass) {
            wgpuComputePassEncoderEnd(pass);
            wgpuComputePassEncoderRelease(pass);
        }
        finish_invalid(encoder);
        wgpuCommandEncoderRelease(encoder);
        pop(s,test == 0 ? WGPUErrorType_Internal : WGPUErrorType_OutOfMemory);
    }
    native_buffer *argument_payload = doe_resource_buffer_native((doe_resource_buffer *)arguments);
    native_buffer *output_payload = doe_resource_buffer_native((doe_resource_buffer *)output);
    void *argument_identity = doe_mtl_buffer_native(argument_payload->handle.metal);
    void *output_identity = doe_mtl_buffer_native(output_payload->handle.metal);
    unsigned queries = mock->content_queries, copies = mock->copies, indirects = mock->indirects;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderSetPipeline(pass,program);
    wgpuComputePassEncoderSetBindGroup(pass,0,bindings,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroupsIndirect(pass,arguments,4);
    assert(device->metal_dispatch_guard && mock->created == created + 1);
    wgpuComputePassEncoderDispatchWorkgroupsIndirect(pass,arguments,4);
    assert(mock->created == created + 1); /* One lazy device PSO, not per record. */
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuComputePipelineRelease(program);
    wgpuBindGroupRelease(bindings);
    wgpuBufferRelease(arguments);
    wgpuBufferRelease(output);
    submit(s,encoder);
    assert(mock->content_queries == queries && mock->copies == copies + 2 && mock->indirects == indirects + 2);
    assert(mock->last_source == argument_identity && mock->last_source_offset == 4 && mock->last_length == 12);
    assert(mock->indirect_arguments == mock->last_destination && mock->indirect_arguments != argument_identity);
    assert(mock->direct_resources.buffers[0].buffer == mock->indirect_arguments);
    assert(mock->bindings[0].buffer == output_identity && mock->bindings[0].size == 16);
    assert(mock->copy_sequence < mock->dispatch_sequence && mock->dispatch_sequence < mock->indirect_sequence);
    assert(mock->workgroup.x == 2 && mock->workgroup.y == 1 && mock->workgroup.z == 1 && !s->errors);
}
#endif
