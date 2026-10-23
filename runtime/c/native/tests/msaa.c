/* SPDX-License-Identifier: MIT */
#include "consumer.h"

static WGPURenderPipeline graphics(state *s, WGPUShaderModule shader, uint32_t mask, bool coverage, bool transparent)
{
    WGPUColorTargetState color = {.format=WGPUTextureFormat_RGBA8Unorm,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=shader,.targetCount=1,.targets=&color,
        .entryPoint={transparent ? "transparent" : "opaque",WGPU_STRLEN}};
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.vertex.module = shader;
    desc.fragment = &fragment;
    desc.multisample.count = 4;
    desc.multisample.mask = mask;
    desc.multisample.alphaToCoverageEnabled = coverage;
    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth.format = WGPUTextureFormat_Depth32Float;
    depth.depthWriteEnabled = WGPUOptionalBool_True;
    depth.depthCompare = WGPUCompareFunction_Less;
    desc.depthStencil = &depth;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(s->device,&desc);
    assert(pipeline);
    return pipeline;
}
static WGPUTexture multisample(state *s, WGPUTextureFormat format)
{
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.format = format;
    desc.size = (WGPUExtent3D){2,2,1};
    desc.usage = WGPUTextureUsage_RenderAttachment;
    desc.sampleCount = 4;
    WGPUTexture image = wgpuDeviceCreateTexture(s->device,&desc);
    assert(image && wgpuTextureGetSampleCount(image) == 4);
    return image;
}
static void image_case(state *s, WGPUShaderModule module, uint32_t mask, bool coverage, bool transparent, bool draw,
    bool load_after_discard, const unsigned char expected[4])
{
    WGPUTexture color = multisample(s,WGPUTextureFormat_RGBA8Unorm);
    WGPUTexture depth = multisample(s,WGPUTextureFormat_Depth32Float);
    WGPUTexture resolved = texture(s,WGPUTextureFormat_RGBA8Unorm,WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc,1);
    WGPUTextureView cv = wgpuTextureCreateView(color,NULL), dv = wgpuTextureCreateView(depth,NULL);
    WGPUTextureView rv = wgpuTextureCreateView(resolved,NULL);
    assert(cv && dv && rv);
    WGPURenderPipeline pipeline = graphics(s,module,mask,coverage,transparent);
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = cv;
    attachment.resolveTarget = rv;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Discard;
    attachment.clearValue = (WGPUColor){0,0,1,1};
    WGPURenderPassDepthStencilAttachment ds = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    ds.view = dv;
    ds.depthLoadOp = WGPULoadOp_Clear;
    ds.depthStoreOp = WGPUStoreOp_Discard;
    ds.depthClearValue = 1;
    WGPURenderPassDescriptor pass_desc = {.colorAttachmentCount=1,.colorAttachments=&attachment,.depthStencilAttachment=&ds};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&pass_desc);
    assert(pass);
    if (draw) {
        wgpuRenderPassEncoderSetPipeline(pass,pipeline);
        wgpuRenderPassEncoderDraw(pass,3,1,0,0);
    }
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    if (load_after_discard) {
        attachment.loadOp = WGPULoadOp_Load;
        ds.depthLoadOp = WGPULoadOp_Load;
        pass = wgpuCommandEncoderBeginRenderPass(encoder,&pass_desc);
        assert(pass);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }
    WGPUBuffer readback = buffer(s,512,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    texture_to_buffer(encoder,resolved,readback,0,2,2);
    wgpuRenderPipelineRelease(pipeline);
    wgpuTextureViewRelease(cv);
    wgpuTextureViewRelease(dv);
    wgpuTextureViewRelease(rv);
    wgpuTextureRelease(color);
    wgpuTextureRelease(depth);
    wgpuTextureRelease(resolved);
    submit(s,encoder);
    const unsigned char *data = map(s,readback,512);
    for (size_t y = 0; y < 2; ++y) for (size_t x = 0; x < 2; ++x)
        assert(!memcmp(data+y*256+x*4,expected,4));
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}
static void depth_only(state *s)
{
    WGPUTexture image = texture(s,WGPUTextureFormat_Depth32Float,WGPUTextureUsage_RenderAttachment,1);
    WGPUTextureView view = wgpuTextureCreateView(image,NULL);
    WGPUQuerySetDescriptor query_desc = {.type=WGPUQueryType_Occlusion,.count=1};
    WGPUQuerySet queries = wgpuDeviceCreateQuerySet(s->device,&query_desc);
    assert(view && queries);
    WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    depth.view = view;
    depth.depthLoadOp = WGPULoadOp_Clear;
    depth.depthStoreOp = WGPUStoreOp_Store;
    depth.depthClearValue = .5f;
    WGPURenderPassDescriptor descriptor = {.depthStencilAttachment=&depth,.occlusionQuerySet=queries};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
    assert(pass);
    wgpuRenderPassEncoderBeginOcclusionQuery(pass,0);
    wgpuRenderPassEncoderEndOcclusionQuery(pass);
    WGPURenderBundleEncoderDescriptor bundle_desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
    bundle_desc.depthStencilFormat = WGPUTextureFormat_Depth32Float;
    WGPURenderBundleEncoder builder = wgpuDeviceCreateRenderBundleEncoder(s->device,&bundle_desc);
    assert(builder);
    WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(builder,NULL);
    assert(bundle);
    wgpuRenderBundleEncoderRelease(builder);
    wgpuRenderPassEncoderExecuteBundles(pass,1,&bundle);
    wgpuRenderBundleRelease(bundle);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUBuffer result = buffer(s,8,WGPUBufferUsage_QueryResolve|WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(s,8,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    wgpuCommandEncoderResolveQuerySet(encoder,queries,0,1,result,0);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,result,0,readback,0,8);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(image);
    wgpuQuerySetRelease(queries);
    wgpuBufferRelease(result);
    submit(s,encoder);
    assert(*(const uint64_t *)map(s,readback,8) == 0);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}
static void negatives(state *s)
{
    WGPUTextureDescriptor image_desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    image_desc.format = WGPUTextureFormat_RGBA8Unorm;
    image_desc.size = (WGPUExtent3D){2,2,1};
    image_desc.sampleCount = 4;
    image_desc.usage = WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc;
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateTexture(s->device,&image_desc));
    expect_validation(s);
    image_desc.sampleCount = 2;
    image_desc.usage = WGPUTextureUsage_RenderAttachment;
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateTexture(s->device,&image_desc));
    expect_validation(s);
    WGPUTexture source = texture(s,WGPUTextureFormat_RGBA8Unorm,WGPUTextureUsage_RenderAttachment,1);
    WGPUTexture destination = texture(s,WGPUTextureFormat_RGBA8Unorm,WGPUTextureUsage_RenderAttachment,1);
    WGPUTextureView from = wgpuTextureCreateView(source,NULL), to = wgpuTextureCreateView(destination,NULL);
    assert(from && to);
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = from;
    color.resolveTarget = to;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor pass_desc = {.colorAttachmentCount=1,.colorAttachments=&color};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!wgpuCommandEncoderBeginRenderPass(encoder,&pass_desc));
    assert(!wgpuCommandEncoderFinish(encoder,NULL));
    expect_validation(s);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(from);
    wgpuTextureViewRelease(to);
    wgpuTextureRelease(source);
    wgpuTextureRelease(destination);
}
int main(void)
{
    state s = {0};
    consumer_init(&s);
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){"@vertex fn vertex(@builtin(vertex_index) i:u32)->@builtin(position) vec4f{"
        "if(i==0u){return vec4f(-1.0,-1.0,0.5,1.0);}if(i==1u){return vec4f(3.0,-1.0,0.5,1.0);}"
        "return vec4f(-1.0,3.0,0.5,1.0);}@fragment fn opaque()->@location(0) vec4f{return vec4f(1.0,0.0,0.0,1.0);}"
        "@fragment fn transparent()->@location(0) vec4f{return vec4f(1.0,0.0,0.0,0.0);}",WGPU_STRLEN};
    WGPUShaderModuleDescriptor desc = {.nextInChain=&source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s.device,&desc);
    assert(module);
    image_case(&s,module,UINT32_MAX,false,false,true,false,(const unsigned char[]){255,0,0,255});
    image_case(&s,module,0,false,false,true,false,(const unsigned char[]){0,0,255,255});
    image_case(&s,module,UINT32_MAX,true,false,true,false,(const unsigned char[]){255,0,0,255});
    image_case(&s,module,UINT32_MAX,false,false,false,false,(const unsigned char[]){0,0,255,255});
    image_case(&s,module,UINT32_MAX,false,false,true,true,(const unsigned char[]){0,0,0,0});
    image_case(&s,module,UINT32_MAX,true,true,true,false,(const unsigned char[]){0,0,255,255});
    image_case(&s,module,UINT32_MAX,false,true,true,false,(const unsigned char[]){255,0,0,0});
    depth_only(&s);
    negatives(&s);
    assert(s.validation_errors == 3);
    wgpuShaderModuleRelease(module);
    consumer_finish(&s);
    printf("PASS public MSAA: actual 4x color/depth, exact resolve pixels, mask0/zero-and-full alpha coverage, empty resolve, same-batch discard-zero and depth-only empty/query/bundle passes %s\n", adapter_class_scope());
    return 0;
}
