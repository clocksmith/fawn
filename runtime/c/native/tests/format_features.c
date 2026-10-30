/* SPDX-License-Identifier: MIT
 * Public optional format features: float32-filterable (linear sampling of
 * r32float/rgba32float under the automatic 'float' layout), float32-blendable
 * (additive blending into r32float) and depth32float-stencil8 (rendered depth
 * and stencil read back per aspect) and rg11b10ufloat-renderable (blended and
 * 4x-resolved rg11b10ufloat targets read back as packed bits; without it the
 * core format still samples exactly). Each is reported only from the device's
 * native format features; a device that did not enable it rejects every use. */
#include "consumer.h"
#include <stdlib.h>

enum { SIZE = 4, PITCH = 256 };

#define FULL_SCREEN "@vertex fn vertex(@builtin(vertex_index) i:u32)->@builtin(position) vec4f{" \
    "var p=array<vec2f,3>(vec2f(-1.0,-1.0),vec2f(3.0,-1.0),vec2f(-1.0,3.0));" \
    "return vec4f(p[i],0.25,1.0);}"
static const char *const sampling_code =
    "@group(0) @binding(0) var r:texture_2d<f32>;"
    "@group(0) @binding(1) var c:texture_2d<f32>;"
    "@group(0) @binding(2) var s:sampler;"
    "@group(0) @binding(3) var<storage,read_write> out:array<vec4f,4>;"
    "@compute @workgroup_size(1) fn main(){"
    "out[0]=textureSampleLevel(r,s,vec2f(0.5,0.5),0.0);"
    "out[1]=textureSampleLevel(r,s,vec2f(0.25,0.5),0.0);"
    "out[2]=textureSampleLevel(c,s,vec2f(0.5,0.5),0.0);"
    "out[3]=textureSampleLevel(c,s,vec2f(0.75,0.5),0.0);}";

static WGPUShaderModule module(state *s, const char *code)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code,WGPU_STRLEN};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain=&source.chain};
    WGPUShaderModule result = wgpuDeviceCreateShaderModule(s->device,&descriptor);
    assert(result);
    return result;
}
static WGPUTexture image(state *s, WGPUTextureFormat format, WGPUTextureUsage usage, uint32_t width, uint32_t height)
{
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = format;
    descriptor.usage = usage;
    descriptor.size = (WGPUExtent3D){width,height,1};
    return wgpuDeviceCreateTexture(s->device,&descriptor);
}
static void upload(state *s, WGPUTexture texture, const void *data, uint32_t row, uint32_t width)
{
    WGPUTexelCopyTextureInfo destination = {.texture=texture,.aspect=WGPUTextureAspect_All};
    WGPUTexelCopyBufferLayout layout = {.bytesPerRow=row,.rowsPerImage=1};
    wgpuQueueWriteTexture(s->queue,&destination,data,row,&layout,&(WGPUExtent3D){width,1,1});
}
static WGPURenderPipeline color_pipeline(state *s, WGPUShaderModule shader, WGPUTextureFormat format,
    const WGPUBlendState *blend)
{
    WGPUColorTargetState target = {.format=format,.blend=blend,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=shader,.entryPoint={"fragment",WGPU_STRLEN},.targetCount=1,.targets=&target};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex = (WGPUVertexState){.module=shader,.entryPoint={"vertex",WGPU_STRLEN}};
    descriptor.fragment = &fragment;
    return wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
}
static WGPURenderPipeline depth_pipeline(state *s, WGPUShaderModule shader, WGPUTextureFormat format)
{
    WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
    depth.format = format;
    depth.depthWriteEnabled = WGPUOptionalBool_True;
    depth.depthCompare = WGPUCompareFunction_Always;
    depth.stencilFront.compare = depth.stencilBack.compare = WGPUCompareFunction_Always;
    depth.stencilFront.passOp = depth.stencilBack.passOp = WGPUStencilOperation_Replace;
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex = (WGPUVertexState){.module=shader,.entryPoint={"vertex",WGPU_STRLEN}};
    descriptor.depthStencil = &depth;
    return wgpuDeviceCreateRenderPipeline(s->device,&descriptor);
}
static WGPUBindGroup sampling_group(state *s, WGPUComputePipeline pipeline, WGPUTextureView r,
    WGPUTextureView c, WGPUSampler sampler, WGPUBuffer out)
{
    WGPUBindGroupEntry entries[4] = {{.binding=0,.textureView=r},{.binding=1,.textureView=c},
        {.binding=2,.sampler=sampler},{.binding=3,.buffer=out,.size=64}};
    WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(pipeline,0);
    assert(layout);
    WGPUBindGroupDescriptor descriptor = {.layout=layout,.entryCount=4,.entries=entries};
    WGPUBindGroup result = wgpuDeviceCreateBindGroup(s->device,&descriptor);
    wgpuBindGroupLayoutRelease(layout);
    return result;
}
static WGPUSampler linear_sampler(state *s)
{
    WGPUSamplerDescriptor descriptor = WGPU_SAMPLER_DESCRIPTOR_INIT;
    descriptor.magFilter = descriptor.minFilter = WGPUFilterMode_Linear;
    WGPUSampler result = wgpuDeviceCreateSampler(s->device,&descriptor);
    assert(result);
    return result;
}
static WGPUTextureView view_of(WGPUTexture texture)
{
    WGPUTextureView view = wgpuTextureCreateView(texture,NULL);
    assert(view);
    return view;
}
/* rg11b10ufloat bits: R/G 5-bit exponent and 6-bit mantissa, B 5-bit mantissa, bias 15. */
static uint32_t packed_rg11b10(uint32_t r, uint32_t g, uint32_t b) { return r | g << 11 | b << 22; }
static const char *const load_code =
    "@group(0) @binding(0) var t:texture_2d<f32>;"
    "@group(0) @binding(1) var<storage,read_write> o:array<vec4f,2>;"
    "@compute @workgroup_size(1) fn main(){o[0]=textureLoad(t,vec2i(0,0),0);o[1]=textureLoad(t,vec2i(1,0),0);}";
/* Core rg11b10ufloat sampling: two packed texels load as exact floats. */
static void rg11b10_sampled(state *s)
{
    WGPUTexture texture = image(s,WGPUTextureFormat_RG11B10Ufloat,WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,2,1);
    assert(texture);
    const uint32_t texels[2] = {packed_rg11b10(976,960,516),packed_rg11b10(960,896,416)}; /* (1.25,1,2.25), (1,.5,.25) */
    upload(s,texture,texels,sizeof(texels),2);
    WGPUComputePipeline reader = pipeline(s,load_code);
    WGPUTextureView view = view_of(texture);
    WGPUBuffer out = buffer(s,32,WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(s,32,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUBindGroupEntry entries[2] = {{.binding=0,.textureView=view},{.binding=1,.buffer=out,.size=32}};
    WGPUBindGroup bound = group(s,reader,entries,2);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderSetPipeline(pass,reader);
    wgpuComputePassEncoderSetBindGroup(pass,0,bound,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,out,0,readback,0,32);
    submit(s,encoder);
    const float expected[8] = {1.25f,1.0f,2.25f,1.0f, 1.0f,0.5f,0.25f,1.0f};
    assert(!memcmp(map(s,readback,32),expected,sizeof(expected)));
    wgpuBufferUnmap(readback);
    wgpuBindGroupRelease(bound);
    wgpuBufferRelease(readback);
    wgpuBufferRelease(out);
    wgpuTextureViewRelease(view);
    wgpuComputePipelineRelease(reader);
    wgpuTextureRelease(texture);
}
static void expect_packed(state *s, WGPUTexture texture, uint32_t expected, const char *name)
{
    WGPUBuffer readback = buffer(s,PITCH * SIZE,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    texture_to_buffer(encoder,texture,readback,0,SIZE,SIZE);
    submit(s,encoder);
    const unsigned char *bytes = map(s,readback,PITCH * SIZE);
    for (uint32_t y = 0; y < SIZE; ++y) for (uint32_t x = 0; x < SIZE; ++x) {
        uint32_t value;
        memcpy(&value,bytes + y * PITCH + x * 4,4);
        if (value != expected) {
            fprintf(stderr,"%s texel (%u,%u): %08x != %08x\n",name,x,y,value,expected);
            abort();
        }
    }
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}
/* rg11b10ufloat-renderable: an additive draw of (1,.5,.25) onto a (.25,.5,2)
 * clear stores (1.25,1,2.25); a 4x target resolves the same draw to (1,.5,.25). */
static void rg11b10_rendered(state *s)
{
    WGPUShaderModule shader = module(s,FULL_SCREEN "@fragment fn fragment()->@location(0) vec4f{return vec4f(1.0,0.5,0.25,1.0);}");
    const WGPUBlendComponent add = {.operation=WGPUBlendOperation_Add,.srcFactor=WGPUBlendFactor_One,.dstFactor=WGPUBlendFactor_One};
    const WGPUBlendState blend = {.color=add,.alpha=add};
    WGPURenderPipeline blended_pipeline = color_pipeline(s,shader,WGPUTextureFormat_RG11B10Ufloat,&blend);
    WGPUColorTargetState target_state = {.format=WGPUTextureFormat_RG11B10Ufloat,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=shader,.entryPoint={"fragment",WGPU_STRLEN},.targetCount=1,.targets=&target_state};
    WGPURenderPipelineDescriptor msaa_desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    msaa_desc.vertex = (WGPUVertexState){.module=shader,.entryPoint={"vertex",WGPU_STRLEN}};
    msaa_desc.fragment = &fragment;
    msaa_desc.multisample.count = 4;
    WGPURenderPipeline msaa_pipeline = wgpuDeviceCreateRenderPipeline(s->device,&msaa_desc);
    const WGPUTextureUsage target_usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    WGPUTexture blended_target = image(s,WGPUTextureFormat_RG11B10Ufloat,target_usage,SIZE,SIZE);
    WGPUTexture resolved = image(s,WGPUTextureFormat_RG11B10Ufloat,target_usage,SIZE,SIZE);
    WGPUTextureDescriptor msaa_texture = WGPU_TEXTURE_DESCRIPTOR_INIT;
    msaa_texture.format = WGPUTextureFormat_RG11B10Ufloat;
    msaa_texture.usage = WGPUTextureUsage_RenderAttachment;
    msaa_texture.size = (WGPUExtent3D){SIZE,SIZE,1};
    msaa_texture.sampleCount = 4;
    WGPUTexture multisampled = wgpuDeviceCreateTexture(s->device,&msaa_texture);
    assert(blended_pipeline && msaa_pipeline && blended_target && resolved && multisampled);
    WGPUTextureView views[3] = {view_of(blended_target),view_of(multisampled),view_of(resolved)};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    for (uint32_t i = 0; i < 2; ++i) {
        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view = views[i];
        color.resolveTarget = i ? views[2] : NULL;
        color.loadOp = WGPULoadOp_Clear;
        color.storeOp = i ? WGPUStoreOp_Discard : WGPUStoreOp_Store;
        color.clearValue = i ? (WGPUColor){0,0,0,0} : (WGPUColor){0.25,0.5,2.0,0.0};
        WGPURenderPassDescriptor descriptor = {.colorAttachmentCount=1,.colorAttachments=&color};
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
        wgpuRenderPassEncoderSetPipeline(pass,i ? msaa_pipeline : blended_pipeline);
        wgpuRenderPassEncoderDraw(pass,3,1,0,0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }
    submit(s,encoder);
    expect_packed(s,blended_target,packed_rg11b10(976,960,516),"rg11b10 blended");
    expect_packed(s,resolved,packed_rg11b10(960,896,416),"rg11b10 resolved");
    for (size_t i = 0; i < 3; ++i) wgpuTextureViewRelease(views[i]);
    wgpuTextureRelease(multisampled);
    wgpuTextureRelease(resolved);
    wgpuTextureRelease(blended_target);
    wgpuRenderPipelineRelease(msaa_pipeline);
    wgpuRenderPipelineRelease(blended_pipeline);
    wgpuShaderModuleRelease(shader);
    rg11b10_sampled(s);
}

static void request_failed(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message,
    void *userdata, void *unused)
{
    (void)message; (void)unused;
    assert(status == WGPURequestDeviceStatus_Error && device == NULL);
    ++*(unsigned *)userdata;
}

/* Every gated use on a device created without any optional feature. */
static void disabled_controls(state *s)
{
    WGPUSupportedFeatures listed = WGPU_SUPPORTED_FEATURES_INIT;
    wgpuDeviceGetFeatures(s->device,&listed);
    /* A core device lists only core-features-and-limits, enabled automatically. */
    assert(listed.featureCount == 1 && listed.features[0] == WGPUFeatureName_CoreFeaturesAndLimits);
    assert(wgpuDeviceHasFeature(s->device,WGPUFeatureName_CoreFeaturesAndLimits));
    wgpuSupportedFeaturesFreeMembers(listed);
    assert(!wgpuDeviceHasFeature(s->device,WGPUFeatureName_Float32Filterable) &&
        !wgpuDeviceHasFeature(s->device,WGPUFeatureName_Float32Blendable) &&
        !wgpuDeviceHasFeature(s->device,WGPUFeatureName_Depth32FloatStencil8) &&
        !wgpuDeviceHasFeature(s->device,WGPUFeatureName_RG11B10UfloatRenderable));
    /* An unoffered feature fails the request instead of creating a device. The
     * offered set is the adapter's (ASTC, for one, is offered on ANV), so take
     * the first standard optional feature this adapter does not offer. */
    WGPUFeatureName unoffered = WGPUFeatureName_Force32;
    for (uint32_t value = WGPUFeatureName_DepthClipControl; value <= WGPUFeatureName_SubgroupSizeControl; ++value)
        if (!wgpuAdapterHasFeature(s->adapter,(WGPUFeatureName)value)) { unoffered = (WGPUFeatureName)value; break; }
    assert(unoffered != WGPUFeatureName_Force32);
    unsigned failed = 0;
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.requiredFeatureCount = 1;
    descriptor.requiredFeatures = &unoffered;
    WGPURequestDeviceCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,.callback=request_failed,.userdata1=&failed};
    assert(wgpuAdapterRequestDevice(s->adapter,&descriptor,callback).id && failed == 1);
    printf("unoffered feature 0x%x refused\n",(unsigned)unoffered);

    WGPUShaderModule shader = module(s,FULL_SCREEN "@fragment fn fragment()->@location(0) vec4f{return vec4f(3.5,0.0,0.0,1.0);}");
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!image(s,WGPUTextureFormat_Depth32FloatStencil8,WGPUTextureUsage_RenderAttachment,SIZE,SIZE));
    expect_validation(s);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!depth_pipeline(s,shader,WGPUTextureFormat_Depth32FloatStencil8));
    expect_validation(s);
    WGPURenderBundleEncoderDescriptor bundle = {.depthStencilFormat=WGPUTextureFormat_Depth32FloatStencil8,.sampleCount=1};
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    WGPURenderBundleEncoder invalid_bundle = wgpuDeviceCreateRenderBundleEncoder(s->device,&bundle);
    assert(invalid_bundle); /* WebGPU invalid encoder; the error is reported at creation */
    wgpuRenderBundleEncoderRelease(invalid_bundle);
    expect_validation(s);
    /* rg11b10ufloat is core-sampled but renderable only with its feature. */
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!image(s,WGPUTextureFormat_RG11B10Ufloat,WGPUTextureUsage_RenderAttachment,SIZE,SIZE));
    expect_validation(s);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!color_pipeline(s,shader,WGPUTextureFormat_RG11B10Ufloat,NULL));
    expect_validation(s);
    WGPUTextureFormat rg11b10 = WGPUTextureFormat_RG11B10Ufloat;
    WGPURenderBundleEncoderDescriptor color_bundle = {.colorFormatCount=1,.colorFormats=&rg11b10,.sampleCount=1};
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    WGPURenderBundleEncoder invalid_color_bundle = wgpuDeviceCreateRenderBundleEncoder(s->device,&color_bundle);
    assert(invalid_color_bundle); /* WebGPU invalid encoder; the error is reported at creation */
    wgpuRenderBundleEncoderRelease(invalid_color_bundle);
    expect_validation(s);
    rg11b10_sampled(s);
    WGPURenderPipeline control = depth_pipeline(s,shader,WGPUTextureFormat_Depth24PlusStencil8);
    assert(control);
    wgpuRenderPipelineRelease(control);

    const WGPUBlendComponent add = {.operation=WGPUBlendOperation_Add,.srcFactor=WGPUBlendFactor_One,.dstFactor=WGPUBlendFactor_One};
    const WGPUBlendState blend = {.color=add,.alpha=add};
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!color_pipeline(s,shader,WGPUTextureFormat_R32Float,&blend));
    expect_validation(s);
    WGPURenderPipeline unblended = color_pipeline(s,shader,WGPUTextureFormat_R32Float,NULL);
    WGPURenderPipeline half_float = color_pipeline(s,shader,WGPUTextureFormat_RGBA16Float,&blend);
    assert(unblended && half_float);
    wgpuRenderPipelineRelease(unblended);
    wgpuRenderPipelineRelease(half_float);
    wgpuShaderModuleRelease(shader);

    /* A sampler-using shader's automatic layout is 'float': r32float views are
     * unfilterable without the feature; rgba16float binds as the control. */
    WGPUComputePipeline sampling = pipeline(s,sampling_code);
    WGPUTexture r32 = image(s,WGPUTextureFormat_R32Float,WGPUTextureUsage_TextureBinding,2,1);
    WGPUTexture r16 = image(s,WGPUTextureFormat_RGBA16Float,WGPUTextureUsage_TextureBinding,2,1);
    assert(r32 && r16);
    WGPUTextureView r32_view = view_of(r32), r16_view = view_of(r16);
    WGPUSampler sampler = linear_sampler(s);
    WGPUBuffer out = buffer(s,64,WGPUBufferUsage_Storage);
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!sampling_group(s,sampling,r32_view,r16_view,sampler,out));
    expect_validation(s);
    WGPUBindGroup accepted = sampling_group(s,sampling,r16_view,r16_view,sampler,out);
    assert(accepted);
    wgpuBindGroupRelease(accepted);
    wgpuBufferRelease(out);
    wgpuSamplerRelease(sampler);
    wgpuTextureViewRelease(r32_view);
    wgpuTextureViewRelease(r16_view);
    wgpuTextureRelease(r32);
    wgpuTextureRelease(r16);
    wgpuComputePipelineRelease(sampling);
}

/* Linear filtering of exact f32 values that no 16-bit format can hold. */
static void filtered(state *s)
{
    WGPUTexture r32 = image(s,WGPUTextureFormat_R32Float,WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,2,1);
    WGPUTexture c32 = image(s,WGPUTextureFormat_RGBA32Float,WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,2,1);
    assert(r32 && c32);
    const float r_data[2] = {1000000.0f,1000002.0f};
    const float c_data[8] = {0.0f,10.0f,1000000.0f,-4.0f, 2.0f,20.0f,1000002.0f,4.0f};
    upload(s,r32,r_data,sizeof(r_data),2);
    upload(s,c32,c_data,sizeof(c_data),2);
    WGPUComputePipeline sampling = pipeline(s,sampling_code);
    WGPUTextureView r_view = view_of(r32), c_view = view_of(c32);
    WGPUSampler sampler = linear_sampler(s);
    WGPUBuffer out = buffer(s,64,WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(s,64,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUBindGroup group_handle = sampling_group(s,sampling,r_view,c_view,sampler,out);
    assert(group_handle);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderSetPipeline(pass,sampling);
    wgpuComputePassEncoderSetBindGroup(pass,0,group_handle,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,out,0,readback,0,64);
    submit(s,encoder);
    const float *values = map(s,readback,64);
    const float expected[16] = {1000001.0f,0.0f,0.0f,1.0f, 1000000.0f,0.0f,0.0f,1.0f,
        1.0f,15.0f,1000001.0f,0.0f, 2.0f,20.0f,1000002.0f,4.0f};
    for (size_t i = 0; i < 16; ++i) if (values[i] != expected[i]) {
        fprintf(stderr,"filtered component %zu: %.9g != %.9g\n",i,values[i],expected[i]);
        abort();
    }
    wgpuBufferUnmap(readback);
    wgpuBindGroupRelease(group_handle);
    wgpuBufferRelease(readback);
    wgpuBufferRelease(out);
    wgpuSamplerRelease(sampler);
    wgpuTextureViewRelease(r_view);
    wgpuTextureViewRelease(c_view);
    wgpuComputePipelineRelease(sampling);
    wgpuTextureRelease(r32);
    wgpuTextureRelease(c32);
}

/* Two additive draws of 3.5 onto a 1000000.0 clear: exact only in f32. */
static void blended(state *s)
{
    WGPUShaderModule shader = module(s,FULL_SCREEN "@fragment fn fragment()->@location(0) vec4f{return vec4f(3.5,0.0,0.0,1.0);}");
    const WGPUBlendComponent add = {.operation=WGPUBlendOperation_Add,.srcFactor=WGPUBlendFactor_One,.dstFactor=WGPUBlendFactor_One};
    const WGPUBlendState blend = {.color=add,.alpha=add};
    WGPURenderPipeline pipeline_handle = color_pipeline(s,shader,WGPUTextureFormat_R32Float,&blend);
    WGPUTexture target = image(s,WGPUTextureFormat_R32Float,WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc,SIZE,SIZE);
    assert(pipeline_handle && target);
    WGPUTextureView view = view_of(target);
    WGPUBuffer readback = buffer(s,PITCH * SIZE,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = view;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = (WGPUColor){1000000.0,0.0,0.0,0.0};
    WGPURenderPassDescriptor descriptor = {.colorAttachmentCount=1,.colorAttachments=&color};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
    wgpuRenderPassEncoderSetPipeline(pass,pipeline_handle);
    wgpuRenderPassEncoderDraw(pass,3,1,0,0);
    wgpuRenderPassEncoderDraw(pass,3,1,0,0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    texture_to_buffer(encoder,target,readback,0,SIZE,SIZE);
    submit(s,encoder);
    const unsigned char *bytes = map(s,readback,PITCH * SIZE);
    for (uint32_t y = 0; y < SIZE; ++y) for (uint32_t x = 0; x < SIZE; ++x) {
        float value;
        memcpy(&value,bytes + y * PITCH + x * 4,4);
        if (value != 1000007.0f) {
            fprintf(stderr,"blended texel (%u,%u): %.9g\n",x,y,value);
            abort();
        }
    }
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(target);
    wgpuRenderPipelineRelease(pipeline_handle);
    wgpuShaderModuleRelease(shader);
}

/* depth32float-stencil8: depth 0.25 and stencil reference 7 per aspect. */
static void depth_stencil(state *s)
{
    WGPUShaderModule shader = module(s,FULL_SCREEN);
    WGPURenderPipeline pipeline_handle = depth_pipeline(s,shader,WGPUTextureFormat_Depth32FloatStencil8);
    WGPUTexture target = image(s,WGPUTextureFormat_Depth32FloatStencil8,
        WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc | WGPUTextureUsage_TextureBinding,SIZE,SIZE);
    assert(pipeline_handle && target);
    WGPURenderBundleEncoderDescriptor bundle = {.depthStencilFormat=WGPUTextureFormat_Depth32FloatStencil8,.sampleCount=1};
    WGPURenderBundleEncoder bundle_encoder = wgpuDeviceCreateRenderBundleEncoder(s->device,&bundle);
    assert(bundle_encoder);
    wgpuRenderBundleEncoderRelease(bundle_encoder);
    WGPUTextureView view = view_of(target);
    WGPUTextureViewDescriptor depth_only = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    depth_only.aspect = WGPUTextureAspect_DepthOnly;
    WGPUTextureView sampled = wgpuTextureCreateView(target,&depth_only);
    assert(sampled);
    WGPUBuffer readback = buffer(s,2 * PITCH * SIZE,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPURenderPassDepthStencilAttachment attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.depthLoadOp = attachment.stencilLoadOp = WGPULoadOp_Clear;
    attachment.depthStoreOp = attachment.stencilStoreOp = WGPUStoreOp_Store;
    attachment.depthClearValue = 1.0f;
    WGPURenderPassDescriptor descriptor = {.depthStencilAttachment=&attachment};
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
    wgpuRenderPassEncoderSetPipeline(pass,pipeline_handle);
    wgpuRenderPassEncoderSetStencilReference(pass,7);
    wgpuRenderPassEncoderDraw(pass,3,1,0,0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    const WGPUTextureAspect aspects[2] = {WGPUTextureAspect_DepthOnly,WGPUTextureAspect_StencilOnly};
    for (uint32_t i = 0; i < 2; ++i) {
        WGPUTexelCopyTextureInfo source = {.texture=target,.aspect=aspects[i]};
        WGPUTexelCopyBufferInfo destination = {.buffer=readback,
            .layout={.offset=i * PITCH * SIZE,.bytesPerRow=PITCH,.rowsPerImage=SIZE}};
        wgpuCommandEncoderCopyTextureToBuffer(encoder,&source,&destination,&(WGPUExtent3D){SIZE,SIZE,1});
    }
    submit(s,encoder);
    const unsigned char *bytes = map(s,readback,2 * PITCH * SIZE);
    for (uint32_t y = 0; y < SIZE; ++y) for (uint32_t x = 0; x < SIZE; ++x) {
        float depth;
        memcpy(&depth,bytes + y * PITCH + x * 4,4);
        uint8_t stencil = bytes[PITCH * SIZE + y * PITCH + x];
        if (depth != 0.25f || stencil != 7) {
            fprintf(stderr,"depth32float-stencil8 texel (%u,%u): depth %.9g stencil %u\n",x,y,depth,stencil);
            abort();
        }
    }
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    wgpuTextureViewRelease(sampled);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(target);
    wgpuRenderPipelineRelease(pipeline_handle);
    wgpuShaderModuleRelease(shader);
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    const WGPUFeatureName names[4] = {WGPUFeatureName_Float32Filterable,WGPUFeatureName_Float32Blendable,
        WGPUFeatureName_Depth32FloatStencil8,WGPUFeatureName_RG11B10UfloatRenderable};
    const char *const labels[4] = {"float32-filterable","float32-blendable","depth32float-stencil8",
        "rg11b10ufloat-renderable"};
    WGPUFeatureName offered[4];
    size_t count = 0;
    WGPUSupportedFeatures listed = WGPU_SUPPORTED_FEATURES_INIT;
    wgpuAdapterGetFeatures(s.adapter,&listed);
    for (size_t i = 0; i < 4; ++i) {
        bool in_list = false;
        for (size_t j = 0; j < listed.featureCount; ++j) in_list |= listed.features[j] == names[i];
        assert(in_list == (wgpuAdapterHasFeature(s.adapter,names[i]) != 0));
        if (in_list) offered[count++] = names[i];
        else printf("SKIP %s: adapter does not offer the native format features it needs %s\n",
            labels[i],adapter_class_scope());
    }
    wgpuSupportedFeaturesFreeMembers(listed);
    disabled_controls(&s);
    assert(s.validation_errors == 8);
    consumer_finish(&s);

    state enabled = {0};
    consumer_init_features(&enabled,offered,count);
    WGPUSupportedFeatures device_features = WGPU_SUPPORTED_FEATURES_INIT;
    wgpuDeviceGetFeatures(enabled.device,&device_features);
    assert(device_features.featureCount == count + 1); /* plus core-features-and-limits */
    wgpuSupportedFeaturesFreeMembers(device_features);
    for (size_t i = 0; i < count; ++i) {
        assert(wgpuDeviceHasFeature(enabled.device,offered[i]));
        if (offered[i] == WGPUFeatureName_Float32Filterable) filtered(&enabled);
        if (offered[i] == WGPUFeatureName_Float32Blendable) blended(&enabled);
        if (offered[i] == WGPUFeatureName_Depth32FloatStencil8) depth_stencil(&enabled);
        if (offered[i] == WGPUFeatureName_RG11B10UfloatRenderable) rg11b10_rendered(&enabled);
    }
    consumer_finish(&enabled);
    printf("PASS public format features: %zu of 4 offered and enabled (float32-filterable linear r32float/"
        "rgba32float midpoints exact, float32-blendable additive r32float 1000007.0, depth32float-stencil8 "
        "depth 0.25/stencil 7 per aspect, rg11b10ufloat-renderable blended (1.25,1,2.25) and 4x-resolved "
        "(1,.5,.25) packed bits); without them depth32float-stencil8 texture/pipeline/bundle, r32float "
        "blending, 'float' r32float binding and rg11b10ufloat attachment/target/bundle reject while "
        "rg11b10ufloat still samples exactly, an unoffered feature fails the request %s\n",
        count, adapter_class_scope());
    return 0;
}
