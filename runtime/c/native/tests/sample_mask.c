/* SPDX-License-Identifier: MIT */
#include "consumer.h"
#include <stdlib.h>

static WGPUShaderModule module(state *s)
{
    static const char code[] =
        "@group(0) @binding(0) var<storage,read_write> effects:array<atomic<u32>>;"
        "override shader_mask:u32=15u;"
        "@vertex fn vertex(@builtin(vertex_index) i:u32)->@builtin(position) vec4f{"
        "if(i==0u){return vec4f(-1,-1,0.5,1);}if(i==1u){return vec4f(3,-1,0.5,1);}return vec4f(-1,3,0.5,1);}"
        "struct Out{@location(0) color:vec4f,@builtin(sample_mask) coverage:u32}"
        "@fragment fn covered()->Out{atomicAdd(&effects[0],1u);return Out(vec4f(1,0,0,1),shader_mask);}"
        "@fragment fn plain()->@location(0) vec4f{atomicAdd(&effects[0],1u);return vec4f(1,0,0,1);}";
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code,sizeof(code)-1};
    WGPUShaderModuleDescriptor desc = {.nextInChain=&source.chain};
    WGPUShaderModule result = wgpuDeviceCreateShaderModule(s->device,&desc);
    assert(result);
    return result;
}
static void coverage_case(state *s, WGPUShaderModule shader, uint32_t samples,
    uint32_t fixed_mask, uint32_t shader_mask, bool covered, uint32_t written)
{
    WGPUColorTargetState color = {.format=WGPUTextureFormat_RGBA8Unorm,.writeMask=WGPUColorWriteMask_All};
    WGPUConstantEntry value = {.key={"shader_mask",WGPU_STRLEN},.value=shader_mask};
    WGPUFragmentState fragment = {.module=shader,.targetCount=1,.targets=&color,
        .entryPoint={covered ? "covered" : "plain",WGPU_STRLEN},
        .constantCount=covered ? 1 : 0,.constants=covered ? &value : NULL};
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.vertex.module = shader;
    desc.fragment = &fragment;
    desc.multisample.count = samples;
    desc.multisample.mask = fixed_mask;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(s->device,&desc);
    assert(pipeline);
    WGPUBuffer effects = buffer(s,4,WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc);
    WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(pipeline,0);
    WGPUBindGroupEntry binding = {.binding=0,.buffer=effects,.size=4};
    WGPUBindGroupDescriptor gd = {.layout=layout,.entryCount=1,.entries=&binding};
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(s->device,&gd);
    assert(layout && group);
    WGPUTexture resolved = texture(s,WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc,1);
    WGPUTexture target = resolved;
    if (samples != 1) {
        WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
        td.format = color.format;
        td.size = (WGPUExtent3D){2,2,1};
        td.usage = WGPUTextureUsage_RenderAttachment;
        td.sampleCount = samples;
        target = wgpuDeviceCreateTexture(s->device,&td);
        assert(target);
    }
    WGPUTextureView tv = wgpuTextureCreateView(target,NULL), rv = wgpuTextureCreateView(resolved,NULL);
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = tv;
    attachment.resolveTarget = samples == 1 ? NULL : rv;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    attachment.clearValue = (WGPUColor){0,0,1,1};
    WGPURenderPassDescriptor pd = {.colorAttachmentCount=1,.colorAttachments=&attachment};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device,NULL);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,&pd);
    assert(pass);
    wgpuRenderPassEncoderSetPipeline(pass,pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass,0,group,0,NULL);
    wgpuRenderPassEncoderDraw(pass,3,1,0,0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUBuffer readback = buffer(s,520,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    texture_to_buffer(encoder,resolved,readback,0,2,2);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,effects,0,readback,512,4);
    wgpuRenderPipelineRelease(pipeline);
    wgpuBindGroupRelease(group);
    wgpuBindGroupLayoutRelease(layout);
    wgpuBufferRelease(effects);
    wgpuTextureViewRelease(tv);
    wgpuTextureViewRelease(rv);
    if (target != resolved) wgpuTextureRelease(target);
    wgpuTextureRelease(resolved);
    submit(s,encoder);
    WGPUBufferMapCallbackInfo callback = {.mode=WGPUCallbackMode_WaitAnyOnly,.callback=mapped,.userdata1=s};
    WGPUFutureWaitInfo wait = {.future=wgpuBufferMapAsync(readback,WGPUMapMode_Read,0,520,callback)};
    assert(wait.future.id && wgpuInstanceWaitAny(s->instance,1,&wait,UINT64_C(5000000000)) == WGPUWaitStatus_Success);
    const unsigned char *bytes = wgpuBufferGetConstMappedRange(readback,0,520);
    assert(bytes && wait.completed);
    uint32_t invoked;
    memcpy(&invoked,bytes+512,4);
    assert(invoked > 0); /* A zero coverage output must retain fragment effects. */
    const int red = (int)((255u*written + samples/2)/samples);
    for (size_t y=0;y<2;++y) for (size_t x=0;x<2;++x) {
        const unsigned char *pixel = bytes+y*256+x*4;
        assert(abs((int)pixel[0]-red) <= 1 && pixel[1] == 0 &&
            abs((int)pixel[2]-(255-red)) <= 1 && pixel[3] == 255);
    }
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    printf("mask samples=%u fixed=0x%08x shader=0x%08x declared=%u covered=%u effects=%u PASS\n",
        samples,fixed_mask,shader_mask,covered,written,invoked);
}
int main(void)
{
    state s = {0};
    consumer_init(&s);
    WGPUShaderModule shader = module(&s);
    coverage_case(&s,shader,1,UINT32_MAX,1,false,1);
    coverage_case(&s,shader,1,0,1,false,0);
    coverage_case(&s,shader,1,2,15,true,0);
    coverage_case(&s,shader,4,UINT32_MAX,5,true,2);
    coverage_case(&s,shader,4,5,15,false,2);
    coverage_case(&s,shader,4,5,6,true,1);
    coverage_case(&s,shader,4,5,10,true,0);
    coverage_case(&s,shader,4,0,15,true,0);
    wgpuShaderModuleRelease(shader);
    consumer_finish(&s);
    printf("PASS public fragment sample-mask intersection, resolved pixels and zero-mask side effects %s\n",adapter_class_scope());
    return 0;
}
