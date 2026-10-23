/* SPDX-License-Identifier: MIT
 * Public storage textures through the real C compiler and Vulkan: every core
 * storage format written by textureStore and read back exactly, read-only and
 * read-write access, 1D/2D-array/3D dimensions, fragment-stage stores,
 * write-then-sample visibility across passes and submissions, and the WebGPU
 * format/access/usage negatives. */
#include "consumer.h"
#include <math.h>
#include <stdlib.h>

enum { SIZE = 4, PITCH = 256 };
typedef enum kind { UNORM8, SNORM8, FLOAT16, FLOAT32, UINT_KIND, SINT_KIND } kind;
typedef struct storage_format {
    WGPUTextureFormat format;
    const char *name;
    kind kind;
    uint32_t channels, channel_bytes;
} storage_format;
static const storage_format formats[] = {
    {WGPUTextureFormat_RGBA8Unorm, "rgba8unorm", UNORM8, 4, 1},
    {WGPUTextureFormat_RGBA8Snorm, "rgba8snorm", SNORM8, 4, 1},
    {WGPUTextureFormat_RGBA8Uint, "rgba8uint", UINT_KIND, 4, 1},
    {WGPUTextureFormat_RGBA8Sint, "rgba8sint", SINT_KIND, 4, 1},
    {WGPUTextureFormat_RGBA16Uint, "rgba16uint", UINT_KIND, 4, 2},
    {WGPUTextureFormat_RGBA16Sint, "rgba16sint", SINT_KIND, 4, 2},
    {WGPUTextureFormat_RGBA16Float, "rgba16float", FLOAT16, 4, 2},
    {WGPUTextureFormat_R32Uint, "r32uint", UINT_KIND, 1, 4},
    {WGPUTextureFormat_R32Sint, "r32sint", SINT_KIND, 1, 4},
    {WGPUTextureFormat_R32Float, "r32float", FLOAT32, 1, 4},
    {WGPUTextureFormat_RG32Uint, "rg32uint", UINT_KIND, 2, 4},
    {WGPUTextureFormat_RG32Sint, "rg32sint", SINT_KIND, 2, 4},
    {WGPUTextureFormat_RG32Float, "rg32float", FLOAT32, 2, 4},
    {WGPUTextureFormat_RGBA32Uint, "rgba32uint", UINT_KIND, 4, 4},
    {WGPUTextureFormat_RGBA32Sint, "rgba32sint", SINT_KIND, 4, 4},
    {WGPUTextureFormat_RGBA32Float, "rgba32float", FLOAT32, 4, 4},
};

static WGPUShaderModule module(state *s, const char *code)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code, WGPU_STRLEN};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain = &source.chain};
    WGPUShaderModule result = wgpuDeviceCreateShaderModule(s->device, &descriptor);
    assert(result);
    return result;
}

static WGPUComputePipeline compute(state *s, const char *code)
{
    WGPUShaderModule shader = module(s, code);
    WGPUComputePipelineDescriptor descriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    descriptor.compute.module = shader;
    WGPUComputePipeline result = wgpuDeviceCreateComputePipeline(s->device, &descriptor);
    wgpuShaderModuleRelease(shader);
    assert(result);
    return result;
}

static WGPUTexture image(state *s, WGPUTextureFormat format, WGPUTextureDimension dimension, WGPUExtent3D size,
    WGPUTextureUsage usage)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = format;
    descriptor.dimension = dimension;
    descriptor.size = size;
    descriptor.usage = usage;
    WGPUTexture result = wgpuDeviceCreateTexture(s->device, &descriptor);
    assert(result);
    return result;
}

/* One dispatch binding entries[0..count) of group 0 from the automatic layout. */
static void run(state *s, WGPUComputePipeline pipeline, const WGPUBindGroupEntry *entries, size_t count,
    uint32_t x, uint32_t y, uint32_t z)
{
    WGPUBindGroup bound = group(s, pipeline, entries, count);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bound, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, x, y, z);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuBindGroupRelease(bound);
    submit(s, encoder);
}

static void readback(state *s, WGPUTexture texture, WGPUExtent3D size, unsigned char *out, uint32_t texel_bytes)
{
    uint64_t bytes = (uint64_t)PITCH * size.height * size.depthOrArrayLayers;
    WGPUBuffer staging = buffer(s, bytes, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPUTexelCopyTextureInfo source = {.texture = texture};
    WGPUTexelCopyBufferInfo destination = {.buffer = staging, .layout = {.bytesPerRow = PITCH, .rowsPerImage = size.height}};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &size);
    submit(s, encoder);
    const unsigned char *mapped = map(s, staging, (size_t)bytes);
    for (uint32_t z = 0; z < size.depthOrArrayLayers; ++z)
        for (uint32_t y = 0; y < size.height; ++y)
            memcpy(out + ((size_t)z * size.height + y) * size.width * texel_bytes,
                mapped + ((size_t)z * size.height + y) * PITCH, (size_t)size.width * texel_bytes);
    wgpuBufferUnmap(staging);
    wgpuBufferRelease(staging);
}

static float half_to_float(uint16_t half)
{
    int exponent = (half >> 10) & 31, mantissa = half & 1023;
    float magnitude = exponent ? ldexpf((float)(mantissa | 1024), exponent - 25) : ldexpf((float)mantissa, -24);
    return half & 0x8000 ? -magnitude : magnitude;
}

/* Expected channel c of texel (x, y) as the shader wrote it, in the read type. */
static double expected(kind kind, uint32_t c, uint32_t x, uint32_t y)
{
    switch (kind) {
    case UNORM8: { const double v[4] = {x, y, x + y, 255}; return v[c]; }
    case SNORM8: { const double v[4] = {x, -(double)y, x + y, -127}; return v[c]; }
    case FLOAT16:
    case FLOAT32: { const double v[4] = {x + 0.5, -(double)y, x * y * 0.25, 7}; return v[c]; }
    case UINT_KIND: { const double v[4] = {x, y + 200, x * y, 255}; return v[c]; }
    case SINT_KIND: { const double v[4] = {x, -(double)y - 100, x * y, -128}; return v[c]; }
    }
    abort();
}

static double stored(const storage_format *f, const unsigned char *texel, uint32_t c)
{
    const unsigned char *at = texel + c * f->channel_bytes;
    switch (f->kind) {
    case UNORM8: return at[0];
    case SNORM8: return (signed char)at[0];
    case FLOAT16: { uint16_t h; memcpy(&h, at, 2); return half_to_float(h); }
    case FLOAT32: { float v; memcpy(&v, at, 4); return v; }
    case UINT_KIND:
        if (f->channel_bytes == 1) return at[0];
        if (f->channel_bytes == 2) { uint16_t v; memcpy(&v, at, 2); return v; }
        { uint32_t v; memcpy(&v, at, 4); return v; }
    case SINT_KIND:
        if (f->channel_bytes == 1) return (signed char)at[0];
        if (f->channel_bytes == 2) { int16_t v; memcpy(&v, at, 2); return v; }
        { int32_t v; memcpy(&v, at, 4); return v; }
    }
    abort();
}

static void every_format(state *s)
{
    static const char *const values[] = {
        [UNORM8] = "vec4f(f32(x) / 255.0, f32(y) / 255.0, f32(x + y) / 255.0, 1.0)",
        [SNORM8] = "vec4f(f32(x) / 127.0, -f32(y) / 127.0, f32(x + y) / 127.0, -1.0)",
        [FLOAT16] = "vec4f(f32(x) + 0.5, -f32(y), f32(x * y) * 0.25, 7.0)",
        [FLOAT32] = "vec4f(f32(x) + 0.5, -f32(y), f32(x * y) * 0.25, 7.0)",
        [UINT_KIND] = "vec4u(x, y + 200u, x * y, 255u)",
        [SINT_KIND] = "vec4i(i32(x), -i32(y) - 100, i32(x * y), -128)"};
    for (size_t i = 0; i < sizeof(formats) / sizeof(*formats); ++i) {
        const storage_format *f = &formats[i];
        char code[512];
        snprintf(code, sizeof(code), "@group(0) @binding(0) var image: texture_storage_2d<%s, write>;"
            "@compute @workgroup_size(4, 4) fn main(@builtin(global_invocation_id) id: vec3u) {"
            "let x = id.x; let y = id.y; textureStore(image, vec2u(x, y), %s); }", f->name, values[f->kind]);
        WGPUComputePipeline pipeline = compute(s, code);
        WGPUExtent3D size = {SIZE, SIZE, 1};
        WGPUTexture texture = image(s, f->format, WGPUTextureDimension_2D, size,
            WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc);
        WGPUTextureView view = wgpuTextureCreateView(texture, NULL);
        run(s, pipeline, &(WGPUBindGroupEntry){.binding = 0, .textureView = view}, 1, 1, 1, 1);
        uint32_t texel = f->channels * f->channel_bytes;
        unsigned char pixels[SIZE * SIZE * 16];
        readback(s, texture, size, pixels, texel);
        for (uint32_t y = 0; y < SIZE; ++y)
            for (uint32_t x = 0; x < SIZE; ++x)
                for (uint32_t c = 0; c < f->channels; ++c)
                    if (stored(f, pixels + (y * SIZE + x) * texel, c) != expected(f->kind, c, x, y)) {
                        fprintf(stderr, "%s texel (%u,%u) channel %u: %g != %g\n", f->name, x, y, c,
                            stored(f, pixels + (y * SIZE + x) * texel, c), expected(f->kind, c, x, y));
                        abort();
                    }
        wgpuTextureViewRelease(view);
        wgpuTextureRelease(texture);
        wgpuComputePipelineRelease(pipeline);
    }
}

/* Read-only loads into a buffer, then read-write increments in place, for each r32 type. */
static void access_modes(state *s)
{
    static const struct { WGPUTextureFormat format; const char *name, *scalar, *vec; } r32[] = {
        {WGPUTextureFormat_R32Uint, "r32uint", "u32", "vec4u"},
        {WGPUTextureFormat_R32Sint, "r32sint", "i32", "vec4i"},
        {WGPUTextureFormat_R32Float, "r32float", "f32", "vec4f"}};
    for (size_t i = 0; i < 3; ++i) {
        WGPUExtent3D size = {SIZE, SIZE, 1};
        WGPUTexture texture = image(s, r32[i].format, WGPUTextureDimension_2D, size,
            WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst);
        uint32_t words[SIZE * SIZE];
        for (uint32_t t = 0; t < SIZE * SIZE; ++t) {
            float as_float = (float)t * 1.5f;
            int32_t as_int = (int32_t)t - 7;
            if (r32[i].format == WGPUTextureFormat_R32Float) memcpy(&words[t], &as_float, 4);
            else if (r32[i].format == WGPUTextureFormat_R32Sint) memcpy(&words[t], &as_int, 4);
            else words[t] = t * 3;
        }
        WGPUTexelCopyTextureInfo destination = {.texture = texture};
        WGPUTexelCopyBufferLayout layout = {.bytesPerRow = SIZE * 4, .rowsPerImage = SIZE};
        wgpuQueueWriteTexture(s->queue, &destination, words, sizeof(words), &layout, &size);
        WGPUTextureView view = wgpuTextureCreateView(texture, NULL);
        char code[640];
        snprintf(code, sizeof(code), "@group(0) @binding(0) var source: texture_storage_2d<%s, read>;"
            "@group(0) @binding(1) var<storage, read_write> copied: array<%s, 16>;"
            "@compute @workgroup_size(4, 4) fn main(@builtin(global_invocation_id) id: vec3u) {"
            "copied[id.y * 4u + id.x] = textureLoad(source, vec2u(id.xy)).x; }", r32[i].name, r32[i].scalar);
        WGPUComputePipeline reader = compute(s, code);
        WGPUBuffer copied = buffer(s, sizeof(words), WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
        WGPUBindGroupEntry entries[2] = {{.binding = 0, .textureView = view},
            {.binding = 1, .buffer = copied, .size = sizeof(words)}};
        run(s, reader, entries, 2, 1, 1, 1);
        WGPUBuffer staging = buffer(s, sizeof(words), WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, copied, 0, staging, 0, sizeof(words));
        submit(s, encoder);
        assert(memcmp(map(s, staging, sizeof(words)), words, sizeof(words)) == 0);
        wgpuBufferUnmap(staging);
        snprintf(code, sizeof(code), "@group(0) @binding(0) var data: texture_storage_2d<%s, read_write>;"
            "@compute @workgroup_size(4, 4) fn main(@builtin(global_invocation_id) id: vec3u) {"
            "let value = textureLoad(data, vec2u(id.xy)).x; textureStore(data, vec2u(id.xy), %s(value + %s(2))); }",
            r32[i].name, r32[i].vec, r32[i].scalar);
        WGPUComputePipeline updater = compute(s, code);
        run(s, updater, entries, 1, 1, 1, 1);
        uint32_t updated[SIZE * SIZE];
        readback(s, texture, size, (unsigned char *)updated, 4);
        for (uint32_t t = 0; t < SIZE * SIZE; ++t) {
            uint32_t want;
            float as_float = (float)t * 1.5f + 2.0f;
            int32_t as_int = (int32_t)t - 5;
            if (r32[i].format == WGPUTextureFormat_R32Float) memcpy(&want, &as_float, 4);
            else if (r32[i].format == WGPUTextureFormat_R32Sint) memcpy(&want, &as_int, 4);
            else want = t * 3 + 2;
            assert(updated[t] == want);
        }
        wgpuBufferRelease(staging);
        wgpuBufferRelease(copied);
        wgpuComputePipelineRelease(updater);
        wgpuComputePipelineRelease(reader);
        wgpuTextureViewRelease(view);
        wgpuTextureRelease(texture);
    }
}

static void dimensions(state *s)
{
    static const struct { const char *type, *coordinate; WGPUTextureDimension dimension;
        WGPUTextureViewDimension view; WGPUExtent3D size; } cases[] = {
        {"texture_storage_1d<r32uint, write>", "textureStore(image, id.x, vec4u(id.x * 7u + 1u))",
            WGPUTextureDimension_1D, WGPUTextureViewDimension_1D, {SIZE, 1, 1}},
        {"texture_storage_2d_array<r32uint, write>",
            "textureStore(image, vec2u(id.xy), id.z, vec4u(id.x + 10u * id.y + 100u * id.z + 1u))",
            WGPUTextureDimension_2D, WGPUTextureViewDimension_2DArray, {SIZE, SIZE, 2}},
        {"texture_storage_3d<r32uint, write>", "textureStore(image, id, vec4u(id.x + 10u * id.y + 100u * id.z + 1u))",
            WGPUTextureDimension_3D, WGPUTextureViewDimension_3D, {SIZE, SIZE, 2}}};
    for (size_t i = 0; i < 3; ++i) {
        char code[512];
        snprintf(code, sizeof(code), "@group(0) @binding(0) var image: %s;"
            "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id: vec3u) { %s; }",
            cases[i].type, cases[i].coordinate);
        WGPUComputePipeline pipeline = compute(s, code);
        WGPUExtent3D size = cases[i].size;
        WGPUTexture texture = image(s, WGPUTextureFormat_R32Uint, cases[i].dimension, size,
            WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc);
        WGPUTextureViewDescriptor view_desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        view_desc.dimension = cases[i].view;
        WGPUTextureView view = wgpuTextureCreateView(texture, &view_desc);
        assert(view);
        run(s, pipeline, &(WGPUBindGroupEntry){.binding = 0, .textureView = view}, 1,
            size.width, size.height, size.depthOrArrayLayers);
        uint32_t texels[SIZE * SIZE * 2];
        readback(s, texture, size, (unsigned char *)texels, 4);
        for (uint32_t z = 0; z < size.depthOrArrayLayers; ++z)
            for (uint32_t y = 0; y < size.height; ++y)
                for (uint32_t x = 0; x < size.width; ++x) {
                    uint32_t want = i == 0 ? x * 7 + 1 : x + 10 * y + 100 * z + 1;
                    assert(texels[(z * size.height + y) * size.width + x] == want);
                }
        wgpuTextureViewRelease(view);
        wgpuTextureRelease(texture);
        wgpuComputePipelineRelease(pipeline);
    }
}

/* Fragment-stage write-only stores at each covered pixel of an 4x4 pass. */
static void fragment_stores(state *s)
{
    WGPUShaderModule shader = module(s,
        "@group(0) @binding(0) var marks: texture_storage_2d<r32uint, write>;"
        "@vertex fn vertex(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {"
        "if (i == 0u) { return vec4f(-1.0, -1.0, 0.0, 1.0); }"
        "if (i == 1u) { return vec4f(3.0, -1.0, 0.0, 1.0); }"
        "return vec4f(-1.0, 3.0, 0.0, 1.0); }"
        "@fragment fn fragment(@builtin(position) p: vec4f) -> @location(0) vec4f {"
        "let at = vec2u(p.xy); textureStore(marks, at, vec4u(at.x * 16u + at.y + 5u)); return vec4f(1.0); }");
    WGPUColorTargetState color = {.format = WGPUTextureFormat_RGBA8Unorm, .writeMask = WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module = shader, .targetCount = 1, .targets = &color};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex.module = shader;
    descriptor.fragment = &fragment;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(s->device, &descriptor);
    assert(pipeline);
    WGPUExtent3D size = {SIZE, SIZE, 1};
    WGPUTexture marks = image(s, WGPUTextureFormat_R32Uint, WGPUTextureDimension_2D, size,
        WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc);
    WGPUTexture target = image(s, WGPUTextureFormat_RGBA8Unorm, WGPUTextureDimension_2D, size,
        WGPUTextureUsage_RenderAttachment);
    WGPUTextureView marks_view = wgpuTextureCreateView(marks, NULL), target_view = wgpuTextureCreateView(target, NULL);
    WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroupEntry entry = {.binding = 0, .textureView = marks_view};
    WGPUBindGroupDescriptor group_desc = {.layout = layout, .entryCount = 1, .entries = &entry};
    WGPUBindGroup bound = wgpuDeviceCreateBindGroup(s->device, &group_desc);
    assert(layout && bound);
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = target_view;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor pass_desc = {.colorAttachmentCount = 1, .colorAttachments = &attachment};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bound, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    submit(s, encoder);
    uint32_t texels[SIZE * SIZE];
    readback(s, marks, size, (unsigned char *)texels, 4);
    for (uint32_t y = 0; y < SIZE; ++y)
        for (uint32_t x = 0; x < SIZE; ++x) assert(texels[y * SIZE + x] == x * 16 + y + 5);
    wgpuBindGroupRelease(bound);
    wgpuBindGroupLayoutRelease(layout);
    wgpuTextureViewRelease(marks_view);
    wgpuTextureViewRelease(target_view);
    wgpuTextureRelease(marks);
    wgpuTextureRelease(target);
    wgpuRenderPipelineRelease(pipeline);
    wgpuShaderModuleRelease(shader);
}

/* A storage write in one pass is visible to a sampled load in the next pass of
 * the same encoder, repeated in two submissions with different values. */
static void pass_visibility(state *s)
{
    WGPUExtent3D size = {1, 1, 1};
    WGPUTexture texture = image(s, WGPUTextureFormat_R32Float, WGPUTextureDimension_2D, size,
        WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding);
    WGPUTextureViewDescriptor view_desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    view_desc.usage = WGPUTextureUsage_StorageBinding;
    WGPUTextureView storage_view = wgpuTextureCreateView(texture, &view_desc);
    view_desc.usage = WGPUTextureUsage_TextureBinding;
    WGPUTextureView sampled_view = wgpuTextureCreateView(texture, &view_desc);
    WGPUComputePipeline reader = compute(s, "@group(0) @binding(0) var source: texture_2d<f32>;"
        "@group(0) @binding(1) var<storage, read_write> output: array<f32>;"
        "@compute @workgroup_size(1) fn main() { output[0] = textureLoad(source, vec2i(0, 0), 0).x; }");
    WGPUBuffer output = buffer(s, 4, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUBindGroupEntry reader_entries[2] = {{.binding = 0, .textureView = sampled_view},
        {.binding = 1, .buffer = output, .size = 4}};
    WGPUBindGroup reader_group = group(s, reader, reader_entries, 2);
    const float values[2] = {11, 29};
    for (int round = 0; round < 2; ++round) {
        char code[256];
        snprintf(code, sizeof(code), "@group(0) @binding(0) var destination: texture_storage_2d<r32float, write>;"
            "@compute @workgroup_size(1) fn main() { textureStore(destination, vec2i(0, 0), vec4f(%g)); }", values[round]);
        WGPUComputePipeline writer = compute(s, code);
        WGPUBindGroup writer_group = group(s, writer, &(WGPUBindGroupEntry){.binding = 0, .textureView = storage_view}, 1);
        WGPUBuffer staging = buffer(s, 4, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
        WGPUComputePipeline pipelines[2] = {writer, reader};
        WGPUBindGroup groups[2] = {writer_group, reader_group};
        for (int i = 0; i < 2; ++i) {
            WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
            wgpuComputePassEncoderSetPipeline(pass, pipelines[i]);
            wgpuComputePassEncoderSetBindGroup(pass, 0, groups[i], 0, NULL);
            wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
            wgpuComputePassEncoderEnd(pass);
            wgpuComputePassEncoderRelease(pass);
        }
        wgpuCommandEncoderCopyBufferToBuffer(encoder, output, 0, staging, 0, 4);
        submit(s, encoder);
        float actual;
        memcpy(&actual, map(s, staging, 4), 4);
        assert(actual == values[round]);
        wgpuBufferUnmap(staging);
        wgpuBufferRelease(staging);
        wgpuBindGroupRelease(writer_group);
        wgpuComputePipelineRelease(writer);
    }
    wgpuBindGroupRelease(reader_group);
    wgpuBufferRelease(output);
    wgpuComputePipelineRelease(reader);
    wgpuTextureViewRelease(storage_view);
    wgpuTextureViewRelease(sampled_view);
    wgpuTextureRelease(texture);
}

/* Returns the number of validation errors it expects to have captured. */
static int storage_visibility_and_negatives(state *s)
{
    fragment_stores(s);
    pass_visibility(s);
    int expected_errors = 0;
    /* StorageBinding on a non-storage format. */
    WGPUTextureDescriptor texture_desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texture_desc.format = WGPUTextureFormat_RGBA8UnormSrgb;
    texture_desc.size = (WGPUExtent3D){SIZE, SIZE, 1};
    texture_desc.usage = WGPUTextureUsage_StorageBinding;
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateTexture(s->device, &texture_desc));
    expect_validation(s);
    ++expected_errors;
    /* Read-write access on a format outside the read-write subset: explicit layout and shader. */
    WGPUBindGroupLayoutEntry entry = {.binding = 0, .visibility = WGPUShaderStage_Compute,
        .storageTexture = {.access = WGPUStorageTextureAccess_ReadWrite, .format = WGPUTextureFormat_RGBA8Unorm,
            .viewDimension = WGPUTextureViewDimension_2D}};
    WGPUBindGroupLayoutDescriptor layout_desc = {.entryCount = 1, .entries = &entry};
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateBindGroupLayout(s->device, &layout_desc));
    expect_validation(s);
    ++expected_errors;
    WGPUShaderModule shader = module(s, "@group(0) @binding(0) var data: texture_storage_2d<rgba8unorm, read_write>;"
        "@compute @workgroup_size(1) fn main() { textureStore(data, vec2u(0u), textureLoad(data, vec2u(0u))); }");
    WGPUComputePipelineDescriptor pipeline_desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipeline_desc.compute.module = shader;
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateComputePipeline(s->device, &pipeline_desc));
    expect_validation(s);
    ++expected_errors;
    wgpuShaderModuleRelease(shader);
    /* A storage-only texture cannot be sampled (filterable or not), and a texture
     * without StorageBinding cannot back a storage binding. */
    texture_desc.format = WGPUTextureFormat_R32Float;
    WGPUTexture storage_only = wgpuDeviceCreateTexture(s->device, &texture_desc);
    texture_desc.usage = WGPUTextureUsage_TextureBinding;
    WGPUTexture sampled_only = wgpuDeviceCreateTexture(s->device, &texture_desc);
    assert(storage_only && sampled_only);
    WGPUTextureView storage_view = wgpuTextureCreateView(storage_only, NULL);
    WGPUTextureView sampled_view = wgpuTextureCreateView(sampled_only, NULL);
    WGPUBindGroupLayoutEntry sampled_entry = {.binding = 0, .visibility = WGPUShaderStage_Compute,
        .texture = {.sampleType = WGPUTextureSampleType_UnfilterableFloat, .viewDimension = WGPUTextureViewDimension_2D}};
    WGPUBindGroupLayoutEntry storage_entry = {.binding = 0, .visibility = WGPUShaderStage_Compute,
        .storageTexture = {.access = WGPUStorageTextureAccess_WriteOnly, .format = WGPUTextureFormat_R32Float,
            .viewDimension = WGPUTextureViewDimension_2D}};
    WGPUBindGroupLayout sampled_layout = wgpuDeviceCreateBindGroupLayout(s->device,
        &(WGPUBindGroupLayoutDescriptor){.entryCount = 1, .entries = &sampled_entry});
    WGPUBindGroupLayout storage_layout = wgpuDeviceCreateBindGroupLayout(s->device,
        &(WGPUBindGroupLayoutDescriptor){.entryCount = 1, .entries = &storage_entry});
    assert(sampled_layout && storage_layout);
    WGPUBindGroupLayout layouts[2] = {sampled_layout, storage_layout};
    WGPUTextureView views[2] = {storage_view, sampled_view};
    for (int i = 0; i < 2; ++i) {
        WGPUBindGroupEntry bound = {.binding = 0, .textureView = views[i]};
        WGPUBindGroupDescriptor group_desc = {.layout = layouts[i], .entryCount = 1, .entries = &bound};
        wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
        assert(!wgpuDeviceCreateBindGroup(s->device, &group_desc));
        expect_validation(s);
        ++expected_errors;
    }
    wgpuBindGroupLayoutRelease(sampled_layout);
    wgpuBindGroupLayoutRelease(storage_layout);
    wgpuTextureViewRelease(storage_view);
    wgpuTextureViewRelease(sampled_view);
    wgpuTextureRelease(storage_only);
    wgpuTextureRelease(sampled_only);
    return expected_errors;
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    every_format(&s);
    access_modes(&s);
    dimensions(&s);
    int validation = storage_visibility_and_negatives(&s);
    assert(s.validation_errors == (unsigned)validation);
    consumer_finish(&s);
    printf("PASS public storage textures: all 16 core storage formats written and read back exactly, read-only and "
        "read-write r32 access, 1D/2D-array/3D, fragment-stage stores, write-then-sample across passes and "
        "submissions, and format/access/usage negatives %s\n", adapter_class_scope());
    return 0;
}

