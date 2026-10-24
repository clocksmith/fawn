/* SPDX-License-Identifier: MIT
 * Public WebGPU core color formats beyond the original set: r8/rg8 snorm/uint/
 * sint, r16/rg16 uint/sint, rgb10a2unorm/uint and rgb9e5ufloat. Every one
 * round-trips exact bytes through writeTexture, a texture-to-texture copy and a
 * texture-to-buffer copy; renderable ones clear to exact encoded values; snorm,
 * rgb9e5 and rgb10a2 decode exactly in textureLoad; and the WebGPU capability
 * table rejects attachments of non-renderable formats, multisampling of formats
 * without it and resolving into non-resolvable formats. */
#include "consumer.h"
#include <stdlib.h>

enum { SIZE = 4, PITCH = 256 };

typedef struct format_case {
    WGPUTextureFormat format;
    const char *name;
    uint32_t bytes;
    bool renderable;
    double clear[4];
    uint8_t cleared[8]; /* expected texel bytes after the clear */
} format_case;

static const format_case formats[] = {
    {WGPUTextureFormat_R8Snorm, "r8snorm", 1, false, {0}, {0}},
    {WGPUTextureFormat_R8Uint, "r8uint", 1, true, {200,0,0,0}, {0xC8}},
    {WGPUTextureFormat_R8Sint, "r8sint", 1, true, {-3,0,0,0}, {0xFD}},
    {WGPUTextureFormat_RG8Snorm, "rg8snorm", 2, false, {0}, {0}},
    {WGPUTextureFormat_RG8Uint, "rg8uint", 2, true, {7,250,0,0}, {7,250}},
    {WGPUTextureFormat_RG8Sint, "rg8sint", 2, true, {-128,127,0,0}, {0x80,0x7F}},
    {WGPUTextureFormat_R16Uint, "r16uint", 2, true, {65535,0,0,0}, {0xFF,0xFF}},
    {WGPUTextureFormat_R16Sint, "r16sint", 2, true, {-2,0,0,0}, {0xFE,0xFF}},
    {WGPUTextureFormat_RG16Uint, "rg16uint", 4, true, {1,60000,0,0}, {1,0,0x60,0xEA}},
    {WGPUTextureFormat_RG16Sint, "rg16sint", 4, true, {-32768,300,0,0}, {0x00,0x80,0x2C,0x01}},
    /* r 1023 | g 0 | b 1023 << 20 | a 3 << 30 */
    {WGPUTextureFormat_RGB10A2Unorm, "rgb10a2unorm", 4, true, {1,0,1,1}, {0xFF,0x03,0xF0,0xFF}},
    /* r 1023 | g 5 << 10 | b 512 << 20 | a 2 << 30 */
    {WGPUTextureFormat_RGB10A2Uint, "rgb10a2uint", 4, true, {1023,5,512,2}, {0xFF,0x17,0x00,0xA0}},
    {WGPUTextureFormat_RGB9E5Ufloat, "rgb9e5ufloat", 4, false, {0}, {0}},
};

static WGPUTexture image(state *s, WGPUTextureFormat format, WGPUTextureUsage usage, uint32_t samples)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = format;
    descriptor.usage = usage;
    descriptor.size = (WGPUExtent3D){SIZE,SIZE,1};
    descriptor.sampleCount = samples;
    return wgpuDeviceCreateTexture(s->device,&descriptor);
}
static void read_texels(state *s, WGPUTexture texture, uint32_t bytes, uint8_t out[SIZE * SIZE * 8])
{
    WGPUBuffer readback = buffer(s,PITCH * SIZE,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    texture_to_buffer(encoder,texture,readback,0,SIZE,SIZE);
    submit(s,encoder);
    const uint8_t *mapped = map(s,readback,PITCH * SIZE);
    for (uint32_t y = 0; y < SIZE; ++y) memcpy(out + y * SIZE * bytes,mapped + y * PITCH,SIZE * bytes);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}

/* writeTexture, a texture-to-texture copy and a texture-to-buffer copy keep every byte. */
static void round_trip(state *s, const format_case *f)
{
    const WGPUTextureUsage usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
    WGPUTexture source = image(s,f->format,usage,1), copy = image(s,f->format,usage,1);
    assert(source && copy);
    uint8_t data[SIZE * SIZE * 8];
    for (size_t i = 0; i < sizeof(data); ++i) data[i] = (uint8_t)(i * 37 + 11);
    WGPUTexelCopyTextureInfo destination = {.texture=source,.aspect=WGPUTextureAspect_All};
    WGPUTexelCopyBufferLayout layout = {.bytesPerRow=SIZE * f->bytes,.rowsPerImage=SIZE};
    wgpuQueueWriteTexture(s->queue,&destination,data,SIZE * SIZE * f->bytes,&layout,&(WGPUExtent3D){SIZE,SIZE,1});
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    wgpuCommandEncoderCopyTextureToTexture(encoder,&(WGPUTexelCopyTextureInfo){.texture=source},
        &(WGPUTexelCopyTextureInfo){.texture=copy},&(WGPUExtent3D){SIZE,SIZE,1});
    submit(s,encoder);
    uint8_t back[SIZE * SIZE * 8];
    read_texels(s,copy,f->bytes,back);
    if (memcmp(back,data,SIZE * SIZE * f->bytes)) {
        fprintf(stderr,"%s round trip mismatch\n",f->name);
        abort();
    }
    wgpuTextureRelease(copy);
    wgpuTextureRelease(source);
}

/* A render-pass clear stores the exactly encoded texel everywhere. */
static void clear(state *s, const format_case *f)
{
    WGPUTexture target = image(s,f->format,WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc,1);
    assert(target);
    WGPUTextureView view = wgpuTextureCreateView(target,NULL);
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = view;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = (WGPUColor){f->clear[0],f->clear[1],f->clear[2],f->clear[3]};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPURenderPassDescriptor descriptor = {.colorAttachmentCount=1,.colorAttachments=&color};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    submit(s,encoder);
    uint8_t back[SIZE * SIZE * 8];
    read_texels(s,target,f->bytes,back);
    for (uint32_t i = 0; i < SIZE * SIZE; ++i) if (memcmp(back + i * f->bytes,f->cleared,f->bytes)) {
        fprintf(stderr,"%s clear texel %u mismatch\n",f->name,i);
        abort();
    }
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(target);
}

/* textureLoad of texel 0 from a 1x1-written texture into o[slot]. */
static void load_texel(state *s, WGPUTextureFormat format, const void *texel, uint32_t bytes, const char *type,
    float out[4])
{
    WGPUTexture texture = image(s,format,WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,1);
    WGPUTexelCopyTextureInfo destination = {.texture=texture,.aspect=WGPUTextureAspect_All};
    WGPUTexelCopyBufferLayout layout = {.bytesPerRow=bytes,.rowsPerImage=1};
    wgpuQueueWriteTexture(s->queue,&destination,texel,bytes,&layout,&(WGPUExtent3D){1,1,1});
    char code[320];
    snprintf(code,sizeof(code),"@group(0) @binding(0) var t:texture_2d<%s>;"
        "@group(0) @binding(1) var<storage,read_write> o:array<vec4f>;"
        "@compute @workgroup_size(1) fn main(){o[0]=vec4f(textureLoad(t,vec2i(0,0),0));}",type);
    WGPUComputePipeline reader = pipeline(s,code);
    WGPUTextureView view = wgpuTextureCreateView(texture,NULL);
    WGPUBuffer output = buffer(s,16,WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(s,16,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUBindGroupEntry entries[2] = {{.binding=0,.textureView=view},{.binding=1,.buffer=output,.size=16}};
    WGPUBindGroup bound = group(s,reader,entries,2);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderSetPipeline(pass,reader);
    wgpuComputePassEncoderSetBindGroup(pass,0,bound,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,output,0,readback,0,16);
    submit(s,encoder);
    memcpy(out,map(s,readback,16),16);
    wgpuBufferUnmap(readback);
    wgpuBindGroupRelease(bound);
    wgpuBufferRelease(readback);
    wgpuBufferRelease(output);
    wgpuTextureViewRelease(view);
    wgpuComputePipelineRelease(reader);
    wgpuTextureRelease(texture);
}
static void expect_vec(const float got[4], float x, float y, float z, float w, const char *name)
{
    if (got[0] != x || got[1] != y || got[2] != z || got[3] != w) {
        fprintf(stderr,"%s loaded %g %g %g %g\n",name,got[0],got[1],got[2],got[3]);
        abort();
    }
}
static void decode(state *s)
{
    float v[4];
    const uint8_t r8[1] = {0x81}; /* -127/127 */
    load_texel(s,WGPUTextureFormat_R8Snorm,r8,1,"f32",v);
    expect_vec(v,-1.0f,0.0f,0.0f,1.0f,"r8snorm");
    const uint8_t rg8[2] = {0x7F,0x00};
    load_texel(s,WGPUTextureFormat_RG8Snorm,rg8,2,"f32",v);
    expect_vec(v,1.0f,0.0f,0.0f,1.0f,"rg8snorm");
    /* Shared exponent 17 (bias 15, 9-bit mantissas): 256, 128, 64 -> 2, 1, 0.5. */
    const uint32_t e5 = 256u | 128u << 9 | 64u << 18 | 17u << 27;
    load_texel(s,WGPUTextureFormat_RGB9E5Ufloat,&e5,4,"f32",v);
    expect_vec(v,2.0f,1.0f,0.5f,1.0f,"rgb9e5ufloat");
    const uint32_t a2 = 1023u | 0u << 10 | 1023u << 20 | 3u << 30;
    load_texel(s,WGPUTextureFormat_RGB10A2Unorm,&a2,4,"f32",v);
    expect_vec(v,1.0f,0.0f,1.0f,1.0f,"rgb10a2unorm");
    const uint32_t a2u = 1023u | 5u << 10 | 512u << 20 | 2u << 30;
    load_texel(s,WGPUTextureFormat_RGB10A2Uint,&a2u,4,"u32",v);
    expect_vec(v,1023.0f,5.0f,512.0f,2.0f,"rgb10a2uint");
    const int16_t r16[2] = {-32768,300};
    load_texel(s,WGPUTextureFormat_RG16Sint,r16,4,"i32",v);
    expect_vec(v,-32768.0f,300.0f,0.0f,1.0f,"rg16sint");
}

/* WebGPU capability-table rejections, each one validation error. */
static void controls(state *s)
{
    const struct { WGPUTextureFormat format; WGPUTextureUsage usage; uint32_t samples; } rejected[] = {
        {WGPUTextureFormat_R8Snorm, WGPUTextureUsage_RenderAttachment, 1},
        {WGPUTextureFormat_RGBA8Snorm, WGPUTextureUsage_RenderAttachment, 1},
        {WGPUTextureFormat_RGB9E5Ufloat, WGPUTextureUsage_RenderAttachment, 1},
        {WGPUTextureFormat_R32Uint, WGPUTextureUsage_RenderAttachment, 4},
        {WGPUTextureFormat_RG32Float, WGPUTextureUsage_RenderAttachment, 4},
    };
    for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
        assert(!image(s,rejected[i].format,rejected[i].usage,rejected[i].samples));
        expect_validation(s);
    }
    /* A multisampled r8uint attachment may not resolve (integer formats never do). */
    WGPUTexture multisampled = image(s,WGPUTextureFormat_R8Uint,WGPUTextureUsage_RenderAttachment,4);
    WGPUTexture resolve = image(s,WGPUTextureFormat_R8Uint,WGPUTextureUsage_RenderAttachment,1);
    assert(multisampled && resolve);
    WGPUTextureView views[2] = {wgpuTextureCreateView(multisampled,NULL),wgpuTextureCreateView(resolve,NULL)};
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = views[0];
    color.resolveTarget = views[1];
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Discard;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPURenderPassDescriptor descriptor = {.colorAttachmentCount=1,.colorAttachments=&color};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    finish_invalid(encoder);
    expect_validation(s);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(views[0]);
    wgpuTextureViewRelease(views[1]);
    wgpuTextureRelease(multisampled);
    wgpuTextureRelease(resolve);
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    size_t cleared = 0;
    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        round_trip(&s,&formats[i]);
        if (formats[i].renderable) {
            clear(&s,&formats[i]);
            ++cleared;
        }
    }
    decode(&s);
    controls(&s);
    assert(s.validation_errors == 6);
    consumer_finish(&s);
    printf("PASS public core formats: %zu added core color formats round-trip exact bytes through writeTexture and "
        "texture/buffer copies, %zu renderable ones clear to exact encodings, snorm/rgb9e5/rgb10a2/int textureLoad "
        "decodes exactly, and non-renderable attachments (r8snorm, rgba8snorm, rgb9e5ufloat), multisampled "
        "r32uint/rg32float and an r8uint resolve reject %s\n",
        sizeof(formats) / sizeof(formats[0]), cleared, adapter_class_scope());
    return 0;
}
