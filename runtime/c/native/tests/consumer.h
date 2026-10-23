/* SPDX-License-Identifier: MIT
 * Public-ABI software consumer helpers shared by focused image/admission checks. */
#ifndef DOE_NATIVE_TEST_CONSUMER_H
#define DOE_NATIVE_TEST_CONSUMER_H
#include "webgpu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct state {
    WGPUInstance instance;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;
    unsigned errors, mapped, validation_errors;
} state;
static inline void adapter_reply(WGPURequestAdapterStatus status, WGPUAdapter adapter,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)message; (void)unused;
    assert(status == WGPURequestAdapterStatus_Success);
    ((state *)userdata)->adapter = adapter;
}
static inline void device_reply(WGPURequestDeviceStatus status, WGPUDevice device,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)message; (void)unused;
    assert(status == WGPURequestDeviceStatus_Success);
    ((state *)userdata)->device = device;
}
static inline void error_reply(WGPUDevice const *device, WGPUErrorType type, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)device; (void)type; (void)unused;
    ++((state *)userdata)->errors;
    fprintf(stderr, "native error: %.*s\n", (int)message.length, message.data);
}
static inline void validation_reply(WGPUPopErrorScopeStatus status, WGPUErrorType type,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)message; (void)unused;
    assert(status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_Validation);
    ++((state *)userdata)->validation_errors;
}
static inline void expect_validation(state *s)
{
    unsigned before = s->validation_errors;
    WGPUPopErrorScopeCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=validation_reply,.userdata1=s};
    assert(wgpuDevicePopErrorScope(s->device,callback).id);
    assert(s->validation_errors == before + 1);
}
static inline void mapped(WGPUMapAsyncStatus status, WGPUStringView message, void *userdata, void *unused)
{
    (void)message; (void)unused;
    assert(status == WGPUMapAsyncStatus_Success);
    ++((state *)userdata)->mapped;
}
static inline void submit(state *s, WGPUCommandEncoder encoder)
{
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    assert(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuQueueSubmit(s->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
}
static inline WGPUBuffer buffer(state *s, uint64_t size, WGPUBufferUsage usage)
{
    WGPUBufferDescriptor descriptor = {.size=size,.usage=usage};
    WGPUBuffer result = wgpuDeviceCreateBuffer(s->device, &descriptor);
    assert(result);
    return result;
}
static inline WGPUTexture texture(state *s, WGPUTextureFormat format, WGPUTextureUsage usage, uint32_t mips)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = format;
    descriptor.usage = usage;
    descriptor.size = (WGPUExtent3D){2,2,1};
    descriptor.mipLevelCount = mips;
    WGPUTexture result = wgpuDeviceCreateTexture(s->device, &descriptor);
    assert(result && wgpuTextureGetFormat(result) == format && wgpuTextureGetUsage(result) == usage);
    assert(wgpuTextureGetWidth(result) == 2 && wgpuTextureGetHeight(result) == 2);
    return result;
}
static inline WGPUComputePipeline pipeline(state *s, const char *code)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code,WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader_desc = {.nextInChain=&source.chain};
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(s->device, &shader_desc);
    assert(shader);
    WGPUComputePipelineDescriptor descriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    descriptor.compute.module = shader;
    WGPUComputePipeline result = wgpuDeviceCreateComputePipeline(s->device, &descriptor);
    wgpuShaderModuleRelease(shader);
    assert(result);
    return result;
}
static inline WGPUBindGroup group(state *s, WGPUComputePipeline pipeline, const WGPUBindGroupEntry *entries, size_t count)
{
    WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
    assert(layout);
    WGPUBindGroupDescriptor descriptor = {.layout=layout,.entryCount=count,.entries=entries};
    WGPUBindGroup result = wgpuDeviceCreateBindGroup(s->device, &descriptor);
    wgpuBindGroupLayoutRelease(layout);
    assert(result);
    return result;
}
static inline void dispatch(WGPUCommandEncoder encoder, WGPUComputePipeline pipeline, WGPUBindGroup group)
{
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    assert(pass);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 4, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
}
static inline const void *map(state *s, WGPUBuffer buffer, size_t size)
{
    WGPUBufferMapCallbackInfo callback = {.mode=WGPUCallbackMode_WaitAnyOnly,.callback=mapped,.userdata1=s};
    WGPUFuture future = wgpuBufferMapAsync(buffer,WGPUMapMode_Read,0,size,callback);
    assert(future.id);
    WGPUFutureWaitInfo wait = {.future=future};
    assert(wgpuInstanceWaitAny(s->instance,1,&wait,0) == WGPUWaitStatus_Success);
    const void *data = wgpuBufferGetConstMappedRange(buffer,0,size);
    assert(data);
    return data;
}
static inline void texture_to_buffer(WGPUCommandEncoder encoder, WGPUTexture texture, WGPUBuffer buffer,
    uint32_t mip, uint32_t width, uint32_t height)
{
    WGPUTexelCopyTextureInfo source = {.texture=texture,.mipLevel=mip};
    WGPUTexelCopyBufferInfo destination = {.buffer=buffer,.layout={.bytesPerRow=256,.rowsPerImage=height}};
    WGPUExtent3D size = {width,height,1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder,&source,&destination,&size);
}
#endif
