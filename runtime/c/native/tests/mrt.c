/* SPDX-License-Identifier: MIT
 * Public multiple color attachments through one native pass: a sparse four-slot
 * layout written by one fragment shader (per-slot blend, write mask, clear and
 * integer format), the same draw replayed from a sparse-format render bundle,
 * 4x MSAA resolve of two slots, and limit/format/layout negatives. */
#include "consumer.h"

enum { SIZE = 8, PITCH = 256 };

static WGPUTexture target(state *s, WGPUTextureFormat format, uint32_t samples)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = format;
    descriptor.size = (WGPUExtent3D){SIZE, SIZE, 1};
    descriptor.sampleCount = samples;
    descriptor.usage = WGPUTextureUsage_RenderAttachment | (samples == 1 ? WGPUTextureUsage_CopySrc : 0);
    WGPUTexture result = wgpuDeviceCreateTexture(s->device, &descriptor);
    assert(result);
    return result;
}

static WGPUShaderModule module(state *s, const char *code)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code, WGPU_STRLEN};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain = &source.chain};
    WGPUShaderModule result = wgpuDeviceCreateShaderModule(s->device, &descriptor);
    assert(result);
    return result;
}

/* Left half of clip space as two triangles: x in [-1, 0], all y. */
#define LEFT_HALF "@vertex fn vertex(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {" \
    "if (i == 0u || i == 3u) { return vec4f(-1.0, -1.0, 0.0, 1.0); }" \
    "if (i == 1u) { return vec4f(0.0, -1.0, 0.0, 1.0); }" \
    "if (i == 2u || i == 4u) { return vec4f(0.0, 1.0, 0.0, 1.0); }" \
    "return vec4f(-1.0, 1.0, 0.0, 1.0); }"

static WGPURenderPipeline render_pipeline(state *s, WGPUShaderModule shader, const WGPUColorTargetState *targets,
    size_t count, uint32_t samples)
{
    WGPUFragmentState fragment = {.module = shader, .targetCount = count, .targets = targets};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex.module = shader;
    descriptor.fragment = &fragment;
    descriptor.multisample.count = samples;
    return wgpuDeviceCreateRenderPipeline(s->device, &descriptor);
}

static void readback(state *s, WGPUTexture texture, uint32_t texel_bytes, unsigned char out[SIZE][SIZE][16])
{
    WGPUBuffer buffer_handle = buffer(s, (uint64_t)PITCH * SIZE, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPUTexelCopyTextureInfo source = {.texture = texture};
    WGPUTexelCopyBufferInfo destination = {.buffer = buffer_handle, .layout = {.bytesPerRow = PITCH, .rowsPerImage = SIZE}};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &(WGPUExtent3D){SIZE, SIZE, 1});
    submit(s, encoder);
    const unsigned char *bytes = map(s, buffer_handle, (size_t)PITCH * SIZE);
    for (uint32_t y = 0; y < SIZE; ++y)
        for (uint32_t x = 0; x < SIZE; ++x) memcpy(out[y][x], bytes + y * PITCH + x * texel_bytes, texel_bytes);
    wgpuBufferUnmap(buffer_handle);
    wgpuBufferRelease(buffer_handle);
}

static void expect_halves(state *s, WGPUTexture texture, uint32_t texel_bytes, const void *left, const void *right,
    const char *name)
{
    unsigned char pixels[SIZE][SIZE][16];
    readback(s, texture, texel_bytes, pixels);
    for (uint32_t y = 0; y < SIZE; ++y)
        for (uint32_t x = 0; x < SIZE; ++x)
            if (memcmp(pixels[y][x], x < SIZE / 2 ? left : right, texel_bytes) != 0) {
                fprintf(stderr, "%s pixel (%u,%u) mismatch\n", name, x, y);
                abort();
            }
}

static WGPURenderPassColorAttachment unused_slot(void)
{
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    return attachment;
}

static WGPURenderPassColorAttachment color(WGPUTextureView view, WGPUColor clear)
{
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    attachment.clearValue = clear;
    return attachment;
}

static const char sparse_shader[] =
    "struct Out { @location(0) first: vec4f, @location(2) integer: vec4u, @location(3) masked: vec4f }"
    LEFT_HALF
    "@fragment fn fragment() -> Out {"
    "return Out(vec4f(1.0, 0.0, 0.0, 0.0), vec4u(7u, 11u, 13u, 17u), vec4f(1.0, 1.0, 1.0, 1.0)); }";

/* Slot 0 additive RGB blend keeping destination alpha, slot 1 unused, slot 2
 * integer (no blend), slot 3 green/alpha write mask: 8 + 16 + 8 = 32 bytes. */
static void sparse_targets(WGPUColorTargetState targets[4], WGPUBlendState *blend)
{
    *blend = (WGPUBlendState){.color = {.operation = WGPUBlendOperation_Add, .srcFactor = WGPUBlendFactor_One,
        .dstFactor = WGPUBlendFactor_One}, .alpha = {.operation = WGPUBlendOperation_Add,
        .srcFactor = WGPUBlendFactor_Zero, .dstFactor = WGPUBlendFactor_One}};
    targets[0] = (WGPUColorTargetState){.format = WGPUTextureFormat_RGBA8Unorm, .blend = blend,
        .writeMask = WGPUColorWriteMask_All};
    targets[1] = (WGPUColorTargetState){.format = WGPUTextureFormat_Undefined};
    targets[2] = (WGPUColorTargetState){.format = WGPUTextureFormat_RGBA32Uint, .writeMask = WGPUColorWriteMask_All};
    targets[3] = (WGPUColorTargetState){.format = WGPUTextureFormat_RGBA8Unorm,
        .writeMask = WGPUColorWriteMask_Green | WGPUColorWriteMask_Alpha};
}

static void sparse_pass(state *s, bool through_bundle)
{
    WGPUShaderModule shader = module(s, sparse_shader);
    WGPUColorTargetState targets[4];
    WGPUBlendState blend;
    sparse_targets(targets, &blend);
    WGPURenderPipeline drawn = render_pipeline(s, shader, targets, 4, 1);
    assert(drawn);
    WGPUTexture textures[3] = {target(s, WGPUTextureFormat_RGBA8Unorm, 1),
        target(s, WGPUTextureFormat_RGBA32Uint, 1), target(s, WGPUTextureFormat_RGBA8Unorm, 1)};
    WGPUTextureView views[3];
    for (int i = 0; i < 3; ++i) views[i] = wgpuTextureCreateView(textures[i], NULL);
    WGPURenderPassColorAttachment attachments[4] = {color(views[0], (WGPUColor){0, 0, 1, 1}),
        WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT, color(views[1], (WGPUColor){1, 2, 3, 4}),
        color(views[2], (WGPUColor){1, 0, 0, 0})};
    WGPURenderPassDescriptor pass_desc = {.colorAttachmentCount = 4, .colorAttachments = attachments};
    WGPURenderBundle bundle = NULL;
    if (through_bundle) {
        const WGPUTextureFormat formats[4] = {WGPUTextureFormat_RGBA8Unorm, WGPUTextureFormat_Undefined,
            WGPUTextureFormat_RGBA32Uint, WGPUTextureFormat_RGBA8Unorm};
        WGPURenderBundleEncoderDescriptor bundle_desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
        bundle_desc.colorFormatCount = 4;
        bundle_desc.colorFormats = formats;
        WGPURenderBundleEncoder recorder = wgpuDeviceCreateRenderBundleEncoder(s->device, &bundle_desc);
        assert(recorder);
        wgpuRenderBundleEncoderSetPipeline(recorder, drawn);
        wgpuRenderBundleEncoderDraw(recorder, 6, 1, 0, 0);
        bundle = wgpuRenderBundleEncoderFinish(recorder, NULL);
        wgpuRenderBundleEncoderRelease(recorder);
        assert(bundle);
    }
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    assert(pass);
    if (bundle) wgpuRenderPassEncoderExecuteBundles(pass, 1, &bundle);
    else {
        wgpuRenderPassEncoderSetPipeline(pass, drawn);
        wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    }
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    if (bundle) wgpuRenderBundleRelease(bundle);
    wgpuRenderPipelineRelease(drawn);
    wgpuShaderModuleRelease(shader);
    for (int i = 0; i < 3; ++i) wgpuTextureViewRelease(views[i]); /* Early caller release. */
    submit(s, encoder);
    const unsigned char first_left[4] = {255, 0, 255, 255}, first_right[4] = {0, 0, 255, 255};
    const uint32_t integer_left[4] = {7, 11, 13, 17}, integer_right[4] = {1, 2, 3, 4};
    const unsigned char masked_left[4] = {255, 255, 0, 255}, masked_right[4] = {255, 0, 0, 0};
    expect_halves(s, textures[0], 4, first_left, first_right, "blended slot 0");
    expect_halves(s, textures[1], 16, integer_left, integer_right, "integer slot 2");
    expect_halves(s, textures[2], 4, masked_left, masked_right, "write-masked slot 3");
    for (int i = 0; i < 3; ++i) wgpuTextureRelease(textures[i]);
}

static void resolved_pass(state *s)
{
    WGPUShaderModule shader = module(s, "struct Out { @location(0) a: vec4f, @location(1) b: vec4f }" LEFT_HALF
        "@fragment fn fragment() -> Out { return Out(vec4f(1.0, 0.0, 0.0, 1.0), vec4f(0.0, 1.0, 0.0, 1.0)); }");
    WGPUColorTargetState targets[2] = {{.format = WGPUTextureFormat_RGBA8Unorm, .writeMask = WGPUColorWriteMask_All},
        {.format = WGPUTextureFormat_RGBA8Unorm, .writeMask = WGPUColorWriteMask_All}};
    WGPURenderPipeline drawn = render_pipeline(s, shader, targets, 2, 4);
    assert(drawn);
    WGPUTexture sampled[2] = {target(s, WGPUTextureFormat_RGBA8Unorm, 4), target(s, WGPUTextureFormat_RGBA8Unorm, 4)};
    WGPUTexture resolved[2] = {target(s, WGPUTextureFormat_RGBA8Unorm, 1), target(s, WGPUTextureFormat_RGBA8Unorm, 1)};
    WGPUTextureView views[4];
    for (int i = 0; i < 2; ++i) {
        views[i] = wgpuTextureCreateView(sampled[i], NULL);
        views[2 + i] = wgpuTextureCreateView(resolved[i], NULL);
    }
    WGPURenderPassColorAttachment attachments[2] = {color(views[0], (WGPUColor){0, 0, 0, 1}),
        color(views[1], (WGPUColor){0, 0, 1, 1})};
    for (int i = 0; i < 2; ++i) {
        attachments[i].resolveTarget = views[2 + i];
        attachments[i].storeOp = WGPUStoreOp_Discard; /* Resolve precedes the discarded store. */
    }
    WGPURenderPassDescriptor pass_desc = {.colorAttachmentCount = 2, .colorAttachments = attachments};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    assert(pass);
    wgpuRenderPassEncoderSetPipeline(pass, drawn);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    submit(s, encoder);
    const unsigned char red[4] = {255, 0, 0, 255}, black[4] = {0, 0, 0, 255};
    const unsigned char green[4] = {0, 255, 0, 255}, blue[4] = {0, 0, 255, 255};
    expect_halves(s, resolved[0], 4, red, black, "resolved slot 0");
    expect_halves(s, resolved[1], 4, green, blue, "resolved slot 1");
    for (int i = 0; i < 4; ++i) wgpuTextureViewRelease(views[i]);
    for (int i = 0; i < 2; ++i) {
        wgpuTextureRelease(sampled[i]);
        wgpuTextureRelease(resolved[i]);
    }
    wgpuRenderPipelineRelease(drawn);
    wgpuShaderModuleRelease(shader);
}

static void rejected_render_pipeline(state *s, WGPUShaderModule shader, const WGPUColorTargetState *targets, size_t count)
{
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    assert(!render_pipeline(s, shader, targets, count, 1));
    expect_validation(s);
}

static void rejected_pass(state *s, const WGPURenderPassColorAttachment *attachments, size_t count)
{
    WGPURenderPassDescriptor pass_desc = {.colorAttachmentCount = count, .colorAttachments = attachments};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    if (pass) {
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }
    finish_invalid(encoder);
    expect_validation(s);
    wgpuCommandEncoderRelease(encoder);
}

static void negatives(state *s)
{
    WGPULimits limits = WGPU_LIMITS_INIT;
    assert(wgpuDeviceGetLimits(s->device, &limits) == WGPUStatus_Success);
    assert(limits.maxColorAttachments >= 4 && limits.maxColorAttachments <= 8 &&
        limits.maxColorAttachmentBytesPerSample == 32);
    WGPUShaderModule shader = module(s, sparse_shader);
    WGPUColorTargetState targets[9];
    WGPUBlendState blend;
    sparse_targets(targets, &blend);
    /* Integer target receiving a float output, and a masked target with no output. */
    targets[0].format = WGPUTextureFormat_RGBA32Uint;
    targets[0].blend = NULL;
    rejected_render_pipeline(s, shader, targets, 4);
    sparse_targets(targets, &blend);
    targets[1] = (WGPUColorTargetState){.format = WGPUTextureFormat_RGBA8Unorm, .writeMask = WGPUColorWriteMask_All};
    rejected_render_pipeline(s, shader, targets, 4);
    sparse_targets(targets, &blend);
    targets[1].blend = &blend; /* An unused slot carries no blend. */
    rejected_render_pipeline(s, shader, targets, 4);
    /* 8 + 16 + 16: over the 32-byte color budget per sample. */
    sparse_targets(targets, &blend);
    targets[3] = (WGPUColorTargetState){.format = WGPUTextureFormat_RGBA32Float, .writeMask = 0};
    rejected_render_pipeline(s, shader, targets, 4);
    /* More slots than any device here admits. */
    for (int i = 4; i < 9; ++i) targets[i] = (WGPUColorTargetState){.format = WGPUTextureFormat_Undefined};
    sparse_targets(targets, &blend);
    rejected_render_pipeline(s, shader, targets, 9);
    WGPUTexture wide[3] = {target(s, WGPUTextureFormat_RGBA32Float, 1), target(s, WGPUTextureFormat_RGBA32Float, 1),
        target(s, WGPUTextureFormat_RGBA32Float, 1)};
    WGPUTextureView views[3];
    for (int i = 0; i < 3; ++i) views[i] = wgpuTextureCreateView(wide[i], NULL);
    WGPURenderPassColorAttachment attachments[9];
    for (int i = 0; i < 9; ++i) attachments[i] = unused_slot();
    for (int i = 0; i < 3; ++i) attachments[i] = color(views[i], (WGPUColor){0, 0, 0, 0});
    rejected_pass(s, attachments, 3); /* 48 color bytes per sample. */
    attachments[1] = color(views[0], (WGPUColor){0, 0, 0, 0});
    attachments[2] = unused_slot();
    rejected_pass(s, attachments, 2); /* One subresource in two slots. */
    attachments[1] = unused_slot();
    rejected_pass(s, attachments, 9); /* Nine slots. */
    rejected_pass(s, attachments + 1, 2); /* Only unused slots and no depth. */
    /* A packed pipeline does not match the sparse pass layout. */
    WGPUColorTargetState packed[3];
    sparse_targets(targets, &blend);
    packed[0] = targets[0];
    packed[1] = targets[2];
    packed[2] = targets[3];
    WGPUShaderModule packed_shader = module(s,
        "struct Out { @location(0) first: vec4f, @location(1) integer: vec4u, @location(2) masked: vec4f }" LEFT_HALF
        "@fragment fn fragment() -> Out { return Out(vec4f(1.0), vec4u(1u), vec4f(1.0)); }");
    WGPURenderPipeline mismatched = render_pipeline(s, packed_shader, packed, 3, 1);
    assert(mismatched);
    WGPUTexture slots[3] = {target(s, WGPUTextureFormat_RGBA8Unorm, 1), target(s, WGPUTextureFormat_RGBA32Uint, 1),
        target(s, WGPUTextureFormat_RGBA8Unorm, 1)};
    WGPUTextureView slot_views[3];
    for (int i = 0; i < 3; ++i) slot_views[i] = wgpuTextureCreateView(slots[i], NULL);
    WGPURenderPassColorAttachment sparse[4] = {color(slot_views[0], (WGPUColor){0}),
        WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT, color(slot_views[1], (WGPUColor){0}), color(slot_views[2], (WGPUColor){0})};
    WGPURenderPassDescriptor pass_desc = {.colorAttachmentCount = 4, .colorAttachments = sparse};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    assert(pass);
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    wgpuRenderPassEncoderSetPipeline(pass, mismatched);
    wgpuRenderPassEncoderEnd(pass); /* The failed encoder reports again; one scope holds the first. */
    wgpuRenderPassEncoderRelease(pass);
    finish_invalid(encoder);
    expect_validation(s);
    wgpuCommandEncoderRelease(encoder);
    for (int i = 0; i < 3; ++i) {
        wgpuTextureViewRelease(views[i]);
        wgpuTextureRelease(wide[i]);
        wgpuTextureViewRelease(slot_views[i]);
        wgpuTextureRelease(slots[i]);
    }
    wgpuRenderPipelineRelease(mismatched);
    wgpuShaderModuleRelease(packed_shader);
    wgpuShaderModuleRelease(shader);
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    sparse_pass(&s, false);
    sparse_pass(&s, true);
    resolved_pass(&s);
    negatives(&s);
    assert(s.validation_errors == 10);
    consumer_finish(&s);
    printf("PASS public multiple color attachments: sparse 4-slot layout at the 32-byte budget, per-slot blend/"
        "write mask/clear/integer values, sparse-format bundle replay, two-slot 4x resolve with discarded "
        "sources, and slot-count/byte-budget/alias/output/format/layout negatives %s\n", adapter_class_scope());
    return 0;
}
