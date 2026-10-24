/* SPDX-License-Identifier: MIT
 * Public texture-component-swizzle: sampled views with (b,a,0,r) and (1,g,r,g)
 * swizzles read through textureLoad and a nearest textureSampleLevel return the
 * identity view's components rearranged exactly, with constant 0/1 sources. A
 * device without the feature accepts only identity swizzles; with it, an
 * unknown source value rejects. A swizzled view may carry RenderAttachment or
 * StorageBinding usage; using it as a color attachment or a storage binding is
 * the validation error (WebGPU checks the swizzle where the view is used). */
#include "consumer.h"
#include <stdlib.h>

static WGPUTextureView swizzled_view(WGPUTexture texture, WGPUTextureUsage usage, WGPUComponentSwizzle r,
    WGPUComponentSwizzle g, WGPUComponentSwizzle b, WGPUComponentSwizzle a)
{
    WGPUTextureComponentSwizzleDescriptor chain = {.chain={.sType=WGPUSType_TextureComponentSwizzleDescriptor},
        .swizzle={r,g,b,a}};
    WGPUTextureViewDescriptor descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    descriptor.nextInChain = &chain.chain;
    descriptor.usage = usage;
    return wgpuTextureCreateView(texture,&descriptor);
}
static WGPUTexture source(state *s, WGPUTextureUsage usage)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = WGPUTextureFormat_RGBA8Unorm;
    descriptor.usage = usage;
    descriptor.size = (WGPUExtent3D){2,1,1};
    WGPUTexture texture = wgpuDeviceCreateTexture(s->device,&descriptor);
    assert(texture);
    return texture;
}
static void rejected(state *s, WGPUTexture texture, WGPUTextureUsage usage, WGPUComponentSwizzle r)
{
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!swizzled_view(texture,usage,r,WGPUComponentSwizzle_A,WGPUComponentSwizzle_Zero,WGPUComponentSwizzle_R));
    expect_validation(s);
}
/* A swizzled view with RenderAttachment in its (default) usage is created; a
 * render pass that names it as a color attachment fails at finish(). */
static void attachment_rejected(state *s, WGPUTexture texture)
{
    WGPUTextureView view = swizzled_view(texture,0,WGPUComponentSwizzle_B,WGPUComponentSwizzle_A,
        WGPUComponentSwizzle_Zero,WGPUComponentSwizzle_R);
    assert(view);
    WGPURenderPassColorAttachment color = {.view=view,.depthSlice=WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp=WGPULoadOp_Clear,.storeOp=WGPUStoreOp_Store};
    WGPURenderPassDescriptor descriptor = {.colorAttachmentCount=1,.colorAttachments=&color};
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
    if (pass) {
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder,NULL);
    if (commands) wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    expect_validation(s);
    wgpuTextureViewRelease(view);
}
/* A swizzled view of a storage texture is created; binding it as a storage
 * texture is the validation error. */
static void storage_rejected(state *s, WGPUTexture texture)
{
    WGPUTextureView view = swizzled_view(texture,0,WGPUComponentSwizzle_B,WGPUComponentSwizzle_A,
        WGPUComponentSwizzle_Zero,WGPUComponentSwizzle_R);
    assert(view);
    WGPUBindGroupLayoutEntry entry = {.binding=0,.visibility=WGPUShaderStage_Compute,
        .storageTexture={.access=WGPUStorageTextureAccess_WriteOnly,.format=WGPUTextureFormat_RGBA8Unorm,
            .viewDimension=WGPUTextureViewDimension_2D}};
    WGPUBindGroupLayoutDescriptor layout_descriptor = {.entryCount=1,.entries=&entry};
    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(s->device,&layout_descriptor);
    assert(layout);
    WGPUBindGroupEntry bound = {.binding=0,.textureView=view};
    WGPUBindGroupDescriptor descriptor = {.layout=layout,.entryCount=1,.entries=&bound};
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(s->device,&descriptor);
    expect_validation(s);
    if (group) wgpuBindGroupRelease(group);
    wgpuBindGroupLayoutRelease(layout);
    wgpuTextureViewRelease(view);
}
static const WGPUTextureUsage sampled_usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst |
    WGPUTextureUsage_RenderAttachment;

/* Without the feature only identity swizzles (Undefined or each channel's own) pass. */
static void disabled(state *s)
{
    WGPUTexture texture = source(s,sampled_usage);
    WGPUTextureView identity = swizzled_view(texture,0,WGPUComponentSwizzle_R,WGPUComponentSwizzle_G,
        WGPUComponentSwizzle_B,WGPUComponentSwizzle_A);
    WGPUTextureView undefined = swizzled_view(texture,0,WGPUComponentSwizzle_Undefined,WGPUComponentSwizzle_Undefined,
        WGPUComponentSwizzle_Undefined,WGPUComponentSwizzle_Undefined);
    assert(identity && undefined);
    rejected(s,texture,WGPUTextureUsage_TextureBinding,WGPUComponentSwizzle_B);
    wgpuTextureViewRelease(identity);
    wgpuTextureViewRelease(undefined);
    wgpuTextureRelease(texture);
}

static void swizzled(state *s)
{
    WGPUTexture texture = source(s,sampled_usage);
    const uint8_t texels[8] = {0x00,0x33,0x66,0xFF, 0x11,0x22,0x44,0x88};
    WGPUTexelCopyTextureInfo destination = {.texture=texture,.aspect=WGPUTextureAspect_All};
    WGPUTexelCopyBufferLayout layout = {.bytesPerRow=8,.rowsPerImage=1};
    wgpuQueueWriteTexture(s->queue,&destination,texels,sizeof(texels),&layout,&(WGPUExtent3D){2,1,1});
    /* An unknown source rejects; RenderAttachment or StorageBinding in the view
     * usage does not, but the swizzled view is rejected where it is used. */
    rejected(s,texture,WGPUTextureUsage_TextureBinding,(WGPUComponentSwizzle)7);
    attachment_rejected(s,texture);
    WGPUTexture storage = source(s,WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding);
    storage_rejected(s,storage);
    wgpuTextureRelease(storage);

    WGPUTextureView identity = wgpuTextureCreateView(texture,NULL);
    WGPUTextureView first = swizzled_view(texture,WGPUTextureUsage_TextureBinding,WGPUComponentSwizzle_B,
        WGPUComponentSwizzle_A,WGPUComponentSwizzle_Zero,WGPUComponentSwizzle_R);
    WGPUTextureView second = swizzled_view(texture,WGPUTextureUsage_TextureBinding,WGPUComponentSwizzle_One,
        WGPUComponentSwizzle_Undefined,WGPUComponentSwizzle_R,WGPUComponentSwizzle_G);
    assert(identity && first && second);
    WGPUSamplerDescriptor nearest = WGPU_SAMPLER_DESCRIPTOR_INIT;
    WGPUSampler sampler = wgpuDeviceCreateSampler(s->device,&nearest);
    WGPUComputePipeline reader = pipeline(s,
        "@group(0) @binding(0) var i:texture_2d<f32>;"
        "@group(0) @binding(1) var f:texture_2d<f32>;"
        "@group(0) @binding(2) var t:texture_2d<f32>;"
        "@group(0) @binding(3) var p:sampler;"
        "@group(0) @binding(4) var<storage,read_write> o:array<vec4f,8>;"
        "@compute @workgroup_size(1) fn main(){for(var x=0;x<2;x++){"
        "o[x]=textureLoad(i,vec2i(x,0),0);o[2+x]=textureLoad(f,vec2i(x,0),0);o[4+x]=textureLoad(t,vec2i(x,0),0);"
        "o[6+x]=textureSampleLevel(f,p,vec2f(0.25+0.5*f32(x),0.5),0.0);}}");
    WGPUBuffer output = buffer(s,128,WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    float sentinel[32];
    for (size_t i = 0; i < 32; ++i) sentinel[i] = -1.0f;
    wgpuQueueWriteBuffer(s->queue,output,0,sentinel,sizeof(sentinel));
    WGPUBuffer readback = buffer(s,128,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUBindGroupEntry entries[5] = {{.binding=0,.textureView=identity},{.binding=1,.textureView=first},
        {.binding=2,.textureView=second},{.binding=3,.sampler=sampler},{.binding=4,.buffer=output,.size=128}};
    WGPUBindGroup bound = group(s,reader,entries,5);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderSetPipeline(pass,reader);
    wgpuComputePassEncoderSetBindGroup(pass,0,bound,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,output,0,readback,0,128);
    submit(s,encoder);
    const float *v = map(s,readback,128);
    for (size_t x = 0; x < 2; ++x) {
        const float *id = v + x * 4;
        assert(id[0] >= 0.0f && (x == 1 || id[3] == 1.0f)); /* the identity read really ran */
        const float first_expected[4] = {id[2],id[3],0.0f,id[0]};
        const float second_expected[4] = {1.0f,id[1],id[0],id[1]};
        assert(!memcmp(v + 8 + x * 4,first_expected,16));
        assert(!memcmp(v + 16 + x * 4,second_expected,16));
        assert(!memcmp(v + 24 + x * 4,first_expected,16)); /* sampled path */
    }
    wgpuBufferUnmap(readback);
    wgpuBindGroupRelease(bound);
    wgpuBufferRelease(readback);
    wgpuBufferRelease(output);
    wgpuComputePipelineRelease(reader);
    wgpuSamplerRelease(sampler);
    wgpuTextureViewRelease(second);
    wgpuTextureViewRelease(first);
    wgpuTextureViewRelease(identity);
    wgpuTextureRelease(texture);
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    bool offered = wgpuAdapterHasFeature(s.adapter,WGPUFeatureName_TextureComponentSwizzle);
    assert(!wgpuDeviceHasFeature(s.device,WGPUFeatureName_TextureComponentSwizzle));
    disabled(&s);
    consumer_finish(&s);
    if (!offered) {
        printf("SKIP texture-component-swizzle: not offered by this adapter %s\n",adapter_class_scope());
        return 0;
    }
    state enabled = {0};
    const WGPUFeatureName feature = WGPUFeatureName_TextureComponentSwizzle;
    consumer_init_features(&enabled,&feature,1);
    assert(wgpuDeviceHasFeature(enabled.device,feature));
    swizzled(&enabled);
    consumer_finish(&enabled);
    printf("PASS public component swizzle: (b,a,0,r) and (1,g,r,g) sampled views return the identity view's "
        "rgba8unorm components rearranged exactly through textureLoad and nearest textureSampleLevel; identity "
        "swizzles need no feature, a non-identity swizzle without the feature or with an unknown source "
        "rejects, and a swizzled view used as a color attachment or storage binding rejects %s\n", adapter_class_scope());
    return 0;
}
