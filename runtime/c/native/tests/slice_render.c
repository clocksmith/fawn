/* SPDX-License-Identifier: MIT
 * Public rendering into 3D texture depth slices (colorAttachments[i].depthSlice):
 * two slices of one mip as two color attachments of one pass, one drawn by a
 * two-target fragment shader over the left half; a slice of mip 1; a discarded
 * slice that reads back as zeros; untouched slices keep their creation zeros.
 * Every slice is read back with one texture-to-buffer copy per mip. Admission
 * negatives: a missing or out-of-range slice and a repeated slice. */
#include "consumer.h"

enum { WIDTH = 4, HEIGHT = 2, DEPTH = 4, PITCH = 256 };

static WGPUShaderModule module(state *s, const char *code)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code, WGPU_STRLEN};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain = &source.chain};
    WGPUShaderModule result = wgpuDeviceCreateShaderModule(s->device, &descriptor);
    assert(result);
    return result;
}

/* Left half of clip space as two triangles; slot 0 white, slot 1 blue. */
static const char left_half[] =
    "@vertex fn vertex(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {"
    "if (i == 0u || i == 3u) { return vec4f(-1.0, -1.0, 0.0, 1.0); }"
    "if (i == 1u) { return vec4f(0.0, -1.0, 0.0, 1.0); }"
    "if (i == 2u || i == 4u) { return vec4f(0.0, 1.0, 0.0, 1.0); }"
    "return vec4f(-1.0, 1.0, 0.0, 1.0); }"
    "struct Out { @location(0) a: vec4f, @location(1) b: vec4f }"
    "@fragment fn fragment() -> Out { return Out(vec4f(1.0), vec4f(0.0, 0.0, 1.0, 1.0)); }";

static WGPURenderPassColorAttachment slice(WGPUTextureView view, uint32_t depth_slice, WGPUColor clear,
    WGPUStoreOp store)
{
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.depthSlice = depth_slice;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = store;
    attachment.clearValue = clear;
    return attachment;
}

static WGPUTextureView mip_view(WGPUTexture texture, uint32_t mip)
{
    WGPUTextureViewDescriptor descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    descriptor.dimension = WGPUTextureViewDimension_3D;
    descriptor.baseMipLevel = mip;
    descriptor.mipLevelCount = 1;
    WGPUTextureView view = wgpuTextureCreateView(texture, &descriptor);
    assert(view);
    return view;
}

static void rejected(state *s, const WGPURenderPassColorAttachment *attachments, size_t count)
{
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPURenderPassDescriptor descriptor = {.colorAttachmentCount = count, .colorAttachments = attachments};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &descriptor);
    assert(pass);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    finish_invalid(encoder);
    wgpuCommandEncoderRelease(encoder);
    expect_validation(s);
}

static void expect_texel(const unsigned char *bytes, uint32_t z, uint32_t height, uint32_t x, uint32_t y,
    const unsigned char expected[4])
{
    const unsigned char *texel = bytes + ((size_t)z * height + y) * PITCH + (size_t)x * 4;
    if (memcmp(texel, expected, 4)) {
        fprintf(stderr, "slice %u texel (%u,%u): %u %u %u %u\n", z, x, y, texel[0], texel[1], texel[2], texel[3]);
        assert(!"unexpected slice texel");
    }
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.dimension = WGPUTextureDimension_3D;
    descriptor.format = WGPUTextureFormat_RGBA8Unorm;
    descriptor.size = (WGPUExtent3D){WIDTH, HEIGHT, DEPTH};
    descriptor.mipLevelCount = 2;
    descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    WGPUTexture volume = wgpuDeviceCreateTexture(s.device, &descriptor);
    assert(volume);
    WGPUTextureView mip0 = mip_view(volume, 0), mip1 = mip_view(volume, 1);
    const WGPUColor red = {1, 0, 0, 1}, green = {0, 1, 0, 1}, white = {1, 1, 1, 1};

    WGPUShaderModule shader = module(&s, left_half);
    WGPUColorTargetState targets[2] = {WGPU_COLOR_TARGET_STATE_INIT, WGPU_COLOR_TARGET_STATE_INIT};
    targets[0].format = targets[1].format = WGPUTextureFormat_RGBA8Unorm;
    WGPUFragmentState fragment = {.module = shader, .targetCount = 2, .targets = targets};
    WGPURenderPipelineDescriptor pipeline_descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pipeline_descriptor.vertex.module = shader;
    pipeline_descriptor.fragment = &fragment;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(s.device, &pipeline_descriptor);
    assert(pipeline);

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    /* Slices 1 (red) and 3 (green) of mip 0 in one pass; left halves drawn. */
    WGPURenderPassColorAttachment pair[2] = {slice(mip0, 1, red, WGPUStoreOp_Store),
        slice(mip0, 3, green, WGPUStoreOp_Store)};
    WGPURenderPassDescriptor pass_descriptor = {.colorAttachmentCount = 2, .colorAttachments = pair};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_descriptor);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    /* Slice 1 of mip 1 (2x1x2) cleared green; slice 2 of mip 0 discarded. */
    WGPURenderPassColorAttachment single = slice(mip1, 1, green, WGPUStoreOp_Store);
    pass_descriptor = (WGPURenderPassDescriptor){.colorAttachmentCount = 1, .colorAttachments = &single};
    pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_descriptor);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    single = slice(mip0, 2, white, WGPUStoreOp_Discard);
    pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_descriptor);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUBuffer readback = buffer(&s, 2 * DEPTH * HEIGHT * PITCH, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    const uint64_t mip1_offset = (uint64_t)DEPTH * HEIGHT * PITCH;
    for (uint32_t mip = 0; mip < 2; ++mip) {
        WGPUTexelCopyTextureInfo source = {.texture = volume, .mipLevel = mip};
        WGPUTexelCopyBufferInfo destination = {.buffer = readback,
            .layout = {.offset = mip ? mip1_offset : 0, .bytesPerRow = PITCH, .rowsPerImage = HEIGHT >> mip}};
        WGPUExtent3D size = {WIDTH >> mip, HEIGHT >> mip, DEPTH >> mip};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &size);
    }
    submit(&s, encoder);

    const unsigned char *bytes = map(&s, readback, 2 * DEPTH * HEIGHT * PITCH);
    const unsigned char zero[4] = {0, 0, 0, 0}, red_bytes[4] = {255, 0, 0, 255}, green_bytes[4] = {0, 255, 0, 255},
        white_bytes[4] = {255, 255, 255, 255}, blue_bytes[4] = {0, 0, 255, 255};
    for (uint32_t y = 0; y < HEIGHT; ++y) {
        for (uint32_t x = 0; x < WIDTH; ++x) {
            const bool left = x < WIDTH / 2;
            expect_texel(bytes, 0, HEIGHT, x, y, zero);                             /* never rendered */
            expect_texel(bytes, 1, HEIGHT, x, y, left ? white_bytes : red_bytes);  /* slot 0 */
            expect_texel(bytes, 2, HEIGHT, x, y, zero);                             /* discarded */
            expect_texel(bytes, 3, HEIGHT, x, y, left ? blue_bytes : green_bytes); /* slot 1 */
        }
    }
    for (uint32_t x = 0; x < WIDTH / 2; ++x) {
        expect_texel(bytes + mip1_offset, 0, HEIGHT / 2, x, 0, zero);
        expect_texel(bytes + mip1_offset, 1, HEIGHT / 2, x, 0, green_bytes);
    }
    wgpuBufferUnmap(readback);

    /* Admission: a 3D view needs a slice below its mip depth, used once. */
    WGPURenderPassColorAttachment bad = slice(mip0, WGPU_DEPTH_SLICE_UNDEFINED, red, WGPUStoreOp_Store);
    rejected(&s, &bad, 1);
    bad = slice(mip1, 2, red, WGPUStoreOp_Store);
    rejected(&s, &bad, 1);
    WGPURenderPassColorAttachment repeated[2] = {slice(mip0, 2, red, WGPUStoreOp_Store),
        slice(mip0, 2, green, WGPUStoreOp_Store)};
    rejected(&s, repeated, 2);

    wgpuBufferRelease(readback);
    wgpuRenderPipelineRelease(pipeline);
    wgpuShaderModuleRelease(shader);
    wgpuTextureViewRelease(mip0);
    wgpuTextureViewRelease(mip1);
    wgpuTextureRelease(volume);
    consumer_finish(&s);
    printf("PASS 3D depth-slice attachments: two slices of one mip in one pass with a two-target draw, a mip-1 slice, a discarded slice reading zeros and untouched slices keeping creation zeros; missing, out-of-range and repeated slices rejected %s\n",
        adapter_class_scope());
}
