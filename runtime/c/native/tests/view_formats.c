/* SPDX-License-Identifier: MIT
 * Public srgb <-> unorm view reinterpretation through viewFormats: render the
 * linear values of chosen sRGB codes through an srgb view of an rgba8unorm
 * texture and read the stored encoded bytes back by copy and through the unorm
 * view; the reverse direction stores raw bytes through a unorm view of an srgb
 * texture; bgra8unorm keeps its byte order. Controls cover non-pair
 * viewFormats, unlisted views, pipeline/attachment format mismatch and the
 * documented srgb-storage limitation. */
#include "consumer.h"
#include <math.h>

enum { WIDTH = 4, HEIGHT = 2, SLOT = 512 };
static const unsigned char codes[4] = {64, 128, 200, 255};

/* sRGB EOTF in double: the linear value whose encoding is exactly code/255. */
static double linear_of(unsigned char code)
{
    double c = code / 255.0;
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}
static WGPURenderPipeline writer(state *s, WGPUTextureFormat target, bool encoded)
{
    char code[512];
    double v[4];
    for (size_t i = 0; i < 4; ++i) v[i] = encoded ? linear_of(codes[i]) : codes[i] / 255.0;
    int length = snprintf(code,sizeof(code),
        "@vertex fn vertex(@builtin(vertex_index) i:u32)->@builtin(position) vec4f{"
        "let x=f32((i<<1u)&2u)*2.0-1.0;let y=f32(i&2u)*2.0-1.0;return vec4f(x,y,0.0,1.0);}"
        "@fragment fn fragment()->@location(0) vec4f{return vec4f(%.9g,%.9g,%.9g,%.9g);}",v[0],v[1],v[2],v[3]);
    assert(length > 0 && (size_t)length < sizeof(code));
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code,WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader = {.nextInChain=&source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device,&shader);
    assert(module);
    WGPUColorTargetState color = {.format=target,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=module,.targetCount=1,.targets=&color};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex = (WGPUVertexState){.module=module};
    descriptor.fragment = &fragment;
    WGPURenderPipeline result = wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
    wgpuShaderModuleRelease(module);
    assert(result);
    return result;
}
static WGPUTexture image(state *s, WGPUTextureFormat format, WGPUTextureFormat view_format, WGPUTextureUsage usage)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = format;
    descriptor.usage = usage;
    descriptor.size = (WGPUExtent3D){WIDTH,HEIGHT,1};
    descriptor.viewFormatCount = view_format ? 1 : 0;
    descriptor.viewFormats = &view_format;
    return wgpuDeviceCreateTexture(s->device,&descriptor);
}
static WGPUTextureView view_as(WGPUTexture texture, WGPUTextureFormat format)
{
    WGPUTextureViewDescriptor descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    descriptor.format = format;
    return wgpuTextureCreateView(texture,&descriptor);
}
static void draw(WGPUCommandEncoder encoder, WGPUTextureView view, WGPURenderPipeline pipeline)
{
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor descriptor = {.colorAttachmentCount=1,.colorAttachments=&attachment};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
    assert(pass);
    wgpuRenderPassEncoderSetPipeline(pass,pipeline);
    wgpuRenderPassEncoderDraw(pass,3,1,0,0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}
static void copy_out(WGPUCommandEncoder encoder, WGPUTexture texture, WGPUBuffer readback, uint32_t slot)
{
    WGPUTexelCopyTextureInfo source = {.texture=texture};
    WGPUTexelCopyBufferInfo destination = {.buffer=readback,.layout={.offset=slot * SLOT,.bytesPerRow=256,.rowsPerImage=HEIGHT}};
    WGPUExtent3D size = {WIDTH,HEIGHT,1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder,&source,&destination,&size);
}
static void controls(state *s, WGPURenderPipeline unorm_writer)
{
    /* Only the srgb/unorm pair of the format may be listed. */
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!image(s,WGPUTextureFormat_RGBA8Unorm,WGPUTextureFormat_RG8Unorm,WGPUTextureUsage_TextureBinding));
    expect_validation(s);
    /* A view format must be listed at creation. */
    WGPUTexture plain = image(s,WGPUTextureFormat_RGBA8Unorm,WGPUTextureFormat_Undefined,
        WGPUTextureUsage_RenderAttachment);
    assert(plain);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!view_as(plain,WGPUTextureFormat_RGBA8UnormSrgb));
    expect_validation(s);
    wgpuTextureRelease(plain);
    /* The pipeline target must match the view format, not the texture format. */
    WGPUTexture listed = image(s,WGPUTextureFormat_RGBA8Unorm,WGPUTextureFormat_RGBA8UnormSrgb,
        WGPUTextureUsage_RenderAttachment);
    WGPUTextureView srgb = view_as(listed,WGPUTextureFormat_RGBA8UnormSrgb);
    assert(listed && srgb);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    draw(encoder,srgb,unorm_writer);
    finish_invalid(encoder);
    expect_validation(s);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(srgb);
    wgpuTextureRelease(listed);
    /* Documented limitation: storage usage with an srgb view format is
     * unsupported (it would need a per-view usage restriction). */
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!image(s,WGPUTextureFormat_RGBA8Unorm,WGPUTextureFormat_RGBA8UnormSrgb,
        WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding));
    expect_validation(s);
}
int main(void)
{
    state s = {0};
    consumer_init(&s);
    WGPURenderPipeline srgb_writer = writer(&s,WGPUTextureFormat_RGBA8UnormSrgb,true);
    WGPURenderPipeline unorm_writer = writer(&s,WGPUTextureFormat_RGBA8Unorm,false);
    WGPURenderPipeline bgra_writer = writer(&s,WGPUTextureFormat_BGRA8UnormSrgb,true);
    controls(&s,unorm_writer);
    const WGPUTextureUsage usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc |
        WGPUTextureUsage_TextureBinding;
    WGPUTexture unorm = image(&s,WGPUTextureFormat_RGBA8Unorm,WGPUTextureFormat_RGBA8UnormSrgb,usage);
    WGPUTexture srgb = image(&s,WGPUTextureFormat_RGBA8UnormSrgb,WGPUTextureFormat_RGBA8Unorm,usage);
    WGPUTexture bgra = image(&s,WGPUTextureFormat_BGRA8Unorm,WGPUTextureFormat_BGRA8UnormSrgb,usage);
    assert(unorm && srgb && bgra);
    WGPUTextureView unorm_as_srgb = view_as(unorm,WGPUTextureFormat_RGBA8UnormSrgb);
    WGPUTextureView unorm_default = view_as(unorm,WGPUTextureFormat_Undefined);
    WGPUTextureView srgb_as_unorm = view_as(srgb,WGPUTextureFormat_RGBA8Unorm);
    WGPUTextureView bgra_as_srgb = view_as(bgra,WGPUTextureFormat_BGRA8UnormSrgb);
    assert(unorm_as_srgb && unorm_default && srgb_as_unorm && bgra_as_srgb);
    WGPUBuffer readback = buffer(&s,4 * SLOT,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUBuffer output = buffer(&s,256,WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUComputePipeline reader = pipeline(&s,
        "@group(0) @binding(0) var t:texture_2d<f32>;@group(0) @binding(1) var<storage,read_write> o:array<vec4f>;"
        "@compute @workgroup_size(1) fn main(){o[0]=textureLoad(t,vec2i(1,1),0);}");
    WGPUBindGroupEntry entries[2] = {{.binding=0,.textureView=unorm_default},{.binding=1,.buffer=output,.size=256}};
    WGPUBindGroup group_entry = group(&s,reader,entries,2);

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    draw(encoder,unorm_as_srgb,srgb_writer);   /* linear values encoded by the srgb view */
    draw(encoder,srgb_as_unorm,unorm_writer);  /* raw values stored by the unorm view */
    draw(encoder,bgra_as_srgb,bgra_writer);
    copy_out(encoder,unorm,readback,0);
    copy_out(encoder,srgb,readback,1);
    copy_out(encoder,bgra,readback,2);
    dispatch(encoder,reader,group_entry);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,output,0,readback,3 * SLOT,16);
    WGPUTextureView views[] = {unorm_as_srgb,unorm_default,srgb_as_unorm,bgra_as_srgb};
    for (size_t i = 0; i < 4; ++i) wgpuTextureViewRelease(views[i]);
    wgpuTextureRelease(unorm);
    wgpuTextureRelease(srgb);
    wgpuTextureRelease(bgra);
    wgpuRenderPipelineRelease(srgb_writer);
    wgpuRenderPipelineRelease(unorm_writer);
    wgpuRenderPipelineRelease(bgra_writer);
    wgpuComputePipelineRelease(reader);
    wgpuBindGroupRelease(group_entry);
    wgpuBufferRelease(output);
    submit(&s,encoder);

    const unsigned char *bytes = map(&s,readback,4 * SLOT);
    const unsigned char bgra_codes[4] = {codes[2],codes[1],codes[0],codes[3]};
    for (size_t y = 0; y < HEIGHT; ++y) for (size_t x = 0; x < WIDTH; ++x) {
        size_t at = y * 256 + x * 4;
        assert(!memcmp(bytes + at,codes,4));
        assert(!memcmp(bytes + SLOT + at,codes,4));
        assert(!memcmp(bytes + 2 * SLOT + at,bgra_codes,4));
    }
    /* The unorm view decodes the stored encoded bytes, not the linear values. */
    const float *loaded = (const float *)(bytes + 3 * SLOT);
    for (size_t i = 0; i < 4; ++i) assert(lrintf(loaded[i] * 255.0f) == codes[i]);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    consumer_finish(&s);
    printf("PASS public view formats: rgba8unorm rendered through its srgb view stores exact sRGB codes 64/128/200/255 (copy and unorm-view textureLoad), rgba8unorm-srgb rendered through its unorm view stores raw bytes, bgra8unorm srgb view keeps byte order, non-pair/unlisted/pipeline-mismatch controls and the srgb-storage limitation, early releases; exact 96 bytes and 4 decoded codes %s\n",
        adapter_class_scope());
}
