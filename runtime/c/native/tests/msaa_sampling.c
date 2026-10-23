/* SPDX-License-Identifier: MIT
 * Public multisampled texture reads: 4x rgba8unorm attachments rendered with
 * sample masks and alpha-to-coverage (stored, not resolved), then every sample
 * read in compute through texture_multisampled_2d<f32> textureLoad(coord, i)
 * via the automatic 'unfilterable-float' multisampled layout, plus
 * textureNumSamples and an out-of-range sample index that loads zero. Mask
 * bit i selects sample i, so per-sample values are exact; alpha 0/1 coverage
 * is all-or-nothing by WebGPU definition. */
#include "consumer.h"

enum { WIDTH = 4, HEIGHT = 2, SAMPLES = 4, TEXELS = WIDTH * HEIGHT, WORDS = TEXELS * SAMPLES * 4 + 8,
    REGION = 1024 /* bytes per reader output; a multiple of the 256-byte storage alignment */ };

static WGPURenderPipeline writer(state *s, const char *color, uint32_t mask, bool alpha_to_coverage)
{
    char code[384];
    int length = snprintf(code,sizeof(code),
        "@vertex fn vertex(@builtin(vertex_index) i:u32)->@builtin(position) vec4f{"
        "let x=f32((i<<1u)&2u)*2.0-1.0;let y=f32(i&2u)*2.0-1.0;return vec4f(x,y,0.0,1.0);}"
        "@fragment fn fragment()->@location(0) vec4f{return vec4f(%s);}",color);
    assert(length > 0 && (size_t)length < sizeof(code));
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code,WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader = {.nextInChain=&source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device,&shader);
    assert(module);
    WGPUColorTargetState target = {.format=WGPUTextureFormat_RGBA8Unorm,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=module,.targetCount=1,.targets=&target};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex = (WGPUVertexState){.module=module};
    descriptor.fragment = &fragment;
    descriptor.multisample.count = SAMPLES;
    descriptor.multisample.mask = mask;
    descriptor.multisample.alphaToCoverageEnabled = alpha_to_coverage;
    WGPURenderPipeline result = wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
    wgpuShaderModuleRelease(module);
    assert(result);
    return result;
}
static WGPUTexture multisampled(state *s, WGPUTextureUsage usage)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = WGPUTextureFormat_RGBA8Unorm;
    descriptor.usage = usage;
    descriptor.size = (WGPUExtent3D){WIDTH,HEIGHT,1};
    descriptor.sampleCount = SAMPLES;
    return wgpuDeviceCreateTexture(s->device,&descriptor);
}
/* Clear blue, then each (pipeline, scissor x range) draw stores per-sample color. */
static void render(WGPUCommandEncoder encoder, WGPUTextureView view, WGPURenderPipeline first,
    WGPURenderPipeline second)
{
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    attachment.clearValue = (WGPUColor){0,0,1,1};
    WGPURenderPassDescriptor descriptor = {.colorAttachmentCount=1,.colorAttachments=&attachment};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
    assert(pass);
    wgpuRenderPassEncoderSetScissorRect(pass,0,0,second ? 2 : WIDTH,HEIGHT);
    wgpuRenderPassEncoderSetPipeline(pass,first);
    wgpuRenderPassEncoderDraw(pass,3,1,0,0);
    if (second) {
        wgpuRenderPassEncoderSetScissorRect(pass,2,0,2,HEIGHT);
        wgpuRenderPassEncoderSetPipeline(pass,second);
        wgpuRenderPassEncoderDraw(pass,3,1,0,0);
    }
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}
static void controls(state *s)
{
    /* Multisampled float layouts are invalid; storage on 4x is not admitted. */
    WGPUBindGroupLayoutEntry entry = {.binding=0,.visibility=WGPUShaderStage_Compute,
        .texture={.sampleType=WGPUTextureSampleType_Float,.viewDimension=WGPUTextureViewDimension_2D,.multisampled=WGPU_TRUE}};
    WGPUBindGroupLayoutDescriptor descriptor = {.entryCount=1,.entries=&entry};
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateBindGroupLayout(s->device,&descriptor));
    expect_validation(s);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!multisampled(s,WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_StorageBinding));
    expect_validation(s);
    /* A single-sample view does not bind to a multisampled entry. */
    entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(s->device,&descriptor);
    assert(layout);
    WGPUTexture single = texture(s,WGPUTextureFormat_RGBA8Unorm,WGPUTextureUsage_TextureBinding,1);
    WGPUTextureView view = wgpuTextureCreateView(single,NULL);
    WGPUBindGroupEntry binding = {.binding=0,.textureView=view};
    WGPUBindGroupDescriptor group_descriptor = {.layout=layout,.entryCount=1,.entries=&binding};
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateBindGroup(s->device,&group_descriptor));
    expect_validation(s);
    wgpuTextureViewRelease(view);
    wgpuBindGroupLayoutRelease(layout);
    /* Multisampled copies: never with a buffer, only between equal sample counts
     * and only of the whole subresource. Each copy rejects; finish then fails. */
    const WGPUTextureUsage copy_usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc |
        WGPUTextureUsage_CopyDst;
    WGPUTexture first = multisampled(s,copy_usage), second = multisampled(s,copy_usage);
    WGPUTexture flat = texture(s,WGPUTextureFormat_RGBA8Unorm,WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc,1);
    assert(first && second);
    WGPUBuffer bytes = buffer(s,256 * HEIGHT,WGPUBufferUsage_CopyDst);
    for (uint32_t which = 0; which < 4; ++which) {
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
        wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
        WGPUTexelCopyTextureInfo from = {.texture=first}, to = {.texture=which == 1 ? flat : second};
        WGPUExtent3D size = {which == 2 ? 2 : (which == 1 ? 2 : WIDTH),which == 1 ? 2 : HEIGHT,1};
        if (which == 0) {
            WGPUTexelCopyBufferInfo destination = {.buffer=bytes,.layout={.bytesPerRow=256,.rowsPerImage=HEIGHT}};
            wgpuCommandEncoderCopyTextureToBuffer(encoder,&from,&destination,&size);
        } else if (which == 3) {
            WGPUTexelCopyBufferInfo source = {.buffer=bytes,.layout={.bytesPerRow=256,.rowsPerImage=HEIGHT}};
            wgpuCommandEncoderCopyBufferToTexture(encoder,&source,&to,&size);
        } else wgpuCommandEncoderCopyTextureToTexture(encoder,&from,&to,&size);
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder,NULL);
        assert(!commands);
        expect_validation(s);
        wgpuCommandEncoderRelease(encoder);
    }
    wgpuBufferRelease(bytes);
    wgpuTextureRelease(flat);
    wgpuTextureRelease(first);
    wgpuTextureRelease(second);
    wgpuTextureRelease(single);
}
int main(void)
{
    state s = {0};
    consumer_init(&s);
    WGPUBuffer output = buffer(&s,REGION * 3,WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(&s,REGION * 3,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    controls(&s);
    WGPURenderPipeline red_even = writer(&s,"1.0,0.0,0.0,1.0",0x5,false);   /* samples 0 and 2 */
    WGPURenderPipeline green_high = writer(&s,"0.0,1.0,0.0,1.0",0xE,false); /* samples 1, 2, 3 */
    WGPURenderPipeline hidden = writer(&s,"1.0,1.0,1.0,0.0",0xFFFFFFFF,true); /* alpha 0: none */
    WGPURenderPipeline shown = writer(&s,"1.0,1.0,1.0,1.0",0xFFFFFFFF,true);  /* alpha 1: all */
    const WGPUTextureUsage usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    /* copied receives masked through a whole-subresource 4x texture copy. */
    WGPUTexture masked = multisampled(&s,usage | WGPUTextureUsage_CopySrc), coverage = multisampled(&s,usage);
    WGPUTexture copied = multisampled(&s,usage | WGPUTextureUsage_CopyDst);
    assert(masked && coverage && copied);
    WGPUTextureView masked_view = wgpuTextureCreateView(masked,NULL);
    WGPUTextureView coverage_view = wgpuTextureCreateView(coverage,NULL);
    WGPUTextureView copied_view = wgpuTextureCreateView(copied,NULL);
    WGPUComputePipeline reader = pipeline(&s,
        "@group(0) @binding(0) var t:texture_multisampled_2d<f32>;"
        "@group(0) @binding(1) var<storage,read_write> o:array<vec4f>;"
        "@compute @workgroup_size(1) fn main(){for(var i=0u;i<32u;i++){"
        "o[i]=textureLoad(t,vec2i(i32((i/4u)%4u),i32(i/16u)),i32(i%4u));}"
        "o[32]=vec4f(f32(textureNumSamples(t)));o[33]=textureLoad(t,vec2i(0,0),7);}");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    render(encoder,masked_view,red_even,NULL);
    /* Masked texture: left half red_even, right half green_high. */
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = masked_view;
    attachment.loadOp = WGPULoadOp_Load;
    attachment.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor load_descriptor = {.colorAttachmentCount=1,.colorAttachments=&attachment};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&load_descriptor);
    wgpuRenderPassEncoderSetScissorRect(pass,2,0,2,HEIGHT);
    wgpuRenderPassEncoderSetPipeline(pass,green_high);
    wgpuRenderPassEncoderDraw(pass,3,1,0,0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    render(encoder,coverage_view,hidden,shown);
    wgpuCommandEncoderCopyTextureToTexture(encoder,&(WGPUTexelCopyTextureInfo){.texture=masked},
        &(WGPUTexelCopyTextureInfo){.texture=copied},&(WGPUExtent3D){WIDTH,HEIGHT,1});
    WGPUTextureView views[3] = {masked_view,coverage_view,copied_view};
    for (uint32_t i = 0; i < 3; ++i) {
        WGPUBindGroupEntry entries[2] = {{.binding=0,.textureView=views[i]},
            {.binding=1,.buffer=output,.offset=i * REGION,.size=WORDS * 4}};
        WGPUBindGroup bound = group(&s,reader,entries,2);
        WGPUComputePassEncoder compute = wgpuCommandEncoderBeginComputePass(encoder,NULL);
        wgpuComputePassEncoderSetPipeline(compute,reader);
        wgpuComputePassEncoderSetBindGroup(compute,0,bound,0,NULL);
        wgpuComputePassEncoderDispatchWorkgroups(compute,1,1,1);
        wgpuComputePassEncoderEnd(compute);
        wgpuComputePassEncoderRelease(compute);
        wgpuBindGroupRelease(bound);
    }
    /* A multisampled texture never takes a sampler, so a filtering sampler
     * paired with a 'float' texture in the same pipeline is valid: the
     * multisampled 'unfilterable-float' binding is outside the pair check. */
    WGPUComputePipeline mixed = pipeline(&s,
        "@group(0) @binding(0) var m:texture_multisampled_2d<f32>;"
        "@group(0) @binding(1) var t:texture_2d<f32>;"
        "@group(0) @binding(2) var sm:sampler;"
        "@group(0) @binding(3) var<storage,read_write> o:array<vec4f>;"
        "@compute @workgroup_size(1) fn main(){o[0]=textureLoad(m,vec2i(0,0),0);"
        "o[1]=textureSampleLevel(t,sm,vec2f(0.5,0.5),0.0);}");
    WGPUTextureDescriptor half_desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    half_desc.format = WGPUTextureFormat_RGBA16Float;
    half_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    half_desc.size = (WGPUExtent3D){2,1,1};
    WGPUTexture half = wgpuDeviceCreateTexture(s.device,&half_desc);
    assert(half);
    const uint16_t half_texels[8] = {0,0,0,0x3C00, 0x4000,0x4400,0,0x3C00}; /* (0,0,0,1), (2,4,0,1) */
    wgpuQueueWriteTexture(s.queue,&(WGPUTexelCopyTextureInfo){.texture=half,.aspect=WGPUTextureAspect_All},
        half_texels,sizeof(half_texels),&(WGPUTexelCopyBufferLayout){.bytesPerRow=16,.rowsPerImage=1},
        &(WGPUExtent3D){2,1,1});
    WGPUTextureView half_view = wgpuTextureCreateView(half,NULL);
    WGPUSamplerDescriptor linear = WGPU_SAMPLER_DESCRIPTOR_INIT;
    linear.magFilter = linear.minFilter = WGPUFilterMode_Linear;
    WGPUSampler sampler = wgpuDeviceCreateSampler(s.device,&linear);
    WGPUBuffer mixed_output = buffer(&s,32,WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUBuffer mixed_readback = buffer(&s,32,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUBindGroupEntry mixed_entries[4] = {{.binding=0,.textureView=masked_view},{.binding=1,.textureView=half_view},
        {.binding=2,.sampler=sampler},{.binding=3,.buffer=mixed_output,.size=32}};
    WGPUBindGroup mixed_group = group(&s,mixed,mixed_entries,4);
    WGPUComputePassEncoder mixed_pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderSetPipeline(mixed_pass,mixed);
    wgpuComputePassEncoderSetBindGroup(mixed_pass,0,mixed_group,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroups(mixed_pass,1,1,1);
    wgpuComputePassEncoderEnd(mixed_pass);
    wgpuComputePassEncoderRelease(mixed_pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,mixed_output,0,mixed_readback,0,32);
    wgpuBindGroupRelease(mixed_group);
    wgpuBufferRelease(mixed_output);
    wgpuSamplerRelease(sampler);
    wgpuTextureViewRelease(half_view);
    wgpuTextureRelease(half);
    wgpuComputePipelineRelease(mixed);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,output,0,readback,0,REGION * 3);
    wgpuTextureViewRelease(masked_view);
    wgpuTextureViewRelease(coverage_view);
    wgpuTextureViewRelease(copied_view);
    wgpuTextureRelease(masked);
    wgpuTextureRelease(coverage);
    wgpuTextureRelease(copied);
    WGPURenderPipeline pipelines[] = {red_even,green_high,hidden,shown};
    for (size_t i = 0; i < 4; ++i) wgpuRenderPipelineRelease(pipelines[i]);
    wgpuComputePipelineRelease(reader);
    wgpuBufferRelease(output);
    submit(&s,encoder);
    const float *values = map(&s,readback,REGION * 3);
    const float blue[4] = {0,0,1,1}, red[4] = {1,0,0,1}, green[4] = {0,1,0,1}, white[4] = {1,1,1,1}, zero[4] = {0,0,0,0};
    for (uint32_t texel = 0; texel < TEXELS; ++texel) for (uint32_t sample = 0; sample < SAMPLES; ++sample) {
        bool left = texel % WIDTH < 2;
        const float *masked_expected = left ? (sample % 2 == 0 ? red : blue) : (sample == 0 ? red : green);
        const float *coverage_expected = left ? blue : white;
        uint32_t at = (texel * SAMPLES + sample) * 4;
        assert(!memcmp(values + at,masked_expected,16));
        assert(!memcmp(values + REGION / 4 + at,coverage_expected,16));
        assert(!memcmp(values + REGION / 2 + at,masked_expected,16));
    }
    for (uint32_t i = 0; i < 3; ++i) {
        assert(values[i * REGION / 4 + 128] == 4.0f);              /* textureNumSamples */
        assert(!memcmp(values + i * REGION / 4 + 132,zero,16));    /* sample 7: zero */
    }
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    const float *mixed_values = map(&s,mixed_readback,32);
    const float mixed_expected[8] = {1,0,0,1, 1,2,0,1}; /* red sample 0; linear midpoint */
    assert(!memcmp(mixed_values,mixed_expected,sizeof(mixed_expected)));
    wgpuBufferUnmap(mixed_readback);
    wgpuBufferRelease(mixed_readback);
    consumer_finish(&s);
    printf("PASS public multisampled reads: 4x rgba8unorm stored with sample masks 0x5/0xE and alpha-to-coverage 0/1, every sample of 8 texels read via texture_multisampled_2d textureLoad under the automatic unfilterable-float multisampled layout, textureNumSamples, out-of-range sample zero, a whole 4x texture copy read back per sample, a multisampled read beside a filtering-sampler linear sample in one pipeline, float-layout/storage/single-sample-binding and 4x buffer/count-mismatch/partial copy controls, early releases; exact 96 samples %s\n",
        adapter_class_scope());
}
