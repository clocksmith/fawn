/* SPDX-License-Identifier: MIT
 * Public sampler maxAnisotropy: admission (all filters linear, nonzero, values
 * above the device maximum clamp) and one real sampled draw through a 16x
 * anisotropic sampler. The oracle samples an asymmetric texture 1:1 at texel
 * centers, where every filter returns the exact texel; it checks that the
 * anisotropic sampler is bound and works, not anisotropic filtering quality.
 * The C compiler admits only explicit-LOD textureSampleLevel (no implicit or
 * gradient sampling), so derivative-driven anisotropic footprints are not
 * exercised by this draw. */
#include "consumer.h"

enum { SIZE = 8, PITCH = 256 };

static WGPUSampler sampler(state *s, uint16_t anisotropy, WGPUFilterMode min_filter)
{
    WGPUSamplerDescriptor descriptor = WGPU_SAMPLER_DESCRIPTOR_INIT;
    descriptor.magFilter = WGPUFilterMode_Linear;
    descriptor.minFilter = min_filter;
    descriptor.mipmapFilter = WGPUMipmapFilterMode_Linear;
    descriptor.maxAnisotropy = anisotropy;
    return wgpuDeviceCreateSampler(s->device, &descriptor);
}

static void rejected(state *s, uint16_t anisotropy, WGPUFilterMode min_filter)
{
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    assert(!sampler(s, anisotropy, min_filter));
    expect_validation(s);
}

static void draw(state *s, WGPUSampler filtering, WGPUTextureView image, unsigned char out[SIZE][SIZE][4])
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){
        "@group(0) @binding(0) var image: texture_2d<f32>;"
        "@group(0) @binding(1) var image_sampler: sampler;"
        "@vertex fn vertex(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {"
        "if (i == 0u) { return vec4f(-1.0, -1.0, 0.0, 1.0); }"
        "if (i == 1u) { return vec4f(3.0, -1.0, 0.0, 1.0); }"
        "return vec4f(-1.0, 3.0, 0.0, 1.0); }"
        "@fragment fn fragment(@builtin(position) p: vec4f) -> @location(0) vec4f {"
        "return textureSampleLevel(image, image_sampler, p.xy / 8.0, 0.0); }", WGPU_STRLEN};
    WGPUShaderModuleDescriptor module_desc = {.nextInChain = &source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device, &module_desc);
    assert(module);
    WGPUColorTargetState color = {.format = WGPUTextureFormat_RGBA8Unorm, .writeMask = WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module = module, .targetCount = 1, .targets = &color};
    WGPURenderPipelineDescriptor pipeline_desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pipeline_desc.vertex.module = module;
    pipeline_desc.fragment = &fragment;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(s->device, &pipeline_desc);
    assert(pipeline);
    WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroupEntry entries[2] = {{.binding = 0, .textureView = image}, {.binding = 1, .sampler = filtering}};
    WGPUBindGroupDescriptor group_desc = {.layout = layout, .entryCount = 2, .entries = entries};
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(s->device, &group_desc);
    assert(layout && group);
    WGPUTextureDescriptor target_desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    target_desc.format = WGPUTextureFormat_RGBA8Unorm;
    target_desc.size = (WGPUExtent3D){SIZE, SIZE, 1};
    target_desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    WGPUTexture target = wgpuDeviceCreateTexture(s->device, &target_desc);
    WGPUTextureView view = wgpuTextureCreateView(target, NULL);
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor pass_desc = {.colorAttachmentCount = 1, .colorAttachments = &attachment};
    WGPUBuffer readback = buffer(s, (uint64_t)PITCH * SIZE, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    texture_to_buffer(encoder, target, readback, 0, SIZE, SIZE);
    wgpuBindGroupRelease(group); /* Early caller releases before submission. */
    wgpuBindGroupLayoutRelease(layout);
    wgpuRenderPipelineRelease(pipeline);
    wgpuShaderModuleRelease(module);
    submit(s, encoder);
    const unsigned char *bytes = map(s, readback, (size_t)PITCH * SIZE);
    for (uint32_t y = 0; y < SIZE; ++y) memcpy(out[y], bytes + y * PITCH, SIZE * 4);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(target);
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    rejected(&s, 0, WGPUFilterMode_Linear);
    rejected(&s, 4, WGPUFilterMode_Nearest);
    WGPUSampler clamped = sampler(&s, 1000, WGPUFilterMode_Linear); /* Above every device maximum. */
    WGPUSampler anisotropic = sampler(&s, 16, WGPUFilterMode_Linear);
    WGPUSampler isotropic = sampler(&s, 1, WGPUFilterMode_Linear);
    assert(clamped && anisotropic && isotropic);
    wgpuSamplerRelease(clamped);
    WGPUTextureDescriptor image_desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    image_desc.format = WGPUTextureFormat_RGBA8Unorm;
    image_desc.size = (WGPUExtent3D){SIZE, SIZE, 1};
    image_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    WGPUTexture image = wgpuDeviceCreateTexture(s.device, &image_desc);
    unsigned char texels[SIZE][SIZE][4];
    for (uint32_t y = 0; y < SIZE; ++y)
        for (uint32_t x = 0; x < SIZE; ++x) {
            const unsigned char value[4] = {(unsigned char)(x < 3 ? 255 : 0), (unsigned char)(y < 5 ? 128 : 0),
                (unsigned char)(x >= 3 ? 255 : 0), 255};
            memcpy(texels[y][x], value, 4);
        }
    WGPUTexelCopyTextureInfo destination = {.texture = image};
    WGPUTexelCopyBufferLayout layout = {.bytesPerRow = SIZE * 4, .rowsPerImage = SIZE};
    wgpuQueueWriteTexture(s.queue, &destination, texels, sizeof(texels), &layout, &(WGPUExtent3D){SIZE, SIZE, 1});
    WGPUTextureView view = wgpuTextureCreateView(image, NULL);
    unsigned char anisotropic_pixels[SIZE][SIZE][4], isotropic_pixels[SIZE][SIZE][4];
    draw(&s, anisotropic, view, anisotropic_pixels);
    draw(&s, isotropic, view, isotropic_pixels);
    assert(memcmp(anisotropic_pixels, texels, sizeof(texels)) == 0);
    assert(memcmp(isotropic_pixels, texels, sizeof(texels)) == 0);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(image);
    wgpuSamplerRelease(anisotropic);
    wgpuSamplerRelease(isotropic);
    assert(s.validation_errors == 2);
    consumer_finish(&s);
    printf("PASS public sampler anisotropy: zero and non-linear-filter rejection, above-maximum clamp, real 16x "
        "anisotropic explicit-LOD sampled draw with exact asymmetric texel-center pixels %s\n", adapter_class_scope());
    return 0;
}
