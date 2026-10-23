/* SPDX-License-Identifier: MIT
 * Every pinned WebGPU vertex format through a real draw: each attribute is
 * fetched per instance, passed flat to a 1x1 32-bit target and read back.
 * Values are exact under any conforming normalization (0, max, -max). The most
 * negative snorm code (-128, -32768) is avoided: Vulkan requires it to clamp to
 * -1.0, but lavapipe returned -128/127 on 2026-10-23, a driver deviation. */
#include "consumer.h"
#include <stdint.h>

enum { FLOAT_KIND, UINT_KIND, SINT_KIND };
typedef struct vertex_case {
    WGPUVertexFormat format;
    int kind;
    uint8_t data[16];
    union { float f[4]; uint32_t u[4]; int32_t i[4]; } expected;
} vertex_case;

#define F(...) {.f = {__VA_ARGS__}}
#define U(...) {.u = {__VA_ARGS__}}
#define I(...) {.i = {__VA_ARGS__}}
static const vertex_case cases[] = {
    {WGPUVertexFormat_Uint8, UINT_KIND, {7}, U(7, 0, 0, 1)},
    {WGPUVertexFormat_Uint8x2, UINT_KIND, {3, 250}, U(3, 250, 0, 1)},
    {WGPUVertexFormat_Uint8x4, UINT_KIND, {1, 2, 250, 255}, U(1, 2, 250, 255)},
    {WGPUVertexFormat_Sint8, SINT_KIND, {0xFF}, I(-1, 0, 0, 1)},
    {WGPUVertexFormat_Sint8x2, SINT_KIND, {0x80, 0x7F}, I(-128, 127, 0, 1)},
    {WGPUVertexFormat_Sint8x4, SINT_KIND, {0xFF, 0x80, 0x7F, 5}, I(-1, -128, 127, 5)},
    {WGPUVertexFormat_Unorm8, FLOAT_KIND, {255}, F(1, 0, 0, 1)},
    {WGPUVertexFormat_Unorm8x2, FLOAT_KIND, {0, 255}, F(0, 1, 0, 1)},
    {WGPUVertexFormat_Unorm8x4, FLOAT_KIND, {255, 0, 255, 0}, F(1, 0, 1, 0)},
    {WGPUVertexFormat_Snorm8, FLOAT_KIND, {0x7F}, F(1, 0, 0, 1)},
    {WGPUVertexFormat_Snorm8x2, FLOAT_KIND, {0x81, 0x7F}, F(-1, 1, 0, 1)},
    {WGPUVertexFormat_Snorm8x4, FLOAT_KIND, {0x7F, 0, 0x81, 0x7F}, F(1, 0, -1, 1)},
    {WGPUVertexFormat_Uint16, UINT_KIND, {0xFF, 0xFF}, U(65535, 0, 0, 1)},
    {WGPUVertexFormat_Uint16x2, UINT_KIND, {0x2C, 0x01, 0, 0}, U(300, 0, 0, 1)},
    {WGPUVertexFormat_Uint16x4, UINT_KIND, {1, 0, 0xFF, 0xFF, 0x2C, 0x01, 7, 0}, U(1, 65535, 300, 7)},
    {WGPUVertexFormat_Sint16, SINT_KIND, {0x00, 0x80}, I(-32768, 0, 0, 1)},
    {WGPUVertexFormat_Sint16x2, SINT_KIND, {0xFF, 0x7F, 0xFF, 0xFF}, I(32767, -1, 0, 1)},
    {WGPUVertexFormat_Sint16x4, SINT_KIND, {0xFE, 0xFF, 3, 0, 0x00, 0x80, 0xFF, 0x7F}, I(-2, 3, -32768, 32767)},
    {WGPUVertexFormat_Unorm16, FLOAT_KIND, {0xFF, 0xFF}, F(1, 0, 0, 1)},
    {WGPUVertexFormat_Unorm16x2, FLOAT_KIND, {0, 0, 0xFF, 0xFF}, F(0, 1, 0, 1)},
    {WGPUVertexFormat_Unorm16x4, FLOAT_KIND, {0xFF, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF}, F(1, 0, 0, 1)},
    {WGPUVertexFormat_Snorm16, FLOAT_KIND, {0xFF, 0x7F}, F(1, 0, 0, 1)},
    {WGPUVertexFormat_Snorm16x2, FLOAT_KIND, {0x01, 0x80, 0xFF, 0x7F}, F(-1, 1, 0, 1)},
    {WGPUVertexFormat_Snorm16x4, FLOAT_KIND, {0xFF, 0x7F, 0, 0, 0x01, 0x80, 0, 0}, F(1, 0, -1, 0)},
    {WGPUVertexFormat_Float16, FLOAT_KIND, {0x00, 0x3C}, F(1, 0, 0, 1)},
    {WGPUVertexFormat_Float16x2, FLOAT_KIND, {0x00, 0xC0, 0x00, 0x3E}, F(-2, 1.5f, 0, 1)},
    {WGPUVertexFormat_Float16x4, FLOAT_KIND, {0x00, 0x34, 0x00, 0x3C, 0x00, 0xC0, 0x00, 0x3E}, F(0.25f, 1, -2, 1.5f)},
    {WGPUVertexFormat_Float32, FLOAT_KIND, {0, 0, 0x40, 0x40}, F(3, 0, 0, 1)},
    {WGPUVertexFormat_Float32x3, FLOAT_KIND, {0, 0, 0x80, 0x3F, 0, 0, 0, 0xC0, 0, 0, 0x40, 0x40}, F(1, -2, 3, 1)},
    {WGPUVertexFormat_Uint32x2, UINT_KIND, {0xFF, 0xFF, 0xFF, 0xFF, 9, 0, 0, 0}, U(UINT32_MAX, 9, 0, 1)},
    {WGPUVertexFormat_Sint32x4, SINT_KIND, {0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0x80, 2, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0x7F},
        I(-1, INT32_MIN, 2, INT32_MAX)},
    /* r=1023 g=0 b=1023 a=3 packed little-endian; red is in the low bits. */
    {WGPUVertexFormat_Unorm10_10_10_2, FLOAT_KIND, {0xFF, 0x03, 0xF0, 0xFF}, F(1, 0, 1, 1)},
    /* Memory order B,G,R,A; the shader receives r,g,b,a. */
    {WGPUVertexFormat_Unorm8x4BGRA, FLOAT_KIND, {255, 0, 0, 255}, F(0, 0, 1, 1)},
};

static const char *const shaders[3] = {
    "struct O{@builtin(position) p:vec4f,@location(0) @interpolate(flat) v:vec4f}"
    "@vertex fn vs(@builtin(vertex_index) i:u32,@location(0) a:vec4f)->O{"
    "return O(vec4f(f32(i&1u)*4.0-1.0,f32(i>>1u)*4.0-1.0,0.0,1.0),a);}"
    "@fragment fn fs(input:O)->@location(0) vec4f{return input.v;}",
    "struct O{@builtin(position) p:vec4f,@location(0) @interpolate(flat) v:vec4u}"
    "@vertex fn vs(@builtin(vertex_index) i:u32,@location(0) a:vec4u)->O{"
    "return O(vec4f(f32(i&1u)*4.0-1.0,f32(i>>1u)*4.0-1.0,0.0,1.0),a);}"
    "@fragment fn fs(input:O)->@location(0) vec4u{return input.v;}",
    "struct O{@builtin(position) p:vec4f,@location(0) @interpolate(flat) v:vec4i}"
    "@vertex fn vs(@builtin(vertex_index) i:u32,@location(0) a:vec4i)->O{"
    "return O(vec4f(f32(i&1u)*4.0-1.0,f32(i>>1u)*4.0-1.0,0.0,1.0),a);}"
    "@fragment fn fs(input:O)->@location(0) vec4i{return input.v;}",
};
static const WGPUTextureFormat targets[3] = {
    WGPUTextureFormat_RGBA32Float, WGPUTextureFormat_RGBA32Uint, WGPUTextureFormat_RGBA32Sint};

static WGPUShaderModule module_for(state *s, int kind)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){shaders[kind], WGPU_STRLEN};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain = &source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device, &descriptor);
    assert(module);
    return module;
}

static WGPURenderPipeline make_pipeline(state *s, WGPUShaderModule module, int kind,
    WGPUVertexFormat format, uint64_t offset)
{
    WGPUVertexAttribute attribute = {.format = format, .offset = offset, .shaderLocation = 0};
    WGPUVertexBufferLayout layout = {.stepMode = WGPUVertexStepMode_Instance, .arrayStride = 32,
        .attributeCount = 1, .attributes = &attribute};
    WGPUColorTargetState color = {.format = targets[kind], .writeMask = WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module = module, .targetCount = 1, .targets = &color};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex = (WGPUVertexState){.module = module, .bufferCount = 1, .buffers = &layout};
    descriptor.fragment = &fragment;
    return wgpuDeviceCreateRenderPipeline(s->device, &descriptor);
}

static void run_case(state *s, const vertex_case *c, WGPUShaderModule module)
{
    WGPURenderPipeline pipeline = make_pipeline(s, module, c->kind, c->format, 8);
    assert(pipeline);
    uint8_t bytes[32] = {0};
    memcpy(bytes + 8, c->data, sizeof(c->data)); /* Exercise a nonzero aligned offset. */
    WGPUBuffer vertices = buffer(s, sizeof(bytes), WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue, vertices, 0, bytes, sizeof(bytes));
    WGPUTextureDescriptor image_desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    image_desc.format = targets[c->kind];
    image_desc.size = (WGPUExtent3D){1, 1, 1};
    image_desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    WGPUTexture image = wgpuDeviceCreateTexture(s->device, &image_desc);
    WGPUTextureView view = wgpuTextureCreateView(image, NULL);
    assert(image && view);
    WGPUBuffer readback = buffer(s, 256, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor pass_desc = {.colorAttachmentCount = 1, .colorAttachments = &attachment};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertices, 0, sizeof(bytes));
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    texture_to_buffer(encoder, image, readback, 0, 1, 1);
    submit(s, encoder);
    const uint32_t *pixel = map(s, readback, 256);
    if (memcmp(pixel, c->expected.u, 16) != 0) {
        fprintf(stderr, "vertex format 0x%x: got %08x %08x %08x %08x expected %08x %08x %08x %08x\n",
            (unsigned)c->format, pixel[0], pixel[1], pixel[2], pixel[3],
            c->expected.u[0], c->expected.u[1], c->expected.u[2], c->expected.u[3]);
        assert(!"exact vertex fetch");
    }
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(image);
    wgpuBufferRelease(vertices);
    wgpuRenderPipelineRelease(pipeline);
}

static void expect_rejected(state *s, WGPUShaderModule module, int kind, WGPUVertexFormat format, uint64_t offset)
{
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    WGPURenderPipeline pipeline = make_pipeline(s, module, kind, format, offset);
    expect_validation(s);
    assert(!pipeline);
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    WGPUShaderModule modules[3] = {module_for(&s, FLOAT_KIND), module_for(&s, UINT_KIND), module_for(&s, SINT_KIND)};
    const size_t count = sizeof(cases) / sizeof(cases[0]);
    for (size_t i = 0; i < count; ++i) run_case(&s, &cases[i], modules[cases[i].kind]);
    /* Offsets align to min(4, format size); WGSL scalar kinds must match. */
    WGPURenderPipeline odd = make_pipeline(&s, modules[FLOAT_KIND], FLOAT_KIND, WGPUVertexFormat_Unorm8, 3);
    assert(odd);
    wgpuRenderPipelineRelease(odd);
    expect_rejected(&s, modules[UINT_KIND], UINT_KIND, WGPUVertexFormat_Uint16, 1);
    expect_rejected(&s, modules[FLOAT_KIND], FLOAT_KIND, WGPUVertexFormat_Float16x4, 2);
    expect_rejected(&s, modules[FLOAT_KIND], FLOAT_KIND, WGPUVertexFormat_Uint8x4, 0);
    expect_rejected(&s, modules[UINT_KIND], UINT_KIND, WGPUVertexFormat_Unorm16x2, 0);
    expect_rejected(&s, modules[FLOAT_KIND], FLOAT_KIND, (WGPUVertexFormat)0x2A, 0);
    for (int kind = 0; kind < 3; ++kind) wgpuShaderModuleRelease(modules[kind]);
    consumer_finish(&s);
    printf("PASS public vertex formats: %zu formats incl. every 8/16-bit, float16, 10-10-10-2 and BGRA fetched exactly at a nonzero offset with default component fill; min(4,size) offset alignment and WGSL scalar-kind controls %s\n",
        count, adapter_class_scope());
    return 0;
}
