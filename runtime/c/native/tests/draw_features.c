/* SPDX-License-Identifier: MIT
 * Public optional draw features: indirect-first-instance (GPU-produced nonzero
 * firstInstance reaches instance-step attributes and instance_index for direct
 * and indexed indirect draws, bounded against the instance buffer) and
 * depth-clip-control (unclippedDepth draws geometry outside [0,1] with depth
 * clamped, where the clipped control draws nothing). Without the features a
 * nonzero indirect firstInstance stays a no-op and unclippedDepth rejects. */
#include "consumer.h"
#include <stdlib.h>

enum { SIZE = 4, PITCH = 256, CLEAR = 7 };

static const char *const instance_code =
    "struct O{@builtin(position) p:vec4f,@location(0) @interpolate(flat) v:u32}"
    "@vertex fn vertex(@builtin(vertex_index) i:u32,@builtin(instance_index) n:u32,@location(0) a:u32)->O{"
    "var p=array<vec2f,3>(vec2f(-1.0,-1.0),vec2f(3.0,-1.0),vec2f(-1.0,3.0));"
    "return O(vec4f(p[i],0.0,1.0),a*1000u+n);}"
    "@fragment fn fragment(o:O)->@location(0) vec4u{return vec4u(o.v,0u,0u,1u);}";
/* Left half at z=1.5 and right half at z=-0.5: both outside the clip volume. */
static const char *const depth_code =
    "@vertex fn vertex(@builtin(vertex_index) i:u32)->@builtin(position) vec4f{"
    "var x=array<f32,6>(-1.0,0.0,0.0,-1.0,0.0,-1.0);"
    "var y=array<f32,6>(-1.0,-1.0,1.0,-1.0,1.0,1.0);"
    "let k=i%6u;"
    "if(i<6u){return vec4f(x[k],y[k],1.5,1.0);}"
    "return vec4f(x[k]+1.0,y[k],-0.5,1.0);}"
    "@fragment fn fragment()->@location(0) vec4f{return vec4f(0.0,1.0,0.0,1.0);}";

static WGPUShaderModule module(state *s, const char *code)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code,WGPU_STRLEN};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain=&source.chain};
    WGPUShaderModule result = wgpuDeviceCreateShaderModule(s->device,&descriptor);
    assert(result);
    return result;
}
static WGPUTexture image(state *s, WGPUTextureFormat format)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = format;
    descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    descriptor.size = (WGPUExtent3D){SIZE,SIZE,1};
    WGPUTexture result = wgpuDeviceCreateTexture(s->device,&descriptor);
    assert(result);
    return result;
}
static WGPUBuffer filled(state *s, WGPUBufferUsage usage, const void *data, size_t size)
{
    WGPUBuffer result = buffer(s,size,usage | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue,result,0,data,size);
    return result;
}
static WGPURenderPipeline instance_pipeline(state *s, WGPUShaderModule shader)
{
    WGPUVertexAttribute attribute = {.format=WGPUVertexFormat_Uint32,.shaderLocation=0};
    WGPUVertexBufferLayout layout = {.stepMode=WGPUVertexStepMode_Instance,.arrayStride=4,
        .attributeCount=1,.attributes=&attribute};
    WGPUColorTargetState target = {.format=WGPUTextureFormat_R32Uint,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=shader,.entryPoint={"fragment",WGPU_STRLEN},.targetCount=1,.targets=&target};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex = (WGPUVertexState){.module=shader,.entryPoint={"vertex",WGPU_STRLEN},.bufferCount=1,.buffers=&layout};
    descriptor.fragment = &fragment;
    WGPURenderPipeline result = wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
    assert(result);
    return result;
}
static WGPURenderPipeline depth_pipeline(state *s, WGPUShaderModule shader, bool unclipped)
{
    WGPUColorTargetState target = {.format=WGPUTextureFormat_RGBA8Unorm,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=shader,.entryPoint={"fragment",WGPU_STRLEN},.targetCount=1,.targets=&target};
    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth.format = WGPUTextureFormat_Depth32Float;
    depth.depthWriteEnabled = WGPUOptionalBool_True;
    depth.depthCompare = WGPUCompareFunction_Always;
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex = (WGPUVertexState){.module=shader,.entryPoint={"vertex",WGPU_STRLEN}};
    descriptor.fragment = &fragment;
    descriptor.depthStencil = &depth;
    descriptor.primitive.unclippedDepth = unclipped;
    return wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
}
static void expect_u32(state *s, WGPUTexture texture, uint32_t left, uint32_t right, const char *name)
{
    WGPUBuffer readback = buffer(s,PITCH * SIZE,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    texture_to_buffer(encoder,texture,readback,0,SIZE,SIZE);
    submit(s,encoder);
    const unsigned char *bytes = map(s,readback,PITCH * SIZE);
    for (uint32_t y = 0; y < SIZE; ++y) for (uint32_t x = 0; x < SIZE; ++x) {
        uint32_t value;
        memcpy(&value,bytes + y * PITCH + x * 4,4);
        if (value != (x < SIZE / 2 ? left : right)) {
            fprintf(stderr,"%s texel (%u,%u): %u\n",name,x,y,value);
            abort();
        }
    }
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}

typedef enum draw_kind { DIRECT, INDIRECT, INDEXED_INDIRECT, BUNDLE_INDIRECT } draw_kind;
/* One full-target draw into a fresh r32uint target cleared to CLEAR; returns it. */
static WGPUTexture instance_draw(state *s, WGPURenderPipeline pipeline, WGPUBuffer instances,
    WGPUBuffer arguments, uint64_t offset, WGPUBuffer indices, draw_kind kind)
{
    WGPUTexture target = image(s,WGPUTextureFormat_R32Uint);
    WGPUTextureView view = wgpuTextureCreateView(target,NULL);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = view;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = (WGPUColor){CLEAR,0,0,0};
    WGPURenderPassDescriptor descriptor = {.colorAttachmentCount=1,.colorAttachments=&color};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
    if (kind == BUNDLE_INDIRECT) {
        WGPUTextureFormat format = WGPUTextureFormat_R32Uint;
        WGPURenderBundleEncoderDescriptor bundle_desc = {.colorFormatCount=1,.colorFormats=&format,.sampleCount=1};
        WGPURenderBundleEncoder bundle_encoder = wgpuDeviceCreateRenderBundleEncoder(s->device,&bundle_desc);
        wgpuRenderBundleEncoderSetPipeline(bundle_encoder,pipeline);
        wgpuRenderBundleEncoderSetVertexBuffer(bundle_encoder,0,instances,0,WGPU_WHOLE_SIZE);
        wgpuRenderBundleEncoderDrawIndirect(bundle_encoder,arguments,offset);
        WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(bundle_encoder,NULL);
        assert(bundle);
        wgpuRenderPassEncoderExecuteBundles(pass,1,&bundle);
        wgpuRenderBundleRelease(bundle);
        wgpuRenderBundleEncoderRelease(bundle_encoder);
    } else {
        wgpuRenderPassEncoderSetPipeline(pass,pipeline);
        wgpuRenderPassEncoderSetVertexBuffer(pass,0,instances,0,WGPU_WHOLE_SIZE);
        if (kind == DIRECT) wgpuRenderPassEncoderDraw(pass,3,1,0,2);
        else if (kind == INDIRECT) wgpuRenderPassEncoderDrawIndirect(pass,arguments,offset);
        else {
            wgpuRenderPassEncoderSetIndexBuffer(pass,indices,WGPUIndexFormat_Uint16,0,WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderDrawIndexedIndirect(pass,arguments,offset);
        }
    }
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    submit(s,encoder);
    wgpuTextureViewRelease(view);
    return target;
}

/* Instance attribute values 10/20/30 and instance_index n write a*1000+n. */
static void first_instance(state *s, bool enabled)
{
    WGPUShaderModule shader = module(s,instance_code);
    WGPURenderPipeline pipeline = instance_pipeline(s,shader);
    const uint32_t values[3] = {10,20,30};
    WGPUBuffer instances = filled(s,WGPUBufferUsage_Vertex,values,sizeof(values));
    const uint16_t index_data[4] = {0,1,2,0};
    WGPUBuffer indices = filled(s,WGPUBufferUsage_Index,index_data,sizeof(index_data));
    /* Words 0..3 draw first=2; 4..7 draw first=2 x2 (overruns three instances);
     * 8..12 drawIndexed first=1; 13..15 pad. */
    const uint32_t words[16] = {3,1,0,2, 3,2,0,2, 3,1,0,0,1, 0,0,0};
    WGPUBuffer arguments = filled(s,WGPUBufferUsage_Indirect,words,sizeof(words));
    struct { draw_kind kind; uint64_t offset; uint32_t expected; const char *name; } draws[] = {
        {DIRECT,0,30002,"direct firstInstance 2"},
        {INDIRECT,0,enabled ? 30002 : CLEAR,"indirect firstInstance 2"},
        {INDIRECT,16,CLEAR,"indirect firstInstance 2 overrun"},
        {INDEXED_INDIRECT,32,enabled ? 20001 : CLEAR,"indexed indirect firstInstance 1"},
        {BUNDLE_INDIRECT,0,enabled ? 30002 : CLEAR,"bundle indirect firstInstance 2"},
    };
    for (size_t i = 0; i < sizeof(draws) / sizeof(draws[0]); ++i) {
        WGPUTexture target = instance_draw(s,pipeline,instances,arguments,draws[i].offset,indices,draws[i].kind);
        expect_u32(s,target,draws[i].expected,draws[i].expected,draws[i].name);
        wgpuTextureRelease(target);
    }
    wgpuBufferRelease(arguments);
    wgpuBufferRelease(indices);
    wgpuBufferRelease(instances);
    wgpuRenderPipelineRelease(pipeline);
    wgpuShaderModuleRelease(shader);
}

/* Unclipped: left texels depth 1.0, right 0.0, all green. Clipped: untouched. */
static void unclipped_depth(state *s)
{
    WGPUShaderModule shader = module(s,depth_code);
    WGPURenderPipeline pipelines[2] = {depth_pipeline(s,shader,true),depth_pipeline(s,shader,false)};
    assert(pipelines[0] && pipelines[1]);
    for (size_t p = 0; p < 2; ++p) {
        WGPUTexture color_texture = image(s,WGPUTextureFormat_RGBA8Unorm);
        WGPUTexture depth_texture = image(s,WGPUTextureFormat_Depth32Float);
        WGPUTextureView color_view = wgpuTextureCreateView(color_texture,NULL);
        WGPUTextureView depth_view = wgpuTextureCreateView(depth_texture,NULL);
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view = color_view;
        color.loadOp = WGPULoadOp_Clear;
        color.storeOp = WGPUStoreOp_Store;
        color.clearValue = (WGPUColor){0,0,0,0};
        WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        depth.view = depth_view;
        depth.depthLoadOp = WGPULoadOp_Clear;
        depth.depthStoreOp = WGPUStoreOp_Store;
        depth.depthClearValue = 0.5f;
        WGPURenderPassDescriptor descriptor = {.colorAttachmentCount=1,.colorAttachments=&color,
            .depthStencilAttachment=&depth};
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
        wgpuRenderPassEncoderSetPipeline(pass,pipelines[p]);
        wgpuRenderPassEncoderDraw(pass,12,1,0,0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        WGPUBuffer readback = buffer(s,2 * PITCH * SIZE,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
        texture_to_buffer(encoder,color_texture,readback,0,SIZE,SIZE);
        WGPUTexelCopyTextureInfo source = {.texture=depth_texture,.aspect=WGPUTextureAspect_DepthOnly};
        WGPUTexelCopyBufferInfo destination = {.buffer=readback,
            .layout={.offset=PITCH * SIZE,.bytesPerRow=PITCH,.rowsPerImage=SIZE}};
        wgpuCommandEncoderCopyTextureToBuffer(encoder,&source,&destination,&(WGPUExtent3D){SIZE,SIZE,1});
        submit(s,encoder);
        const unsigned char *bytes = map(s,readback,2 * PITCH * SIZE);
        for (uint32_t y = 0; y < SIZE; ++y) for (uint32_t x = 0; x < SIZE; ++x) {
            uint32_t pixel;
            float value;
            memcpy(&pixel,bytes + y * PITCH + x * 4,4);
            memcpy(&value,bytes + PITCH * SIZE + y * PITCH + x * 4,4);
            uint32_t expected_pixel = p == 0 ? 0xff00ff00u : 0;
            float expected_depth = p == 1 ? 0.5f : x < SIZE / 2 ? 1.0f : 0.0f;
            if (pixel != expected_pixel || value != expected_depth) {
                fprintf(stderr,"%s texel (%u,%u): color %08x depth %.9g\n",p == 0 ? "unclipped" : "clipped",
                    x,y,pixel,value);
                abort();
            }
        }
        wgpuBufferUnmap(readback);
        wgpuBufferRelease(readback);
        wgpuTextureViewRelease(color_view);
        wgpuTextureViewRelease(depth_view);
        wgpuTextureRelease(color_texture);
        wgpuTextureRelease(depth_texture);
    }
    wgpuRenderPipelineRelease(pipelines[0]);
    wgpuRenderPipelineRelease(pipelines[1]);
    wgpuShaderModuleRelease(shader);
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    const WGPUFeatureName names[2] = {WGPUFeatureName_IndirectFirstInstance,WGPUFeatureName_DepthClipControl};
    const char *const labels[2] = {"indirect-first-instance","depth-clip-control"};
    WGPUFeatureName offered[2];
    size_t count = 0;
    for (size_t i = 0; i < 2; ++i) {
        if (wgpuAdapterHasFeature(s.adapter,names[i])) offered[count++] = names[i];
        else printf("SKIP %s: the Vulkan device does not offer %s %s\n",labels[i],
            i == 0 ? "drawIndirectFirstInstance" : "depthClamp",adapter_class_scope());
    }
    assert(!wgpuDeviceHasFeature(s.device,WGPUFeatureName_IndirectFirstInstance) &&
        !wgpuDeviceHasFeature(s.device,WGPUFeatureName_DepthClipControl));
    first_instance(&s,false);
    WGPUShaderModule shader = module(&s,depth_code);
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Validation);
    assert(!depth_pipeline(&s,shader,true));
    expect_validation(&s);
    wgpuShaderModuleRelease(shader);
    assert(s.validation_errors == 1);
    consumer_finish(&s);

    state enabled = {0};
    consumer_init_features(&enabled,offered,count);
    for (size_t i = 0; i < count; ++i) {
        assert(wgpuDeviceHasFeature(enabled.device,offered[i]));
        if (offered[i] == WGPUFeatureName_IndirectFirstInstance) first_instance(&enabled,true);
        if (offered[i] == WGPUFeatureName_DepthClipControl) unclipped_depth(&enabled);
    }
    consumer_finish(&enabled);
    printf("PASS public draw features: %zu of 2 offered and enabled (indirect-first-instance: GPU firstInstance "
        "2/1 reaches instance attributes and instance_index exactly for indirect, indexed indirect and bundle "
        "draws, an instance-buffer overrun stays a no-op; depth-clip-control: unclippedDepth draws z 1.5/-0.5 "
        "clamped to depth 1.0/0.0 where the clipped control draws nothing); without them nonzero indirect "
        "firstInstance is a no-op while direct firstInstance draws, and unclippedDepth rejects %s\n",
        count, adapter_class_scope());
    return 0;
}
