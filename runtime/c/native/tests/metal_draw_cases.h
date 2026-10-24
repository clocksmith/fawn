/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_TEST_METAL_DRAW_CASES_H
#define DOE_NATIVE_TEST_METAL_DRAW_CASES_H
#include "../src/metal_draw_guard.h"

static WGPURenderPipeline metal_index_pipeline(state *s)
{
    const char *code="@vertex fn v(@location(0) value:u32)->@builtin(position) vec4f{return vec4f(f32(value),0.0,0.0,1.0);}"
        "@fragment fn f()->@location(0) vec4f{return vec4f(1.0);}";
    WGPUShaderSourceWGSL wgsl=WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code=(WGPUStringView){code,WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader_desc={.nextInChain=&wgsl.chain};
    WGPUShaderModule shader=wgpuDeviceCreateShaderModule(s->device,&shader_desc);
    assert(shader);
    WGPUVertexAttribute attribute={.format=WGPUVertexFormat_Uint32,.shaderLocation=0};
    WGPUVertexBufferLayout layout={.arrayStride=4,.attributeCount=1,.attributes=&attribute};
    WGPUColorTargetState color={.format=WGPUTextureFormat_RGBA8Unorm,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment={.module=shader,.targetCount=1,.targets=&color};
    WGPURenderPipelineDescriptor desc=WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.vertex=(WGPUVertexState){.module=shader,.bufferCount=1,.buffers=&layout};
    desc.fragment=&fragment;
    WGPURenderPipeline result=wgpuDeviceCreateRenderPipeline(s->device,&desc);
    wgpuShaderModuleRelease(shader);
    assert(result);
    return result;
}
static WGPURenderPassEncoder metal_index_begin(WGPUCommandEncoder encoder,WGPUTextureView view,
    WGPURenderPipeline pipeline,WGPUBuffer vertices,WGPUBuffer indices)
{
    WGPURenderPassColorAttachment color=WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view=view; color.loadOp=WGPULoadOp_Clear; color.storeOp=WGPUStoreOp_Store;
    WGPURenderPassDescriptor descriptor={.colorAttachmentCount=1,.colorAttachments=&color};
    WGPURenderPassEncoder pass=wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
    assert(pass);
    wgpuRenderPassEncoderSetPipeline(pass,pipeline);
    wgpuRenderPassEncoderSetVertexBuffer(pass,0,vertices,0,16);
    wgpuRenderPassEncoderSetIndexBuffer(pass,indices,WGPUIndexFormat_Uint16,0,6);
    return pass;
}
typedef struct metal_draw_error { WGPURenderPassEncoder pass; unsigned calls; } metal_draw_error;
static void metal_draw_reentry(WGPUDevice const *device,WGPUErrorType type,WGPUStringView message,void *userdata,void *unused)
{
    (void)unused;
    metal_draw_error *error=userdata;
    assert(type==WGPUErrorType_OutOfMemory && error->calls++==0);
    static const char expected[]="Metal: injected native failure (native=-71): owned fault boundary";
    assert(message.length==sizeof(expected)-1 && !memcmp(message.data,expected,message.length));
    WGPULimits limits=WGPU_LIMITS_INIT;
    assert(wgpuDeviceGetLimits(*device,&limits)==WGPUStatus_Success);
    wgpuRenderPassEncoderRelease(error->pass);
    error->pass=NULL;
}
static void metal_draw_guard_ledger(state *s,mock_native *mock)
{
    native_device *device=(native_device *)s->device;
    assert(!device->metal_draw_guard);
    WGPURenderPipeline pipeline=metal_index_pipeline(s);
    WGPUTexture image=metal_image(s);
    WGPUTextureView view=wgpuTextureCreateView(image,NULL);
    WGPUBuffer vertices=buffer(s,16,WGPUBufferUsage_Vertex);
    WGPUBuffer indices=buffer(s,6,WGPUBufferUsage_Index);
    native_buffer *index=doe_resource_buffer_native((doe_resource_buffer *)indices);
    assert(wgpuBufferGetSize(indices)==6 && doe_mtl_buffer_size(index->handle.metal)==8);
    void *index_identity=doe_mtl_buffer_native(index->handle.metal);
    WGPUCommandEncoder encoder=wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPURenderPassEncoder pass=metal_index_begin(encoder,view,pipeline,vertices,indices);
    unsigned baseline=mock->live;
    mock->fail_pipeline_at=mock->pipeline_calls+2;
    mock->pipeline_failure=DOE_MTL_OUT_OF_MEMORY;
    metal_draw_error error={.pass=pass};
    WGPUUncapturedErrorCallbackInfo saved=device->uncaptured;
    device->uncaptured=(WGPUUncapturedErrorCallbackInfo){.callback=metal_draw_reentry,.userdata1=&error};
    wgpuRenderPassEncoderDrawIndexed(pass,3,1,0,0,0);
    device->uncaptured=saved;
    mock->fail_pipeline_at=0; mock->pipeline_failure=DOE_MTL_OK;
    assert(error.calls==1 && !error.pass && !device->metal_draw_guard && mock->live==baseline);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_OutOfMemory);
    finish_invalid(encoder); wgpuCommandEncoderRelease(encoder); pop(s,WGPUErrorType_OutOfMemory);
    WGPUBuffer arguments=buffer(s,24,WGPUBufferUsage_Indirect|WGPUBufferUsage_CopyDst);
    const uint32_t words[6]={0xa5a5a5a5,3,1,0,0,0};
    wgpuQueueWriteBuffer(s->queue,arguments,0,words,sizeof(words));
    encoder=wgpuDeviceCreateCommandEncoder(s->device,NULL);
    pass=metal_index_begin(encoder,view,pipeline,vertices,indices);
    wgpuRenderPassEncoderDrawIndexed(pass,3,1,0,0,0);
    assert(device->metal_draw_guard);
    wgpuRenderPassEncoderDrawIndexedIndirect(pass,arguments,4);
    wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
    wgpuRenderPipelineRelease(pipeline); wgpuTextureViewRelease(view); wgpuTextureRelease(image);
    wgpuBufferRelease(vertices); wgpuBufferRelease(indices); wgpuBufferRelease(arguments);
    unsigned renders=mock->renders,dispatches=mock->dispatches;
    native_lock(device); hold=true; native_unlock(device);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Internal);
    submit(s,encoder); pop(s,WGPUErrorType_Internal);
    native_lock(device);
    assert(device->pending && mock->renders==renders+1 && mock->dispatches==dispatches+4);
    assert(mock->render_draws[0].indirect_arguments && mock->render_draws[1].indirect_arguments);
    assert(mock->render_draws[0].indirect_arguments!=mock->render_draws[1].indirect_arguments);
    assert(mock->render_draws[0].index_buffer==index_identity && mock->render_draws[1].index_buffer==index_identity);
    assert(mock->dispatch_sequence<mock->render_sequence && mock->last_length==20 && mock->last_source_offset==4);
    hold=false;
    native_queue_settlement settled=native_settle_pending(device);
    assert(settled.complete && settled.failure.status==NATIVE_OK && !device->pending);
    native_unlock(device);
    assert(!s->errors);
    puts("PASS public Metal guarded indexed host: compiler-selected PSOs, exact6logical/8physical uint16 bytes, second-PSO OOM/reentry rollback, distinct GPU scratch, whole-pass ordering and unknown early releases; no GPU execution");
}
#endif
