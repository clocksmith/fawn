/* SPDX-License-Identifier: MIT
 * Public point lists over the C Vulkan path: one-pixel points at exact pixel
 * centers drawn directly, from a render bundle and through an index buffer
 * (with an unselected decoy vertex), then the exact covered pixels. Requires
 * the device's maintenance5 feature (default PointSize 1.0), which the Vulkan
 * owner enables only when offered; shaders never write PointSize. */
#include "consumer.h"

enum { WIDTH = 8, HEIGHT = 4, VERTICES = 10 };
typedef struct point { float x, y, color[4]; } point;
/* Pixel (px, py) center in WebGPU clip space, y up, top row 0. */
static point at(int px, int py, float r, float g, float b)
{
    return (point){-1.0f + (px + 0.5f) * 2.0f / WIDTH, 1.0f - (py + 0.5f) * 2.0f / HEIGHT, {r, g, b, 1.0f}};
}
static WGPURenderPipeline points(state *s, WGPUPrimitiveTopology topology, WGPUIndexFormat strip)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){
        "struct O{@builtin(position) p:vec4f,@location(0) @interpolate(flat) c:vec4f}"
        "@vertex fn vertex(@location(0) p:vec2f,@location(1) c:vec4f)->O{return O(vec4f(p,0.0,1.0),c);}"
        "@fragment fn fragment(i:O)->@location(0) vec4f{return i.c;}",WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader = {.nextInChain=&source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device,&shader);
    assert(module);
    WGPUVertexAttribute attributes[2] = {{.format=WGPUVertexFormat_Float32x2,.shaderLocation=0},
        {.format=WGPUVertexFormat_Float32x4,.offset=8,.shaderLocation=1}};
    WGPUVertexBufferLayout layout = {.arrayStride=sizeof(point),.attributeCount=2,.attributes=attributes};
    WGPUColorTargetState color = {.format=WGPUTextureFormat_RGBA8Unorm,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=module,.targetCount=1,.targets=&color};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex = (WGPUVertexState){.module=module,.bufferCount=1,.buffers=&layout};
    descriptor.fragment = &fragment;
    descriptor.primitive.topology = topology;
    descriptor.primitive.stripIndexFormat = strip;
    WGPURenderPipeline result = wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
    wgpuShaderModuleRelease(module);
    return result;
}
int main(void)
{
    state s = {0};
    consumer_init(&s);
    WGPURenderPipeline pipeline = points(&s,WGPUPrimitiveTopology_PointList,WGPUIndexFormat_Undefined);
    assert(pipeline);
    /* stripIndexFormat is only valid for strips: a point list with one rejects. */
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Validation);
    assert(!points(&s,WGPUPrimitiveTopology_PointList,WGPUIndexFormat_Uint16));
    expect_validation(&s);
    const point data[VERTICES] = {
        at(0,0,1,0,0), at(3,1,1,0,0), at(7,3,1,0,0),   /* 0-2 direct, red */
        at(1,2,0,1,0), at(5,0,0,1,0), at(6,3,0,1,0),   /* 3-5 bundle, green */
        at(4,2,0,0,1), at(2,1,1,1,0),                  /* 6 indexed blue, 7 unselected decoy */
        at(2,3,0,0,1), at(7,0,0,0,1)};                 /* 8-9 indexed blue */
    WGPUBuffer vertices = buffer(&s,sizeof(data),WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s.queue,vertices,0,data,sizeof(data));
    const uint16_t indices[4] = {6,8,9,0};
    WGPUBuffer index_buffer = buffer(&s,sizeof(indices),WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s.queue,index_buffer,0,indices,sizeof(indices));
    WGPUTextureDescriptor texture_descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texture_descriptor.format = WGPUTextureFormat_RGBA8Unorm;
    texture_descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    texture_descriptor.size = (WGPUExtent3D){WIDTH,HEIGHT,1};
    WGPUTexture target = wgpuDeviceCreateTexture(s.device,&texture_descriptor);
    WGPUTextureView view = wgpuTextureCreateView(target,NULL);
    assert(target && view);
    WGPURenderBundleEncoderDescriptor bundle_descriptor = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
    bundle_descriptor.colorFormatCount = 1;
    bundle_descriptor.colorFormats = &texture_descriptor.format;
    WGPURenderBundleEncoder builder = wgpuDeviceCreateRenderBundleEncoder(s.device,&bundle_descriptor);
    wgpuRenderBundleEncoderSetPipeline(builder,pipeline);
    wgpuRenderBundleEncoderSetVertexBuffer(builder,0,vertices,0,WGPU_WHOLE_SIZE);
    wgpuRenderBundleEncoderDraw(builder,3,1,3,0);
    WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(builder,NULL);
    wgpuRenderBundleEncoderRelease(builder);
    assert(bundle);
    WGPUBuffer readback = buffer(&s,256 * HEIGHT,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    attachment.clearValue = (WGPUColor){0,0,0,1};
    WGPURenderPassDescriptor pass_descriptor = {.colorAttachmentCount=1,.colorAttachments=&attachment};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&pass_descriptor);
    wgpuRenderPassEncoderSetPipeline(pass,pipeline);
    wgpuRenderPassEncoderSetVertexBuffer(pass,0,vertices,0,WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDraw(pass,3,1,0,0);
    wgpuRenderPassEncoderExecuteBundles(pass,1,&bundle);
    wgpuRenderPassEncoderSetPipeline(pass,pipeline);
    wgpuRenderPassEncoderSetVertexBuffer(pass,0,vertices,0,WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass,index_buffer,WGPUIndexFormat_Uint16,0,6);
    wgpuRenderPassEncoderDrawIndexed(pass,3,1,0,0,0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    texture_to_buffer(encoder,target,readback,0,WIDTH,HEIGHT);
    wgpuRenderBundleRelease(bundle);
    wgpuRenderPipelineRelease(pipeline);
    wgpuBufferRelease(vertices);
    wgpuBufferRelease(index_buffer);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(target);
    submit(&s,encoder);
    const unsigned char *bytes = map(&s,readback,256 * HEIGHT);
    unsigned char expected[HEIGHT][WIDTH][4];
    for (int y = 0; y < HEIGHT; ++y) for (int x = 0; x < WIDTH; ++x) {
        expected[y][x][0] = expected[y][x][1] = expected[y][x][2] = 0;
        expected[y][x][3] = 255;
    }
    const int drawn[] = {0,1,2,3,4,5,6,8,9};
    for (size_t i = 0; i < sizeof(drawn) / sizeof(*drawn); ++i) {
        const point *p = &data[drawn[i]];
        int px = (int)((p->x + 1.0f) * WIDTH / 2.0f), py = (int)((1.0f - p->y) * HEIGHT / 2.0f);
        for (int c = 0; c < 4; ++c) expected[py][px][c] = (unsigned char)(p->color[c] * 255.0f);
    }
    unsigned covered = 0;
    for (int y = 0; y < HEIGHT; ++y) for (int x = 0; x < WIDTH; ++x) {
        assert(!memcmp(bytes + y * 256 + x * 4,expected[y][x],4));
        covered += expected[y][x][0] || expected[y][x][1] || expected[y][x][2];
    }
    assert(covered == 9 && expected[1][2][0] == 0); /* Decoy (2,1) stays clear. */
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    consumer_finish(&s);
    printf("PASS public point lists: enabled maintenance5 default point size, 9 one-pixel points at exact centers from a direct draw, a render bundle and an indexed draw with an unselected decoy, stripIndexFormat control, early releases; exact 32 pixels %s\n",
        adapter_class_scope());
}
