/* SPDX-License-Identifier: MIT
 * Public indexed drawing through pass/bundle entrypoints: uint16/uint32 indices,
 * signed base vertex, firstIndex/instance selection, strip primitive restart,
 * GPU-produced indexed indirect arguments and wrong-but-typed admission controls.
 * Each 16x4 column is one draw under its own scissor, giving an exact per-draw
 * pixel oracle; the strip target is checked as exact 128 pixels. */
#include "consumer.h"

enum { COLUMNS = 16, ROWS = 4, STRIP_WIDTH = 16, STRIP_HEIGHT = 8, ARGUMENT_RECORDS = 9 };
enum { CLEAR = 0, RED = 1, GREEN = 2, WHITE = 3 };

static WGPURenderPipeline graphics(state *s, WGPUPrimitiveTopology topology, WGPUIndexFormat strip)
{
    const char *code =
        "struct O{@builtin(position) p:vec4f,@location(0) @interpolate(flat) c:vec4f}"
        "@vertex fn vertex(@location(0) point:vec2f,@location(1) color:vec4f,@location(2) shift:vec2f)->O{"
        "return O(vec4f(point+shift,0.0,1.0),color);}"
        "@fragment fn fragment(input:O)->@location(0) vec4f{return input.c;}";
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code,WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader = {.nextInChain=&source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device,&shader);
    assert(module);
    WGPUVertexAttribute attributes[] = {{.format=WGPUVertexFormat_Float32x2,.shaderLocation=0},
        {.format=WGPUVertexFormat_Float32x4,.offset=8,.shaderLocation=1},
        {.format=WGPUVertexFormat_Float32x2,.shaderLocation=2}};
    WGPUVertexBufferLayout buffers[] = {{.arrayStride=24,.attributeCount=2,.attributes=attributes},
        {.arrayStride=8,.stepMode=WGPUVertexStepMode_Instance,.attributeCount=1,.attributes=attributes+2}};
    WGPUColorTargetState color = {.format=WGPUTextureFormat_RGBA8Unorm,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=module,.targetCount=1,.targets=&color};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex = (WGPUVertexState){.module=module,.bufferCount=2,.buffers=buffers};
    descriptor.fragment = &fragment;
    descriptor.primitive.topology = topology;
    descriptor.primitive.stripIndexFormat = strip;
    WGPURenderPipeline result = wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
    wgpuShaderModuleRelease(module);
    return result;
}
static WGPUBuffer filled(state *s, WGPUBufferUsage usage, const void *data, uint64_t size)
{
    WGPUBuffer result = buffer(s,size,usage|WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue,result,0,data,(size_t)size);
    return result;
}
static WGPUTextureView target(state *s, uint32_t width, uint32_t height, WGPUTexture *image)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = WGPUTextureFormat_RGBA8Unorm;
    descriptor.size = (WGPUExtent3D){width,height,1};
    descriptor.usage = WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc;
    *image = wgpuDeviceCreateTexture(s->device,&descriptor);
    assert(*image);
    WGPUTextureView view = wgpuTextureCreateView(*image,NULL);
    assert(view);
    return view;
}
static WGPURenderPassEncoder begin(WGPUCommandEncoder encoder, WGPUTextureView view)
{
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    attachment.clearValue = (WGPUColor){0,0,1,1};
    WGPURenderPassDescriptor descriptor = {.colorAttachmentCount=1,.colorAttachments=&attachment};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
    assert(pass);
    return pass;
}
static void expect_pixel(const unsigned char *pixel, int color)
{
    const unsigned char expected[4][4] = {{0,0,255,255},{255,0,0,255},{0,255,0,255},{255,255,255,255}};
    assert(!memcmp(pixel,expected[color],4));
}

typedef struct fixture {
    WGPURenderPipeline list, strip16, strip32;
    WGPUBuffer points, strip_points, shifts, indices16, indices32, strip16_indices, strip32_indices;
    WGPUBuffer arguments, copied;
    WGPUComputePipeline producer, inspector;
    WGPUBindGroup produce_group, inspect_group;
} fixture;

/* GPU-produced drawIndexed records at offset 4 + 20*i. Index16 capacity is 12,
 * the instance-step buffer holds two instances (instance0 offscreen). */
static const uint32_t argument_words[ARGUMENT_RECORDS * 5 + 1] = {0xa5a5a5a5,
    3,2,9,0,0,                  /* exact end: firstIndex+count == capacity, white */
    3,2,10,0,0,                 /* one past the bound index range: no-op */
    3,2,UINT32_MAX,0,0,         /* wrapping firstIndex+count: no-op */
    0,2,3,0,0,                  /* zero count */
    3,2,3,0,1,                  /* nonzero firstInstance without the feature: no-op */
    3,3,3,0,0,                  /* instance-step overrun: no-op */
    3,2,6,(uint32_t)-3,0,       /* negative base vertex: indices 3,4,5 -> 0,1,2 red */
    3,2,3,3,0,                  /* bundle: indices 0,1,2 + 3 -> green */
    3,2,0,(uint32_t)-6,0};      /* uint32 offset range: 6,7,8 - 6 -> red */

static void bind(WGPURenderPassEncoder pass, const fixture *f, WGPURenderPipeline pipeline, WGPUBuffer points)
{
    wgpuRenderPassEncoderSetPipeline(pass,pipeline);
    wgpuRenderPassEncoderSetVertexBuffer(pass,0,points,0,WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(pass,1,f->shifts,0,16);
}
static void build(state *s, fixture *f)
{
    f->list = graphics(s,WGPUPrimitiveTopology_TriangleList,WGPUIndexFormat_Undefined);
    f->strip16 = graphics(s,WGPUPrimitiveTopology_TriangleStrip,WGPUIndexFormat_Uint16);
    f->strip32 = graphics(s,WGPUPrimitiveTopology_TriangleStrip,WGPUIndexFormat_Uint32);
    assert(f->list && f->strip16 && f->strip32);
    /* Vertices 0-2 red, 3-5 green, 6-8 white full-target triangles, 9-11 offscreen. */
    float points[12][6];
    const float corners[3][2] = {{-1,-1},{3,-1},{-1,3}};
    const float colors[4][4] = {{1,0,0,1},{0,1,0,1},{1,1,1,1},{0,0,0,1}};
    for (size_t i = 0; i < 12; ++i) {
        points[i][0] = i < 9 ? corners[i % 3][0] : 5 + corners[i % 3][0];
        points[i][1] = i < 9 ? corners[i % 3][1] : 5 + corners[i % 3][1];
        memcpy(&points[i][2],colors[i / 3],sizeof(colors[0]));
    }
    f->points = filled(s,WGPUBufferUsage_Vertex,points,sizeof(points));
    /* Two strip quads on pixel boundaries: red top-left 2x2, green bottom-right 2x2. */
    const float quad[8][6] = {{-1,1,1,0,0,1},{-1,0.5f,1,0,0,1},{-0.5f,1,1,0,0,1},{-0.5f,0.5f,1,0,0,1},
        {0.5f,-0.5f,0,1,0,1},{0.5f,-1,0,1,0,1},{1,-0.5f,0,1,0,1},{1,-1,0,1,0,1}};
    f->strip_points = filled(s,WGPUBufferUsage_Vertex,quad,sizeof(quad));
    const float shifts[4] = {100,100,0,0};
    f->shifts = filled(s,WGPUBufferUsage_Vertex,shifts,sizeof(shifts));
    const uint16_t indices16[16] = {9,10,11,0,1,2,3,4,5,6,7,8,0xbeef,0xbeef,0xbeef,0xbeef};
    f->indices16 = filled(s,WGPUBufferUsage_Index|WGPUBufferUsage_Vertex,indices16,sizeof(indices16));
    const uint32_t indices32[4] = {0xa5a5a5a5,6,7,8};
    f->indices32 = filled(s,WGPUBufferUsage_Index,indices32,sizeof(indices32));
    /* Restart separates the quads. Without restart the joining triangles would
     * reference an out-of-bounds vertex and could cover the center pixels. */
    const uint16_t strip16[10] = {0,1,2,3,0xffff,4,5,6,7,0};
    f->strip16_indices = filled(s,WGPUBufferUsage_Index,strip16,sizeof(strip16));
    const uint32_t strip32[9] = {0,1,2,3,0xffffffffu,4,5,6,7};
    f->strip32_indices = filled(s,WGPUBufferUsage_Index,strip32,sizeof(strip32));
    char source[1400];
    size_t length = (size_t)snprintf(source,sizeof(source),
        "@group(0) @binding(0) var<storage,read_write> args:array<u32>;"
        "@compute @workgroup_size(1) fn main(){");
    for (size_t i = 0; i < sizeof(argument_words) / sizeof(*argument_words); ++i) {
        int written = snprintf(source + length,sizeof(source) - length,"args[%zu]=%uu;",i,argument_words[i]);
        assert(written > 0 && (size_t)written < sizeof(source) - length);
        length += (size_t)written;
    }
    assert(length + 2 < sizeof(source));
    source[length++] = '}';
    source[length] = 0;
    f->producer = pipeline(s,source);
    f->inspector = pipeline(s,
        "@group(0) @binding(0) var<storage,read> args:array<u32>;"
        "@group(0) @binding(1) var<storage,read_write> out:array<u32>;"
        "@compute @workgroup_size(1) fn main(){for(var i=0u;i<46u;i++){out[i]=args[i];}}");
    f->arguments = buffer(s,sizeof(argument_words),WGPUBufferUsage_Storage|WGPUBufferUsage_Indirect);
    f->copied = buffer(s,sizeof(argument_words),WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc);
    WGPUBindGroupEntry args_entry = {.binding=0,.buffer=f->arguments,.size=sizeof(argument_words)};
    f->produce_group = group(s,f->producer,&args_entry,1);
    WGPUBindGroupEntry inspect_entries[] = {args_entry,{.binding=1,.buffer=f->copied,.size=sizeof(argument_words)}};
    f->inspect_group = group(s,f->inspector,inspect_entries,2);
}
static void release(fixture *f)
{
    wgpuRenderPipelineRelease(f->list);
    wgpuRenderPipelineRelease(f->strip16);
    wgpuRenderPipelineRelease(f->strip32);
    WGPUBuffer buffers[] = {f->points,f->strip_points,f->shifts,f->indices16,f->indices32,
        f->strip16_indices,f->strip32_indices,f->arguments,f->copied};
    for (size_t i = 0; i < sizeof(buffers) / sizeof(*buffers); ++i) wgpuBufferRelease(buffers[i]);
    wgpuComputePipelineRelease(f->producer);
    wgpuComputePipelineRelease(f->inspector);
    wgpuBindGroupRelease(f->produce_group);
    wgpuBindGroupRelease(f->inspect_group);
}
static WGPURenderBundle bundle(state *s, const fixture *f, bool indirect)
{
    WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
    WGPURenderBundleEncoderDescriptor descriptor = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
    descriptor.colorFormatCount = 1;
    descriptor.colorFormats = &format;
    WGPURenderBundleEncoder encoder = wgpuDeviceCreateRenderBundleEncoder(s->device,&descriptor);
    wgpuRenderBundleEncoderSetPipeline(encoder,f->list);
    wgpuRenderBundleEncoderSetVertexBuffer(encoder,0,f->points,0,WGPU_WHOLE_SIZE);
    wgpuRenderBundleEncoderSetVertexBuffer(encoder,1,f->shifts,0,16);
    if (indirect) {
        wgpuRenderBundleEncoderSetIndexBuffer(encoder,f->indices16,WGPUIndexFormat_Uint16,0,24);
        wgpuRenderBundleEncoderDrawIndexedIndirect(encoder,f->arguments,4 + 7 * 20);
    } else {
        /* Offset 6 starts at element 3; bound elements 6..8 hold 6,7,8: white. */
        wgpuRenderBundleEncoderSetIndexBuffer(encoder,f->indices16,WGPUIndexFormat_Uint16,6,18);
        wgpuRenderBundleEncoderDrawIndexed(encoder,3,1,6,0,1);
    }
    WGPURenderBundle result = wgpuRenderBundleEncoderFinish(encoder,NULL);
    wgpuRenderBundleEncoderRelease(encoder);
    assert(result);
    return result;
}
static void compute(WGPUCommandEncoder encoder, WGPUComputePipeline pipeline, WGPUBindGroup group)
{
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderSetPipeline(pass,pipeline);
    wgpuComputePassEncoderSetBindGroup(pass,0,group,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
}
static void columns(state *s, const fixture *f, WGPUCommandEncoder encoder, WGPUTextureView view)
{
    WGPURenderBundle direct_bundle = bundle(s,f,false), indirect_bundle = bundle(s,f,true);
    WGPURenderPassEncoder pass = begin(encoder,view);
    bind(pass,f,f->list,f->points);
    wgpuRenderPassEncoderSetIndexBuffer(pass,f->indices16,WGPUIndexFormat_Uint16,0,24);
    const uint32_t direct[6][5] = {
        {3,1,3,0,1},    /* c0 firstIndex selects 0,1,2: red */
        {3,1,3,6,1},    /* c1 positive base vertex: white */
        {0,0,0,0,0},    /* c2 uint32 negative base, below */
        {3,1,6,0,0},    /* c3 only offscreen instance0: clear */
        {3,2,6,0,0},    /* c4 instance1 visible: green */
        {3,1,0,0,1}};   /* c5 indices 9,10,11 offscreen: clear */
    for (uint32_t column = 0; column < 6; ++column) {
        wgpuRenderPassEncoderSetScissorRect(pass,column,0,1,ROWS);
        if (column == 2) {
            wgpuRenderPassEncoderSetIndexBuffer(pass,f->indices32,WGPUIndexFormat_Uint32,4,12);
            wgpuRenderPassEncoderDrawIndexed(pass,3,1,0,-3,1);
            wgpuRenderPassEncoderSetIndexBuffer(pass,f->indices16,WGPUIndexFormat_Uint16,0,WGPU_WHOLE_SIZE);
            continue;
        }
        const uint32_t *d = direct[column];
        wgpuRenderPassEncoderDrawIndexed(pass,d[0],d[1],d[2],(int32_t)d[3],d[4]);
    }
    wgpuRenderPassEncoderSetScissorRect(pass,6,0,1,ROWS);
    wgpuRenderPassEncoderExecuteBundles(pass,1,&direct_bundle);
    /* Bundle execution clears pass bindings; rebind the exact 24-byte range. */
    bind(pass,f,f->list,f->points);
    wgpuRenderPassEncoderSetIndexBuffer(pass,f->indices16,WGPUIndexFormat_Uint16,0,24);
    for (uint32_t record = 0; record < 7; ++record) {
        wgpuRenderPassEncoderSetScissorRect(pass,7 + record,0,1,ROWS);
        wgpuRenderPassEncoderDrawIndexedIndirect(pass,f->arguments,4 + record * 20);
    }
    wgpuRenderPassEncoderSetScissorRect(pass,14,0,1,ROWS);
    wgpuRenderPassEncoderExecuteBundles(pass,1,&indirect_bundle);
    bind(pass,f,f->list,f->points);
    wgpuRenderPassEncoderSetIndexBuffer(pass,f->indices32,WGPUIndexFormat_Uint32,4,12);
    wgpuRenderPassEncoderSetScissorRect(pass,15,0,1,ROWS);
    wgpuRenderPassEncoderDrawIndexedIndirect(pass,f->arguments,4 + 8 * 20);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuRenderBundleRelease(direct_bundle);
    wgpuRenderBundleRelease(indirect_bundle);
}
static void strips(const fixture *f, WGPUCommandEncoder encoder, WGPUTextureView view)
{
    WGPURenderPassEncoder pass = begin(encoder,view);
    bind(pass,f,f->strip16,f->strip_points);
    wgpuRenderPassEncoderSetViewport(pass,0,0,8,8,0,1);
    wgpuRenderPassEncoderSetIndexBuffer(pass,f->strip16_indices,WGPUIndexFormat_Uint16,0,18);
    wgpuRenderPassEncoderDrawIndexed(pass,9,1,0,0,1);
    bind(pass,f,f->strip32,f->strip_points);
    wgpuRenderPassEncoderSetViewport(pass,8,0,8,8,0,1);
    wgpuRenderPassEncoderSetIndexBuffer(pass,f->strip32_indices,WGPUIndexFormat_Uint32,0,WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(pass,9,1,0,0,1);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

/* Each control invalidates exactly one encoder through public validation. */
static void rejected(state *s, const fixture *f, WGPUTextureView view, unsigned control)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    WGPURenderPassEncoder pass = begin(encoder,view);
    bind(pass,f,control == 6 ? f->strip16 : f->list,f->points);
    if (control != 1)
        wgpuRenderPassEncoderSetIndexBuffer(pass,f->indices16,WGPUIndexFormat_Uint16,0,24);
    switch (control) {
    case 0: wgpuRenderPassEncoderDrawIndexed(pass,3,1,10,0,1); break;     /* 13 > 12 indices */
    case 1: wgpuRenderPassEncoderDrawIndexed(pass,3,1,0,0,1); break;      /* no index buffer */
    case 2: wgpuRenderPassEncoderSetIndexBuffer(pass,f->indices16,WGPUIndexFormat_Undefined,0,24); break;
    case 3: wgpuRenderPassEncoderSetIndexBuffer(pass,f->indices32,WGPUIndexFormat_Uint32,2,8); break;
    case 4: wgpuRenderPassEncoderSetIndexBuffer(pass,f->shifts,WGPUIndexFormat_Uint16,0,16); break;
    case 5: wgpuRenderPassEncoderSetIndexBuffer(pass,f->indices16,WGPUIndexFormat_Uint16,8,32); break;
    case 6:                                                                /* strip format mismatch */
        wgpuRenderPassEncoderSetIndexBuffer(pass,f->indices32,WGPUIndexFormat_Uint32,4,12);
        wgpuRenderPassEncoderDrawIndexed(pass,3,1,0,0,1);
        break;
    case 7: wgpuRenderPassEncoderDrawIndexed(pass,3,3,3,0,0); break;      /* 3 instances > 2 */
    case 8: wgpuRenderPassEncoderDrawIndexedIndirect(pass,f->indices16,0); break; /* no Indirect usage */
    case 9: wgpuRenderPassEncoderDrawIndexedIndirect(pass,f->arguments,2); break;  /* misaligned */
    case 10: wgpuRenderPassEncoderDrawIndexedIndirect(pass,f->arguments,4 + 8 * 20 + 4); break; /* past end */
    default: assert(false);
    }
    wgpuRenderPassEncoderEnd(pass);
    assert(!wgpuCommandEncoderFinish(encoder,NULL));
    expect_validation(s);
    wgpuRenderPassEncoderRelease(pass);
    wgpuCommandEncoderRelease(encoder);
}
static void rejected_bundle(state *s, const fixture *f, bool indirect)
{
    WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
    WGPURenderBundleEncoderDescriptor descriptor = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
    descriptor.colorFormatCount = 1;
    descriptor.colorFormats = &format;
    WGPURenderBundleEncoder encoder = wgpuDeviceCreateRenderBundleEncoder(s->device,&descriptor);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    wgpuRenderBundleEncoderSetPipeline(encoder,f->list);
    wgpuRenderBundleEncoderSetVertexBuffer(encoder,0,f->points,0,WGPU_WHOLE_SIZE);
    wgpuRenderBundleEncoderSetVertexBuffer(encoder,1,f->shifts,0,16);
    wgpuRenderBundleEncoderSetIndexBuffer(encoder,f->indices16,WGPUIndexFormat_Uint16,0,24);
    if (indirect) wgpuRenderBundleEncoderDrawIndexedIndirect(encoder,f->shifts,0);
    else wgpuRenderBundleEncoderDrawIndexed(encoder,3,1,10,0,1);
    assert(!wgpuRenderBundleEncoderFinish(encoder,NULL));
    expect_validation(s);
    wgpuRenderBundleEncoderRelease(encoder);
}
static void pipeline_controls(state *s)
{
    /* stripIndexFormat is not allowed on list topologies; strip formats must be real. */
    const WGPUPrimitiveTopology topology[] = {WGPUPrimitiveTopology_TriangleList, WGPUPrimitiveTopology_TriangleStrip};
    const WGPUIndexFormat format[] = {WGPUIndexFormat_Uint16, (WGPUIndexFormat)7};
    for (size_t i = 0; i < 2; ++i) {
        wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
        assert(!graphics(s,topology[i],format[i]));
        expect_validation(s);
    }
}
int main(void)
{
    state s = {0};
    consumer_init(&s);
    fixture f = {0};
    build(&s,&f);
    WGPUTexture column_image, strip_image;
    WGPUTextureView column_view = target(&s,COLUMNS,ROWS,&column_image);
    WGPUTextureView strip_view = target(&s,STRIP_WIDTH,STRIP_HEIGHT,&strip_image);
    pipeline_controls(&s);
    for (unsigned control = 0; control < 11; ++control) rejected(&s,&f,column_view,control);
    rejected_bundle(&s,&f,false);
    rejected_bundle(&s,&f,true);
    WGPUBuffer readback = buffer(&s,3328,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    compute(encoder,f.producer,f.produce_group);
    columns(&s,&f,encoder,column_view);
    strips(&f,encoder,strip_view);
    compute(encoder,f.inspector,f.inspect_group);
    texture_to_buffer(encoder,column_image,readback,0,COLUMNS,ROWS);
    WGPUTexelCopyTextureInfo source = {.texture=strip_image};
    WGPUTexelCopyBufferInfo destination = {.buffer=readback,.layout={.offset=1024,.bytesPerRow=256,.rowsPerImage=STRIP_HEIGHT}};
    WGPUExtent3D size = {STRIP_WIDTH,STRIP_HEIGHT,1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder,&source,&destination,&size);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,f.copied,0,readback,3072,sizeof(argument_words));
    /* Early caller releases: commands retain every resource through retirement. */
    release(&f);
    wgpuTextureViewRelease(column_view);
    wgpuTextureViewRelease(strip_view);
    wgpuTextureRelease(column_image);
    wgpuTextureRelease(strip_image);
    submit(&s,encoder);
    const unsigned char *bytes = map(&s,readback,3328);
    const int expected[COLUMNS] = {RED,WHITE,GREEN,CLEAR,GREEN,CLEAR,WHITE,
        WHITE,CLEAR,CLEAR,CLEAR,CLEAR,CLEAR,RED,GREEN,RED};
    for (size_t y = 0; y < ROWS; ++y)
        for (size_t x = 0; x < COLUMNS; ++x) expect_pixel(bytes + y * 256 + x * 4,expected[x]);
    for (size_t y = 0; y < STRIP_HEIGHT; ++y)
        for (size_t x = 0; x < STRIP_WIDTH; ++x) {
            size_t local = x % 8;
            int color = local < 2 && y < 2 ? RED : local >= 6 && y >= 6 ? GREEN : CLEAR;
            expect_pixel(bytes + 1024 + y * 256 + x * 4,color);
        }
    assert(!memcmp(bytes + 3072,argument_words,sizeof(argument_words)));
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    consumer_finish(&s);
    printf("PASS public indexed draws: uint16/uint32 indices, firstIndex, signed base vertex, instance selection, offset index ranges, pass/bundle replay, uint16/uint32 strip primitive restart, 9 GPU-produced drawIndexedIndirect records with exact-end/out-of-range/wrapping/zero/firstInstance/instance-overrun guards, unchanged arguments, 13 typed validation controls and early releases; exact 192 pixels %s\n",
        adapter_class_scope());
}
