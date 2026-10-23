/* SPDX-License-Identifier: MIT */
#include "consumer.h"

static WGPURenderPipeline program(state *s)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){
        "@vertex fn vertex()->@builtin(position) vec4f{return vec4f(0.0,0.0,0.0,1.0);}"
        "@fragment fn fragment()->@location(0) vec4f{return vec4f(1.0);}",WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader_desc = {.nextInChain=&source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device,&shader_desc);
    assert(module);
    WGPUColorTargetState color = {.format=WGPUTextureFormat_RGBA8Unorm,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=module,.targetCount=1,.targets=&color};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex.module = module;
    descriptor.fragment = &fragment;
    WGPURenderPipeline result = wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
    wgpuShaderModuleRelease(module);
    assert(result);
    return result;
}
static WGPURenderBundle bundle(state *s, WGPURenderPipeline pipeline, uint32_t count)
{
    WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
    WGPURenderBundleEncoderDescriptor descriptor = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
    descriptor.colorFormatCount = 1;
    descriptor.colorFormats = &format;
    WGPURenderBundleEncoder encoder = wgpuDeviceCreateRenderBundleEncoder(s->device,&descriptor);
    assert(encoder);
    if (count) wgpuRenderBundleEncoderSetPipeline(encoder,pipeline);
    for (uint32_t i=0;i<count;++i) wgpuRenderBundleEncoderDraw(encoder,0,1,0,0);
    WGPURenderBundle result = wgpuRenderBundleEncoderFinish(encoder,NULL);
    wgpuRenderBundleEncoderRelease(encoder);
    assert(result);
    return result;
}
int main(void)
{
    state s = {0};
    consumer_init(&s);
    WGPURenderPipeline pipeline = program(&s);
    WGPURenderBundle empty = bundle(&s,pipeline,0), one = bundle(&s,pipeline,1);
    WGPUTexture image = texture(&s,WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc,1);
    WGPUTextureView view = wgpuTextureCreateView(image,NULL);
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = view;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = (WGPUColor){0,0,1,1};
    WGPURenderPassMaxDrawCount budget = WGPU_RENDER_PASS_MAX_DRAW_COUNT_INIT;
    assert(budget.maxDrawCount == 50000000);
    WGPURenderPassDescriptor descriptor = {.nextInChain=&budget.chain,
        .colorAttachmentCount=1,.colorAttachments=&color};
    WGPUBuffer arguments = buffer(&s,16,WGPUBufferUsage_Indirect);
    for (unsigned variant=0;variant<6;++variant) {
        budget.maxDrawCount = variant < 2 ? 0 : 1;
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
        assert(pass);
        wgpuRenderPassEncoderSetPipeline(pass,pipeline);
        wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Validation);
        if (variant == 0) wgpuRenderPassEncoderDraw(pass,0,1,0,0);
        else if (variant == 1) wgpuRenderPassEncoderDrawIndirect(pass,arguments,0);
        else if (variant == 2) {
            wgpuRenderPassEncoderDraw(pass,0,1,0,0);
            wgpuRenderPassEncoderDraw(pass,0,1,0,0);
        } else if (variant == 3) {
            wgpuRenderPassEncoderExecuteBundles(pass,1,&one);
            wgpuRenderPassEncoderDrawIndirect(pass,arguments,0);
        } else if (variant == 4) {
            const WGPURenderBundle bundles[] = {empty,one,one};
            wgpuRenderPassEncoderExecuteBundles(pass,3,bundles);
        } else {
            wgpuRenderPassEncoderDrawIndirect(pass,arguments,0);
            wgpuRenderPassEncoderExecuteBundles(pass,1,&one);
        }
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        assert(!wgpuCommandEncoderFinish(encoder,NULL));
        wgpuCommandEncoderRelease(encoder);
        expect_validation(&s);
    }
    for (unsigned variant=0;variant<2;++variant) {
        WGPUChainedStruct unknown = {.sType=WGPUSType_ShaderSourceWGSL};
        budget.chain.next = variant ? &budget.chain : NULL;
        descriptor.nextInChain = variant ? &budget.chain : &unknown;
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
        wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Validation);
        assert(!wgpuCommandEncoderBeginRenderPass(encoder,&descriptor));
        assert(!wgpuCommandEncoderFinish(encoder,NULL));
        wgpuCommandEncoderRelease(encoder);
        expect_validation(&s);
    }
    budget.chain.next = NULL;
    budget.maxDrawCount = 0;
    descriptor.nextInChain = &budget.chain;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
    assert(pass);
    wgpuRenderPassEncoderExecuteBundles(pass,1,&empty);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    descriptor.nextInChain = NULL;
    pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
    assert(pass);
    wgpuRenderPassEncoderSetPipeline(pass,pipeline);
    wgpuRenderPassEncoderDraw(pass,0,1,0,0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUBuffer readback = buffer(&s,512,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    texture_to_buffer(encoder,image,readback,0,2,2);
    wgpuRenderPipelineRelease(pipeline);
    wgpuRenderBundleRelease(empty);
    wgpuRenderBundleRelease(one);
    wgpuBufferRelease(arguments);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(image);
    submit(&s,encoder);
    const unsigned char *bytes = map(&s,readback,512);
    for (size_t y=0;y<2;++y) for (size_t x=0;x<2;++x) {
        const unsigned char *pixel = bytes+y*256+x*4;
        assert(pixel[0]==0 && pixel[1]==0 && pixel[2]==255 && pixel[3]==255);
    }
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    consumer_finish(&s);
    puts("PASS public maxDrawCount chain: explicit zero, omitted default, direct/indirect no-op counting, aggregate bundles, empty bundle, unknown/duplicate chain invalidation and real zero-budget clear (CPU Vulkan only)");
}
