/* SPDX-License-Identifier: MIT
 * Public-ABI repeated draw state: 64 draws in one render pass each fill one
 * 8x8 cell of a 64x64 RGBA8 target from a read-only storage color table.
 * One pass keeps pipeline and bind group unchanged (set once, and redundantly
 * re-set before some draws); the next alternates two pipelines and two bind
 * groups per draw. A compute dispatch in the same encoder writes the first
 * color table just before the passes read it, and every pixel is compared with
 * the expected cell color. A physical run is developer evidence on the
 * reported adapter only. */
#include "consumer.h"
#include <stdint.h>
#include <stdlib.h>

enum { SIZE = 64, CELL = 8, CELLS = (SIZE / CELL) * (SIZE / CELL), ROW = SIZE * 4 };

static const char *DRAW =
    "@group(0) @binding(0) var<storage, read> colors: array<vec4f>;"
    "struct V { @builtin(position) position: vec4f, @location(0) @interpolate(flat) cell: u32 }"
    "@vertex fn vertex(@builtin(vertex_index) index: u32) -> V {"
    "  let cell = index / 6u; let corner = index % 6u;"
    "  var xs = array<f32, 6>(0.0, 1.0, 0.0, 0.0, 1.0, 1.0);"
    "  var ys = array<f32, 6>(0.0, 0.0, 1.0, 1.0, 0.0, 1.0);"
    "  let px = (f32(cell % 8u) + xs[corner]) * 8.0; let py = (f32(cell / 8u) + ys[corner]) * 8.0;"
    "  var out: V; out.position = vec4f(px / 32.0 - 1.0, 1.0 - py / 32.0, 0.0, 1.0); out.cell = cell; return out; }"
    "@fragment fn plain(v: V) -> @location(0) vec4f { return colors[v.cell]; }"
    "@fragment fn swapped(v: V) -> @location(0) vec4f { return colors[v.cell].bgra; }";
static const char *FILL =
    "@group(0) @binding(0) var<storage, read_write> colors: array<vec4f>;"
    "@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3u) {"
    "  let c = id.x; colors[c] = vec4f(f32(c * 3u % 256u), f32(255u - c * 2u), f32(c * 5u % 256u), 255.0) / 255.0; }";

static WGPURenderPipeline render_pipeline(state *s, WGPUShaderModule shader, const char *entry)
{
    WGPUColorTargetState color = {.format=WGPUTextureFormat_RGBA8Unorm, .writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=shader, .entryPoint={entry,WGPU_STRLEN}, .targetCount=1, .targets=&color};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex.module = shader;
    descriptor.vertex.entryPoint = (WGPUStringView){"vertex",WGPU_STRLEN};
    descriptor.fragment = &fragment;
    WGPURenderPipeline result = wgpuDeviceCreateRenderPipeline(s->device, &descriptor);
    assert(result);
    return result;
}

static WGPUBindGroup render_group(state *s, WGPURenderPipeline pipeline, WGPUBuffer colors)
{
    WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroupEntry entry = {.binding=0,.buffer=colors,.size=CELLS * 16};
    WGPUBindGroupDescriptor descriptor = {.layout=layout,.entryCount=1,.entries=&entry};
    WGPUBindGroup result = wgpuDeviceCreateBindGroup(s->device, &descriptor);
    wgpuBindGroupLayoutRelease(layout);
    assert(result);
    return result;
}

static WGPURenderPassEncoder begin(WGPUCommandEncoder encoder, WGPUTextureView view)
{
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    attachment.clearValue = (WGPUColor){0,0,0,0};
    WGPURenderPassDescriptor descriptor = {.colorAttachmentCount=1, .colorAttachments=&attachment};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &descriptor);
    assert(pass);
    return pass;
}

static void table(uint32_t cell, unsigned char out[4])
{
    out[0] = (unsigned char)(cell * 3u % 256u);
    out[1] = (unsigned char)(255u - cell * 2u);
    out[2] = (unsigned char)(cell * 5u % 256u);
    out[3] = 255;
}

static void check(const unsigned char *pixels, const unsigned char expected[CELLS][4], const char *label)
{
    for (uint32_t y = 0; y < SIZE; ++y) for (uint32_t x = 0; x < SIZE; ++x) {
        const uint32_t cell = (y / CELL) * (SIZE / CELL) + x / CELL;
        const unsigned char *pixel = pixels + y * ROW + x * 4;
        if (!memcmp(pixel, expected[cell], 4)) continue;
        fprintf(stderr, "%s pixel %u,%u cell %u: got %u %u %u %u expected %u %u %u %u\n", label, x, y, cell,
            pixel[0], pixel[1], pixel[2], pixel[3], expected[cell][0], expected[cell][1], expected[cell][2], expected[cell][3]);
        abort();
    }
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    const WGPUBufferUsage storage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    WGPUBuffer first = buffer(&s, CELLS * 16, storage), second = buffer(&s, CELLS * 16, storage);
    float values[CELLS][4];
    unsigned char first_bytes[CELLS][4], second_bytes[CELLS][4];
    for (uint32_t c = 0; c < CELLS; ++c) {
        table(c, first_bytes[c]);
        for (uint32_t k = 0; k < 4; ++k) second_bytes[c][k] = (unsigned char)(k == 3 ? 255 : (c * 7u + k * 61u) % 256u);
        for (uint32_t k = 0; k < 4; ++k) values[c][k] = (float)second_bytes[c][k] / 255.0f;
    }
    wgpuQueueWriteBuffer(s.queue, second, 0, values, sizeof(values));
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){DRAW,WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader_descriptor = {.nextInChain=&source.chain};
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(s.device, &shader_descriptor);
    assert(shader);
    WGPURenderPipeline plain = render_pipeline(&s, shader, "plain"), swapped = render_pipeline(&s, shader, "swapped");
    wgpuShaderModuleRelease(shader);
    WGPUBindGroup groups[2][2] = {
        {render_group(&s, plain, first), render_group(&s, plain, second)},
        {render_group(&s, swapped, first), render_group(&s, swapped, second)}};
    WGPUComputePipeline fill = pipeline(&s, FILL);
    WGPUBindGroupEntry fill_entry = {.binding=0,.buffer=first,.size=CELLS * 16};
    WGPUBindGroup fill_group = group(&s, fill, &fill_entry, 1);
    WGPUTextureDescriptor image_descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    image_descriptor.format = WGPUTextureFormat_RGBA8Unorm;
    image_descriptor.size = (WGPUExtent3D){SIZE,SIZE,1};
    image_descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    WGPUTexture images[2] = {wgpuDeviceCreateTexture(s.device, &image_descriptor), wgpuDeviceCreateTexture(s.device, &image_descriptor)};
    WGPUTextureView views[2] = {wgpuTextureCreateView(images[0], NULL), wgpuTextureCreateView(images[1], NULL)};
    assert(images[0] && images[1] && views[0] && views[1]);
    WGPUBuffer readback = buffer(&s, 2 * ROW * SIZE, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    WGPUComputePassEncoder compute = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(compute, fill);
    wgpuComputePassEncoderSetBindGroup(compute, 0, fill_group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(compute, 1, 1, 1);
    wgpuComputePassEncoderEnd(compute);
    wgpuComputePassEncoderRelease(compute);
    /* Unchanged state: one pipeline and bind group; every fourth draw re-sets both. */
    WGPURenderPassEncoder pass = begin(encoder, views[0]);
    wgpuRenderPassEncoderSetPipeline(pass, plain);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, groups[0][0], 0, NULL);
    for (uint32_t c = 0; c < CELLS; ++c) {
        if (c % 4 == 3) {
            wgpuRenderPassEncoderSetPipeline(pass, plain);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, groups[0][0], 0, NULL);
        }
        wgpuRenderPassEncoderDraw(pass, 6, 1, 6 * c, 0);
    }
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    /* Changing state: pipeline alternates every draw, bind group every two. */
    unsigned char changing[CELLS][4];
    pass = begin(encoder, views[1]);
    for (uint32_t c = 0; c < CELLS; ++c) {
        const uint32_t p = c & 1u, g = (c >> 1) & 1u;
        wgpuRenderPassEncoderSetPipeline(pass, p ? swapped : plain);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, groups[p][g], 0, NULL);
        wgpuRenderPassEncoderDraw(pass, 6, 1, 6 * c, 0);
        const unsigned char *color = g ? second_bytes[c] : first_bytes[c];
        if (p) { changing[c][0] = color[2]; changing[c][1] = color[1]; changing[c][2] = color[0]; changing[c][3] = color[3]; }
        else memcpy(changing[c], color, 4);
    }
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    for (uint32_t i = 0; i < 2; ++i) {
        WGPUTexelCopyTextureInfo from = {.texture=images[i]};
        WGPUTexelCopyBufferInfo to = {.buffer=readback,.layout={.offset=(uint64_t)i * ROW * SIZE,.bytesPerRow=ROW,.rowsPerImage=SIZE}};
        WGPUExtent3D extent = {SIZE,SIZE,1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &from, &to, &extent);
    }
    submit(&s, encoder);
    const unsigned char *pixels = map(&s, readback, 2 * ROW * SIZE);
    check(pixels, (const unsigned char (*)[4])first_bytes, "unchanged");
    check(pixels + ROW * SIZE, (const unsigned char (*)[4])changing, "changing");
    wgpuBufferUnmap(readback);
    assert(!s.errors);
    for (uint32_t i = 0; i < 2; ++i) {
        wgpuTextureViewRelease(views[i]);
        wgpuTextureRelease(images[i]);
        for (uint32_t j = 0; j < 2; ++j) wgpuBindGroupRelease(groups[i][j]);
    }
    wgpuBindGroupRelease(fill_group);
    wgpuComputePipelineRelease(fill);
    wgpuRenderPipelineRelease(plain);
    wgpuRenderPipelineRelease(swapped);
    wgpuBufferRelease(first); wgpuBufferRelease(second); wgpuBufferRelease(readback);
    consumer_finish(&s);
    printf("PASS public repeated draw state: 64 draws with unchanged and redundantly re-set pipeline/bind group, "
        "64 draws alternating two pipelines and two bind groups, compute-written color table read by both "
        "passes, every pixel exact %s\n", adapter_class_scope());
    return 0;
}
