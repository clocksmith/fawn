/* SPDX-License-Identifier: MIT
 * Separate paired-provider diagnostic, not a replacement for msaa_sampling.c.
 * Exact 64-pixel oracle: blue outside scissor, red inside; mismatches fail.
 * Reads every resolved pixel to preserve the complete differential evidence. */
#include "consumer.h"

int main(void)
{
    state s = {0};
    consumer_init(&s);
    const char *code =
        "@vertex fn vs(@builtin(vertex_index) i:u32)->@builtin(position) vec4f{"
        "var p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));"
        "return vec4f(p[i],0,1);}"
        "@fragment fn fs()->@location(0) vec4f{return vec4f(1,0,0,1);}";
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code,WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    shader.nextInChain = &source.chain;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s.device,&shader);
    assert(module);
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_RGBA8Unorm;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = (WGPUStringView){"fs",2};
    fragment.targetCount = 1;
    fragment.targets = &target;
    WGPURenderPipelineDescriptor pipeline_desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pipeline_desc.vertex.module = module;
    pipeline_desc.vertex.entryPoint = (WGPUStringView){"vs",2};
    pipeline_desc.fragment = &fragment;
    pipeline_desc.multisample.count = 4;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(s.device,&pipeline_desc);
    assert(pipeline);
    WGPUTextureDescriptor texture_desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texture_desc.dimension = WGPUTextureDimension_2D;
    texture_desc.format = target.format;
    texture_desc.size = (WGPUExtent3D){8,8,1};
    texture_desc.sampleCount = 4;
    texture_desc.usage = WGPUTextureUsage_RenderAttachment;
    WGPUTexture msaa = wgpuDeviceCreateTexture(s.device,&texture_desc);
    assert(msaa);
    texture_desc.sampleCount = 1;
    texture_desc.usage |= WGPUTextureUsage_CopySrc;
    WGPUTexture resolved = wgpuDeviceCreateTexture(s.device,&texture_desc);
    assert(resolved);
    WGPUTextureView multisampled_view = wgpuTextureCreateView(msaa,NULL);
    WGPUTextureView resolved_view = wgpuTextureCreateView(resolved,NULL);
    assert(multisampled_view && resolved_view);
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = multisampled_view;
    color.resolveTarget = resolved_view;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = (WGPUColor){0,0,1,1};
    WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&pass_desc);
    assert(pass);
    wgpuRenderPassEncoderSetPipeline(pass,pipeline);
    wgpuRenderPassEncoderSetScissorRect(pass,4,0,4,8);
    wgpuRenderPassEncoderDraw(pass,3,1,0,0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUBuffer readback = buffer(&s,256 * 8,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    texture_to_buffer(encoder,resolved,readback,0,8,8);
    submit(&s,encoder);
    const unsigned char *bytes = map(&s,readback,256 * 8);
    unsigned failures = 0;
    for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 8; ++x) {
        const unsigned char expected[4] = {x >= 4 ? 255 : 0,0,x < 4 ? 255 : 0,255};
        const unsigned char *pixel = bytes + y * 256 + x * 4;
        printf("pixel %u %u = %u,%u,%u,%u expected %u,%u,%u,%u\n",x,y,
            pixel[0],pixel[1],pixel[2],pixel[3],expected[0],expected[1],expected[2],expected[3]);
        if (memcmp(pixel,expected,4)) ++failures;
    }
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    wgpuTextureViewRelease(multisampled_view);
    wgpuTextureViewRelease(resolved_view);
    wgpuTextureRelease(msaa);
    wgpuTextureRelease(resolved);
    wgpuRenderPipelineRelease(pipeline);
    wgpuShaderModuleRelease(module);
    consumer_finish(&s);
    printf("exact64-pixel failures=%u\n",failures);
    return failures ? 1 : 0;
}
