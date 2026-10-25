/* SPDX-License-Identifier: MIT
 * Feature-requested native pipeline composition, not raster/output parity. */
#include "consumer.h"

static WGPUShaderModule module(state *s,const char *text)
{
    WGPUShaderSourceWGSL source=WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code=(WGPUStringView){text,WGPU_STRLEN};
    WGPUShaderModuleDescriptor descriptor={.nextInChain=&source.chain};
    WGPUShaderModule result=wgpuDeviceCreateShaderModule(s->device,&descriptor);
    assert(result);
    return result;
}
static void expect_pipeline(state *s,WGPURenderPipelineDescriptor *descriptor,bool accepted)
{
    if (!accepted) wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    WGPURenderPipeline result=wgpuDeviceCreateRenderPipeline(s->device,descriptor);
    assert((result!=NULL)==accepted);
    wgpuRenderPipelineRelease(result);
    if (!accepted) expect_validation(s);
}
int main(void)
{
    state s={0};
    WGPUFeatureName features[]={WGPUFeatureName_ClipDistances,WGPUFeatureName_PrimitiveIndex,WGPUFeatureName_DualSourceBlending};
    consumer_init_features(&s,features,sizeof(features)/sizeof(features[0]));
    WGPUShaderModule shader=module(&s,
        "enable clip_distances,primitive_index,dual_source_blending;"
        "struct V{@builtin(position)p:vec4f,@builtin(clip_distances)c:array<f32,5>}"
        "struct F{@location(0)@blend_src(0)a:vec4f,@location(0)@blend_src(1)b:vec4f}"
        "@vertex fn vs()->V{return V(vec4f(0,0,0,1),array<f32,5>(1,1,1,1,1));}"
        "@fragment fn fs(@builtin(primitive_index)i:u32)->F{return F(vec4f(f32(i)),vec4f(0.5));}"
        "@fragment fn ordinary()->@location(0)vec4f{return vec4f(1);}");
    WGPUBlendState blend={
        .color={WGPUBlendOperation_Add,WGPUBlendFactor_Src1,WGPUBlendFactor_OneMinusSrc1},
        .alpha={WGPUBlendOperation_Add,WGPUBlendFactor_Src1Alpha,WGPUBlendFactor_OneMinusSrc1Alpha}};
    WGPUColorTargetState colors[2]={{.format=WGPUTextureFormat_RGBA8Unorm,.blend=&blend,.writeMask=WGPUColorWriteMask_All}};
    colors[1]=colors[0];
    WGPUFragmentState fragment={.module=shader,.entryPoint={"fs",WGPU_STRLEN},.targetCount=1,.targets=colors};
    WGPURenderPipelineDescriptor descriptor={
        .vertex={.module=shader,.entryPoint={"vs",WGPU_STRLEN}},
        .primitive={.topology=WGPUPrimitiveTopology_TriangleList},
        .multisample={.count=1,.mask=UINT32_MAX},.fragment=&fragment};
    expect_pipeline(&s,&descriptor,true);
    fragment.entryPoint=(WGPUStringView){"ordinary",WGPU_STRLEN};
    expect_pipeline(&s,&descriptor,false); /* Source1 blend requires actual secondary output. */
    fragment.entryPoint=(WGPUStringView){"fs",WGPU_STRLEN};
    fragment.targetCount=2;
    expect_pipeline(&s,&descriptor,false); /* Dual source is not MRT. */
    wgpuShaderModuleRelease(shader);
    consumer_finish(&s);
    puts("PASS requested clip-distance/primitive-index/dual-source pipeline composition; no raster oracle");
    return 0;
}
