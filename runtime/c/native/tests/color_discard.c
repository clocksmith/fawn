/* SPDX-License-Identifier: MIT
 * Public single-sample attachment storeOp 'discard': the backend's per-aspect
 * owner marks the subresource undefined, and later consumers see real GPU zero
 * clears: a direct copy, a Load pass, a compute textureLoad binding and a
 * depth32float copy. A Store control keeps its data. No CPU fabrication. */
#include "consumer.h"

enum { WIDTH = 4, HEIGHT = 2, SLOT = 512 };

static WGPUTexture image(state *s, WGPUTextureFormat format, WGPUTextureUsage usage)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = format;
    descriptor.usage = usage;
    descriptor.size = (WGPUExtent3D){WIDTH,HEIGHT,1};
    WGPUTexture texture = wgpuDeviceCreateTexture(s->device,&descriptor);
    assert(texture);
    return texture;
}
/* One empty pass: color clear/load with the given store, no draws. */
static void color_pass(WGPUCommandEncoder encoder, WGPUTexture texture, WGPULoadOp load, WGPUStoreOp store,
    WGPUColor clear)
{
    WGPUTextureView view = wgpuTextureCreateView(texture,NULL);
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.loadOp = load;
    attachment.storeOp = store;
    attachment.clearValue = clear;
    WGPURenderPassDescriptor descriptor = {.colorAttachmentCount=1,.colorAttachments=&attachment};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
    assert(pass);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuTextureViewRelease(view);
}
static void copy_out(WGPUCommandEncoder encoder, WGPUTexture texture, WGPUTextureAspect aspect,
    WGPUBuffer readback, uint32_t slot)
{
    WGPUTexelCopyTextureInfo source = {.texture=texture,.aspect=aspect};
    WGPUTexelCopyBufferInfo destination = {.buffer=readback,.layout={.offset=slot * SLOT,.bytesPerRow=256,.rowsPerImage=HEIGHT}};
    WGPUExtent3D size = {WIDTH,HEIGHT,1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder,&source,&destination,&size);
}
int main(void)
{
    state s = {0};
    consumer_init(&s);
    const WGPUTextureUsage usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc |
        WGPUTextureUsage_TextureBinding;
    WGPUTexture copied = image(&s,WGPUTextureFormat_RGBA8Unorm,usage);
    WGPUTexture stored = image(&s,WGPUTextureFormat_RGBA8Unorm,usage);
    WGPUTexture loaded = image(&s,WGPUTextureFormat_RGBA8Unorm,usage);
    WGPUTexture sampled = image(&s,WGPUTextureFormat_RGBA8Unorm,usage);
    WGPUTexture depth = image(&s,WGPUTextureFormat_Depth32Float,WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc);
    WGPUBuffer readback = buffer(&s,5 * SLOT,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUBuffer output = buffer(&s,256,WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    /* Pre-filled with -1.0: only the dispatch's own zero loads can clear it. */
    float sentinel[64];
    for (size_t i = 0; i < 64; ++i) sentinel[i] = -1.0f;
    wgpuQueueWriteBuffer(s.queue,output,0,sentinel,sizeof(sentinel));
    WGPUComputePipeline reader = pipeline(&s,
        "@group(0) @binding(0) var t:texture_2d<f32>;@group(0) @binding(1) var<storage,read_write> o:array<vec4f>;"
        "@compute @workgroup_size(1) fn main(){for(var i=0u;i<8u;i++){o[i]=textureLoad(t,vec2i(i32(i%4u),i32(i/4u)),0);}}");
    WGPUTextureView sampled_view = wgpuTextureCreateView(sampled,NULL);
    WGPUBindGroupEntry entries[2] = {{.binding=0,.textureView=sampled_view},{.binding=1,.buffer=output,.size=256}};
    WGPUBindGroup group_entry = group(&s,reader,entries,2);
    const WGPUColor red = {1,0,0,1}, green = {0,1,0,1};

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    color_pass(encoder,copied,WGPULoadOp_Clear,WGPUStoreOp_Discard,red);
    copy_out(encoder,copied,WGPUTextureAspect_All,readback,0);           /* zeros */
    color_pass(encoder,stored,WGPULoadOp_Clear,WGPUStoreOp_Store,green);
    copy_out(encoder,stored,WGPUTextureAspect_All,readback,1);           /* green */
    color_pass(encoder,loaded,WGPULoadOp_Clear,WGPUStoreOp_Discard,red);
    color_pass(encoder,loaded,WGPULoadOp_Load,WGPUStoreOp_Store,red);    /* Load sees zeros */
    copy_out(encoder,loaded,WGPUTextureAspect_All,readback,2);
    color_pass(encoder,sampled,WGPULoadOp_Clear,WGPUStoreOp_Discard,red);
    dispatch(encoder,reader,group_entry);                                /* binding sees zeros */
    wgpuCommandEncoderCopyBufferToBuffer(encoder,output,0,readback,3 * SLOT,128);
    WGPUTextureView depth_view = wgpuTextureCreateView(depth,NULL);
    WGPURenderPassDepthStencilAttachment depth_attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    depth_attachment.view = depth_view;
    depth_attachment.depthLoadOp = WGPULoadOp_Clear;
    depth_attachment.depthStoreOp = WGPUStoreOp_Discard;
    depth_attachment.depthClearValue = 0.5f;
    WGPURenderPassDescriptor depth_pass = {.depthStencilAttachment=&depth_attachment};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&depth_pass);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuTextureViewRelease(depth_view);
    copy_out(encoder,depth,WGPUTextureAspect_DepthOnly,readback,4);       /* 0.0, not 0.5 */
    WGPUTexture textures[] = {copied,stored,loaded,sampled,depth};
    for (size_t i = 0; i < 5; ++i) wgpuTextureRelease(textures[i]);
    wgpuTextureViewRelease(sampled_view);
    wgpuBindGroupRelease(group_entry);
    wgpuComputePipelineRelease(reader);
    wgpuBufferRelease(output);
    submit(&s,encoder);

    const unsigned char *bytes = map(&s,readback,5 * SLOT);
    const unsigned char zero[4] = {0,0,0,0}, green_bytes[4] = {0,255,0,255};
    for (size_t y = 0; y < HEIGHT; ++y) for (size_t x = 0; x < WIDTH; ++x) {
        size_t at = y * 256 + x * 4;
        assert(!memcmp(bytes + at,zero,4));
        assert(!memcmp(bytes + SLOT + at,green_bytes,4));
        assert(!memcmp(bytes + 2 * SLOT + at,zero,4));
        float depth_value;
        memcpy(&depth_value,bytes + 4 * SLOT + at,4);
        assert(depth_value == 0.0f);
    }
    const float *loaded_texels = (const float *)(bytes + 3 * SLOT);
    for (size_t i = 0; i < 32; ++i) assert(loaded_texels[i] == 0.0f);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    consumer_finish(&s);
    printf("PASS public attachment discard: single-sample color discard then direct copy, Load pass and compute textureLoad all read GPU zeros, Store control keeps green, discarded depth32float copies as 0.0, early releases; exact 40 texels and 32 loaded components %s\n",
        adapter_class_scope());
}
