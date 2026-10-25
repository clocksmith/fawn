/* SPDX-License-Identifier: MIT
 * Public pinned-WebGPU C-only software developer smoke, not GPU qualification. */
#include "adapter_class.h"
#include "webgpu.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct results {
    WGPUAdapter adapter;
    WGPUDevice device;
    unsigned adapters, devices, maps, errors, done;
    WGPUMapAsyncStatus map_status;
} results;
static void adapter_callback(WGPURequestAdapterStatus status, WGPUAdapter adapter,
    WGPUStringView message, void *userdata1, void *userdata2)
{
    (void)userdata2;
    results *result = userdata1;
    if (status != WGPURequestAdapterStatus_Success)
        fprintf(stderr, "adapter request: %.*s\n", (int)message.length, message.data);
    assert(status == WGPURequestAdapterStatus_Success && adapter);
    result->adapter = adapter;
    ++result->adapters;
}
static void device_callback(WGPURequestDeviceStatus status, WGPUDevice device,
    WGPUStringView message, void *userdata1, void *userdata2)
{
    (void)userdata2;
    results *result = userdata1;
    if (status != WGPURequestDeviceStatus_Success)
        fprintf(stderr, "device request: %.*s\n", (int)message.length, message.data);
    assert(status == WGPURequestDeviceStatus_Success && device);
    result->device = device;
    ++result->devices;
}
static void map_callback(WGPUMapAsyncStatus status, WGPUStringView message, void *userdata1, void *userdata2)
{
    (void)message;
    (void)userdata2;
    results *result = userdata1;
    ++result->maps;
    result->map_status = status;
}
static void error_callback(WGPUPopErrorScopeStatus status, WGPUErrorType type,
    WGPUStringView message, void *userdata1, void *userdata2)
{
    (void)message;
    (void)userdata2;
    results *result = userdata1;
    assert(status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_Validation);
    ++result->errors;
}
static void empty_scope_callback(WGPUPopErrorScopeStatus status, WGPUErrorType type,
    WGPUStringView message, void *userdata1, void *userdata2)
{
    (void)message;
    (void)userdata2;
    results *result = userdata1;
    assert(status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_NoError);
    ++result->errors;
}
static void done_callback(WGPUQueueWorkDoneStatus status, WGPUStringView message,
    void *userdata1, void *userdata2)
{
    (void)message;
    WGPUInstance instance = userdata2;
    results *result = userdata1;
    assert(status == WGPUQueueWorkDoneStatus_Success);
    ++result->done;
    /* Explicit nested event pumping is allowed; it must not redeliver this. */
    wgpuInstanceProcessEvents(instance);
}
static void wait_ready(WGPUInstance instance, WGPUFuture future)
{
    assert(future.id);
    WGPUFutureWaitInfo waits[2] = {{.future=future}, {.future=future}};
    assert(wgpuInstanceWaitAny(instance, 2, waits, UINT64_C(1000000000)) == WGPUWaitStatus_Success);
    assert(waits[0].completed && waits[1].completed);
}

int main(void)
{
    results result = {0};
    WGPUProcCreateInstance create_instance = (WGPUProcCreateInstance)wgpuGetProcAddress(
        (WGPUStringView){"wgpuCreateInstance", WGPU_STRLEN});
    assert(create_instance);
    WGPUInstance instance = create_instance(NULL);
    assert(instance);
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.backendType = WGPUBackendType_Vulkan;
    adapter_class_options(&options);
    WGPURequestAdapterCallbackInfo adapter_info = {.mode=WGPUCallbackMode_AllowProcessEvents,
        .callback=adapter_callback, .userdata1=&result};
    WGPUFuture future = wgpuInstanceRequestAdapter(instance, &options, adapter_info);
    assert(future.id && !result.adapters);
    wgpuInstanceProcessEvents(instance);
    assert(result.adapters == 1);
    adapter_class_require(result.adapter);
    printf("Explicit WebGPU C path adapter %s\n", adapter_class_scope());
    WGPURequestDeviceCallbackInfo device_info = {.mode=WGPUCallbackMode_WaitAnyOnly,
        .callback=device_callback, .userdata1=&result};
    future = wgpuAdapterRequestDevice(result.adapter, NULL, device_info);
    assert(!result.devices);
    wait_ready(instance, future);
    WGPUDevice device = result.device;
    WGPUQueue queue = wgpuDeviceGetQueue(device);
    assert(queue);
    const char *source = "@group(0) @binding(0) var<storage,read_write> a:array<u32>;"
        "@compute @workgroup_size(8) fn transform(@builtin(global_invocation_id) gid:vec3<u32>)"
        "{a[gid.x]=a[gid.x]*3u+7u;}"
        "@compute @workgroup_size(1) fn other(){a[0]=999u;}";
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = (WGPUStringView){source, WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader_desc = {.nextInChain=&wgsl.chain};
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(device, &shader_desc);
    assert(shader);
    WGPUComputePipelineDescriptor pipeline_desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipeline_desc.compute.module = shader;
    pipeline_desc.compute.entryPoint = (WGPUStringView){"transform", 9};
    WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(device, &pipeline_desc);
    assert(pipeline);
    WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
    assert(layout);
    WGPUBufferDescriptor buffer_desc = {.size=64*sizeof(uint32_t),
        .usage=WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst|WGPUBufferUsage_CopySrc};
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &buffer_desc);
    assert(buffer);
    uint32_t input[64];
    for (uint32_t i = 0; i < 64; ++i) input[i] = i*5u+1u;
    wgpuQueueWriteBuffer(queue, buffer, 0, input, sizeof(input));
    WGPUBindGroupEntry binding = {.binding=0,.buffer=buffer,.size=WGPU_WHOLE_SIZE};
    WGPUBindGroupDescriptor group_desc = {.layout=layout,.entryCount=1,.entries=&binding};
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(device, &group_desc);
    assert(group);
    WGPUBufferDescriptor read_desc = {.size=sizeof(input),.usage=WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst};
    WGPUBuffer readback = wgpuDeviceCreateBuffer(device, &read_desc);
    assert(readback);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    assert(pass);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    /* 72 invocations for a64-element runtime array exercises compiler bounds. */
    wgpuComputePassEncoderDispatchWorkgroups(pass, 9, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, buffer, 0, readback, 0, sizeof(input));
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    assert(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuShaderModuleRelease(shader);
    wgpuComputePipelineRelease(pipeline);
    wgpuBindGroupLayoutRelease(layout);
    wgpuBindGroupRelease(group);
    wgpuBufferRelease(buffer);
    wgpuQueueSubmit(queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    WGPUQueueWorkDoneCallbackInfo done = {.mode=WGPUCallbackMode_AllowProcessEvents,
        .callback=done_callback,.userdata1=&result,.userdata2=instance};
    future = wgpuQueueOnSubmittedWorkDone(queue, done);
    assert(future.id && !result.done);
    wgpuInstanceProcessEvents(instance);
    assert(result.done == 1);
    WGPUBufferMapCallbackInfo map = {.mode=WGPUCallbackMode_WaitAnyOnly,.callback=map_callback,.userdata1=&result};
    future = wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, WGPU_WHOLE_MAP_SIZE, map);
    assert(!result.maps && wgpuBufferGetMapState(readback) == WGPUBufferMapState_Pending);
    wait_ready(instance, future);
    assert(result.maps == 1 && result.map_status == WGPUMapAsyncStatus_Success);
    const uint32_t *actual = wgpuBufferGetConstMappedRange(readback, 0, WGPU_WHOLE_MAP_SIZE);
    assert(actual);
    for (uint32_t i = 0; i < 64; ++i) assert(actual[i] == input[i]*3u+7u);
    wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
    assert(!wgpuBufferGetMappedRange(readback, 0, sizeof(input)));
    WGPUPopErrorScopeCallbackInfo pop = {.mode=WGPUCallbackMode_WaitAnyOnly,.callback=error_callback,.userdata1=&result};
    wait_ready(instance, wgpuDevicePopErrorScope(device, pop));
    assert(result.errors == 1);
    wgpuBufferUnmap(readback);
    future = wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, sizeof(input), map);
    wgpuBufferUnmap(readback);
    wait_ready(instance, future);
    assert(result.maps == 2 && result.map_status == WGPUMapAsyncStatus_Aborted);
    /* Mapped-at-creation owns real staging writeback, including non-map usage. */
    WGPUBufferDescriptor mapped_desc = {.size=sizeof(input), .usage=WGPUBufferUsage_CopySrc,
        .mappedAtCreation=WGPU_TRUE};
    WGPUBuffer mapped = wgpuDeviceCreateBuffer(device, &mapped_desc);
    assert(mapped);
    uint32_t *mapped_bytes = wgpuBufferGetMappedRange(mapped, 0, sizeof(input));
    assert(mapped_bytes);
    memcpy(mapped_bytes, input, sizeof(input));
    wgpuBufferUnmap(mapped);
    encoder = wgpuDeviceCreateCommandEncoder(device, NULL);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, mapped, 0, readback, 0, sizeof(input));
    commands = wgpuCommandEncoderFinish(encoder, NULL);
    assert(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuBufferRelease(mapped);
    wgpuQueueSubmit(queue, 1, &commands);
    wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
    wgpuQueueSubmit(queue, 1, &commands);
    wait_ready(instance, wgpuDevicePopErrorScope(device, pop));
    assert(result.errors == 2);
    wgpuCommandBufferRelease(commands);
    wait_ready(instance, wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, sizeof(input), map));
    actual = wgpuBufferGetConstMappedRange(readback, 0, sizeof(input));
    assert(actual && !memcmp(actual, input, sizeof(input)));
    wgpuBufferUnmap(readback);

    /* Descriptor/resource provenance rejects cross-device shader and groups. */
    results other = {0};
    device_info.userdata1 = &other;
    wait_ready(instance, wgpuAdapterRequestDevice(result.adapter, NULL, device_info));
    WGPUShaderModule foreign_shader = wgpuDeviceCreateShaderModule(other.device, &shader_desc);
    assert(foreign_shader);
    pipeline_desc.compute.module = foreign_shader;
    wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateComputePipeline(device, &pipeline_desc));
    wait_ready(instance, wgpuDevicePopErrorScope(device, pop));
    assert(result.errors == 3);
    wgpuShaderModuleRelease(foreign_shader);
    wgpuDeviceRelease(other.device);

    /* Explicit layouts exercise late minimum-size and dynamic-offset mapping. */
    shader = wgpuDeviceCreateShaderModule(device, &shader_desc);
    assert(shader);
    WGPULimits limits = WGPU_LIMITS_INIT;
    assert(wgpuDeviceGetLimits(device, &limits) == WGPUStatus_Success);
    uint32_t dynamic_offset = limits.minStorageBufferOffsetAlignment;
    while (dynamic_offset < sizeof(input)) dynamic_offset *= 2;
    WGPUBindGroupLayoutEntry dynamic_entry = {.binding=0, .visibility=WGPUShaderStage_Compute,
        .buffer={.type=WGPUBufferBindingType_Storage,.hasDynamicOffset=WGPU_TRUE}};
    WGPUBindGroupLayoutDescriptor dynamic_layout_desc = {.entryCount=1,.entries=&dynamic_entry};
    layout = wgpuDeviceCreateBindGroupLayout(device, &dynamic_layout_desc);
    assert(layout);
    WGPUPipelineLayoutDescriptor explicit_desc = {.bindGroupLayoutCount=1,.bindGroupLayouts=&layout};
    WGPUPipelineLayout explicit_layout = wgpuDeviceCreatePipelineLayout(device, &explicit_desc);
    assert(explicit_layout);
    pipeline_desc.layout = explicit_layout;
    pipeline_desc.compute.module = shader;
    pipeline = wgpuDeviceCreateComputePipeline(device, &pipeline_desc);
    assert(pipeline);
    buffer_desc.size = dynamic_offset + sizeof(input);
    buffer = wgpuDeviceCreateBuffer(device, &buffer_desc);
    assert(buffer);
    wgpuQueueWriteBuffer(queue, buffer, dynamic_offset, input, sizeof(input));
    binding.buffer = buffer;
    binding.size = sizeof(input);
    group_desc.layout = layout;
    group = wgpuDeviceCreateBindGroup(device, &group_desc);
    assert(group);
    encoder = wgpuDeviceCreateCommandEncoder(device, NULL);
    pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    assert(pass);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 1, &dynamic_offset);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 8, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, buffer, dynamic_offset, readback, 0, sizeof(input));
    commands = wgpuCommandEncoderFinish(encoder, NULL);
    assert(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuQueueSubmit(queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wait_ready(instance, wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, sizeof(input), map));
    actual = wgpuBufferGetConstMappedRange(readback, 0, sizeof(input));
    assert(actual);
    for (size_t i = 0; i < 64; ++i) assert(actual[i] == input[i]*3u+7u);
    wgpuBufferUnmap(readback);

    /* Failed dynamic offset poisons the owner; finish cannot submit a prefix. */
    wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
    encoder = wgpuDeviceCreateCommandEncoder(device, NULL);
    pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    uint32_t bad_offset = 1;
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 1, &bad_offset);
    wgpuComputePassEncoderEnd(pass);
    { WGPUCommandBuffer invalid_commands = wgpuCommandEncoderFinish(encoder, NULL); assert(invalid_commands); wgpuCommandBufferRelease(invalid_commands); }
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderRelease(encoder);
    wait_ready(instance, wgpuDevicePopErrorScope(device, pop));
    assert(result.errors == 4);
    wgpuBindGroupRelease(group);
    wgpuBufferRelease(buffer);
    wgpuComputePipelineRelease(pipeline);
    wgpuShaderModuleRelease(shader);
    wgpuPipelineLayoutRelease(explicit_layout);
    wgpuBindGroupLayoutRelease(layout);

    assert(wgpuGetProcAddress((WGPUStringView){"wgpuQueueSubmit", WGPU_STRLEN}));
    assert(wgpuGetProcAddress((WGPUStringView){"wgpuRenderPassEncoderDrawIndexed", WGPU_STRLEN}) ==
        (WGPUProc)wgpuRenderPassEncoderDrawIndexed);
    assert(wgpuGetProcAddress((WGPUStringView){"wgpuRenderPassEncoderSetImmediates", WGPU_STRLEN}) ==
        (WGPUProc)wgpuRenderPassEncoderSetImmediates);
    assert(!wgpuGetProcAddress((WGPUStringView){"wgpuExternalTextureSetLabel", WGPU_STRLEN})); /* pinned, unprovided */
    assert(!wgpuGetProcAddress((WGPUStringView){"doe_vk_context_create", WGPU_STRLEN}));
    /* Invalid mapping reports to the active scope at the call, independently
     * of when its WaitAnyOnly callback is delivered; a later valid map works. */
    unsigned maps_before = result.maps;
    wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
    future = wgpuBufferMapAsync(readback, WGPUMapMode_Read, 4, 16, map);
    assert(future.id && result.maps == maps_before);
    wait_ready(instance, wgpuDevicePopErrorScope(device, pop));
    assert(result.errors == 5 && result.maps == maps_before);
    wait_ready(instance, future);
    assert(result.maps == maps_before + 1 && result.map_status == WGPUMapAsyncStatus_Error);
    assert(wgpuBufferGetMapState(readback) == WGPUBufferMapState_Unmapped);
    wait_ready(instance, wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, 16, map));
    assert(result.map_status == WGPUMapAsyncStatus_Success);
    wgpuBufferUnmap(readback);

    /* Device loss aborts mapping; it does not generate another validation
     * error, even when the requested map also has invalid alignment. */
    wgpuDeviceDestroy(device);
    wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
    wait_ready(instance, wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, 16, map));
    assert(result.map_status == WGPUMapAsyncStatus_Aborted);
    wait_ready(instance, wgpuBufferMapAsync(readback, WGPUMapMode_Read, 4, 16, map));
    assert(result.map_status == WGPUMapAsyncStatus_Aborted);
    pop.callback = empty_scope_callback;
    wait_ready(instance, wgpuDevicePopErrorScope(device, pop));
    assert(result.errors == 6);
    wgpuBufferRelease(readback);
    wgpuQueueRelease(queue);
    wgpuDeviceRelease(device);
    wgpuAdapterRelease(result.adapter);
    wgpuInstanceRelease(instance);
    printf("PASS public ABI: compute/readback, OOB guards, dynamic offsets, cross-device rejection, one-shot submit, early releases, callback reentry and map cancellation %s\n", adapter_class_scope());
    return 0;
}
