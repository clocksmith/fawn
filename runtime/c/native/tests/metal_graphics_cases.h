/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_TEST_METAL_GRAPHICS_CASES_H
#define DOE_NATIVE_TEST_METAL_GRAPHICS_CASES_H
#include "consumer.h"

static WGPURenderPipeline metal_triangle_pipeline(state *s)
{
    const char *code =
        "@vertex fn vertex(@builtin(vertex_index) index:u32)->@builtin(position) vec4f{"
        "var positions=array<vec2f,3>(vec2f(-1.0,-1.0),vec2f(1.0,-1.0),vec2f(-1.0,1.0));"
        "return vec4f(positions[index],0.0,1.0);}"
        "@fragment fn fragment()->@location(0) vec4f{return vec4f(1.0,0.0,0.0,1.0);}";
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code,WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader_descriptor = {.nextInChain=&source.chain};
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(s->device, &shader_descriptor);
    assert(shader);
    WGPUColorTargetState color = {.format=WGPUTextureFormat_RGBA8Unorm, .writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=shader, .targetCount=1, .targets=&color};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex.module = shader;
    descriptor.fragment = &fragment;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(s->device, &descriptor);
    wgpuShaderModuleRelease(shader);
    return pipeline;
}

static WGPUTexture metal_image(state *s)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = WGPUTextureFormat_RGBA8Unorm;
    descriptor.size = (WGPUExtent3D){8,8,1};
    descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
    return wgpuDeviceCreateTexture(s->device, &descriptor);
}

static void metal_triangle_record(state *s, WGPURenderPipeline pipeline, WGPUTextureView view,
    WGPUCommandEncoder encoder, WGPUStoreOp store)
{
    (void)s;
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = store;
    attachment.clearValue = (WGPUColor){0,0,1,1};
    WGPURenderPassDescriptor descriptor = {.colorAttachmentCount=1, .colorAttachments=&attachment};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &descriptor);
    assert(pass);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

static void metal_graphics_pixels(state *s)
{
    WGPURenderPipeline pipeline = metal_triangle_pipeline(s);
    WGPUTexture image = metal_image(s);
    WGPUTextureView view = wgpuTextureCreateView(image, NULL);
    WGPUBuffer readback = buffer(s, 2048, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    assert(pipeline && image && view);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    metal_triangle_record(s, pipeline, view, encoder, WGPUStoreOp_Store);
    texture_to_buffer(encoder, image, readback, 0, 8, 8);
    /* Command packets, not public handles, retain the actual native inputs. */
    wgpuRenderPipelineRelease(pipeline);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(image);
    submit(s, encoder);
    const unsigned char *pixels = map(s, readback, 2048);
    unsigned red = 0, blue = 0;
    for (size_t y = 0; y < 8; ++y) for (size_t x = 0; x < 8; ++x) {
        const unsigned char *pixel = pixels + y * 256 + x * 4;
        assert(pixel[1] == 0 && pixel[3] == 255);
        assert((pixel[0] == 255 && pixel[2] == 0) || (pixel[0] == 0 && pixel[2] == 255));
        red += pixel[0] == 255;
        blue += pixel[2] == 255;
    }
    assert(red >= 28 && red <= 36 && red + blue == 64);
    /* Asymmetric independent corner oracle also catches vertical inversion. */
    assert(pixels[0] == 0 && pixels[2] == 255);
    assert(pixels[7 * 256] == 255 && pixels[7 * 256 + 2] == 0);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);

    image = metal_image(s);
    view = wgpuTextureCreateView(image, NULL);
    pipeline = metal_triangle_pipeline(s);
    readback = buffer(s, 2048, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    metal_triangle_record(s, pipeline, view, encoder, WGPUStoreOp_Discard);
    texture_to_buffer(encoder, image, readback, 0, 8, 8);
    submit(s, encoder);
    pixels = map(s, readback, 2048);
    for (size_t y = 0; y < 8; ++y) for (size_t x = 0; x < 32; ++x) assert(pixels[y * 256 + x] == 0);
    wgpuBufferUnmap(readback);
    /* Packed rows exercise QueueWriteTexture repacking, preserving exact bytes. */
    unsigned char upload[256];
    for (size_t i = 0; i < sizeof(upload); ++i) upload[i] = (unsigned char)(i * 37 + 11);
    WGPUTexelCopyTextureInfo destination = {.texture=image};
    WGPUTexelCopyBufferLayout layout = {.bytesPerRow=32, .rowsPerImage=8};
    WGPUExtent3D extent = {8,8,1};
    wgpuQueueWriteTexture(s->queue, &destination, upload, sizeof(upload), &layout, &extent);
    encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    texture_to_buffer(encoder, image, readback, 0, 8, 8);
    submit(s, encoder);
    pixels = map(s, readback, 2048);
    for (size_t y = 0; y < 8; ++y) assert(!memcmp(pixels + y * 256, upload + y * 32, 32));
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    wgpuRenderPipelineRelease(pipeline);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(image);
}
#endif
