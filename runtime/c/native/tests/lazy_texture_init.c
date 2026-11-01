/* SPDX-License-Identifier: MIT
 * Texture creation does not wait, yet every first access observes zero
 * initialization: an untouched read, a partial write, a Load pass, a Clear pass,
 * a texture-to-texture copy and a non-renderable format staged without waiting. */
#include "consumer.h"

enum { SIDE = 64, ROW = SIDE * 4 };

static WGPUTexture make(state *s, WGPUTextureFormat format, WGPUTextureUsage usage)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = format;
    descriptor.usage = usage;
    descriptor.size = (WGPUExtent3D){SIDE, SIDE, 1};
    WGPUTexture texture = wgpuDeviceCreateTexture(s->device, &descriptor);
    assert(texture);
    return texture;
}

static void pass(state *s, WGPUTexture texture, WGPULoadOp load)
{
    WGPUTextureView view = wgpuTextureCreateView(texture, NULL);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = view;
    color.loadOp = load;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = (WGPUColor){1, 0, 0, 1};
    WGPURenderPassDescriptor descriptor = {.colorAttachmentCount = 1, .colorAttachments = &color};
    WGPURenderPassEncoder render = wgpuCommandEncoderBeginRenderPass(encoder, &descriptor);
    wgpuRenderPassEncoderEnd(render);
    wgpuRenderPassEncoderRelease(render);
    submit(s, encoder);
    wgpuTextureViewRelease(view);
}

/* Reads the whole texture; each texel's first byte is compared row by row. */
static void expect(state *s, WGPUTexture texture, uint32_t texel_bytes, const unsigned char *inside,
    uint32_t region, const unsigned char *outside)
{
    WGPUBuffer readback = buffer(s, (uint64_t)ROW * SIDE, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    texture_to_buffer(encoder, texture, readback, 0, SIDE, SIDE);
    submit(s, encoder);
    work_done(s);
    const unsigned char *bytes = map(s, readback, (size_t)ROW * SIDE);
    for (uint32_t y = 0; y < SIDE; ++y) {
        for (uint32_t x = 0; x < SIDE; ++x) {
            const unsigned char *want = x < region && y < region ? inside : outside;
            assert(!memcmp(bytes + (size_t)y * ROW + (size_t)x * texel_bytes, want, texel_bytes));
        }
    }
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}

int main(void)
{
    consumer_line_output();
    state s = {0};
    consumer_init(&s);
    const unsigned char zero[4] = {0}, white[4] = {255, 255, 255, 255}, red[4] = {255, 0, 0, 255};
    const WGPUTextureUsage attachment = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc |
        WGPUTextureUsage_CopyDst;

    WGPUTexture untouched = make(&s, WGPUTextureFormat_RGBA8Unorm, attachment);
    expect(&s, untouched, 4, zero, 0, zero);

    WGPUTexture partial = make(&s, WGPUTextureFormat_RGBA8Unorm, attachment);
    unsigned char patch[8 * 8 * 4];
    memset(patch, 255, sizeof(patch));
    WGPUTexelCopyTextureInfo destination = {.texture = partial, .aspect = WGPUTextureAspect_All};
    WGPUTexelCopyBufferLayout layout = {.bytesPerRow = 8 * 4, .rowsPerImage = 8};
    WGPUExtent3D extent = {8, 8, 1};
    wgpuQueueWriteTexture(s.queue, &destination, patch, sizeof(patch), &layout, &extent);
    expect(&s, partial, 4, white, 8, zero);

    WGPUTexture loaded = make(&s, WGPUTextureFormat_RGBA8Unorm, attachment);
    pass(&s, loaded, WGPULoadOp_Load);
    expect(&s, loaded, 4, zero, 0, zero);

    WGPUTexture cleared = make(&s, WGPUTextureFormat_RGBA8Unorm, attachment);
    pass(&s, cleared, WGPULoadOp_Clear);
    expect(&s, cleared, 4, red, SIDE, red);

    /* Copying an untouched source into a partially written destination. */
    WGPUTexture source = make(&s, WGPUTextureFormat_RGBA8Unorm, attachment);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    WGPUTexelCopyTextureInfo from = {.texture = source}, to = {.texture = cleared};
    WGPUExtent3D half = {SIDE / 2, SIDE / 2, 1};
    wgpuCommandEncoderCopyTextureToTexture(encoder, &from, &to, &half);
    submit(&s, encoder);
    expect(&s, cleared, 4, zero, SIDE / 2, red);

    /* Not attachment-capable: zeroed by a staged upload that nothing waits for. */
    WGPUTexture shared_exponent = make(&s, WGPUTextureFormat_RGB9E5Ufloat,
        WGPUTextureUsage_CopySrc | WGPUTextureUsage_TextureBinding);
    expect(&s, shared_exponent, 4, zero, 0, zero);

    /* Destroying a texture whose initialization may still run is safe. */
    WGPUTexture discarded = make(&s, WGPUTextureFormat_RGB9E5Ufloat, WGPUTextureUsage_CopySrc);
    wgpuTextureDestroy(discarded);
    wgpuTextureRelease(discarded);

    WGPUTexture all[] = {untouched, partial, loaded, cleared, source, shared_exponent};
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i) wgpuTextureRelease(all[i]);
    work_done(&s);
    consumer_finish(&s);
    printf("PASS zero initialization without creation waits: untouched read, partial write, Load/Clear passes, copy and staged format %s\n",
        adapter_class_scope());
    return 0;
}
