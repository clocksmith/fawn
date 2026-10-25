/* SPDX-License-Identifier: MIT
 * Public half-stage composition: canonical identity with native f32 carriers. */
#include "consumer.h"

static WGPUShaderModule module(state *s, const char *code)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code, WGPU_STRLEN};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain=&source.chain};
    WGPUShaderModule result = wgpuDeviceCreateShaderModule(s->device, &descriptor);
    assert(result);
    return result;
}

static void expect_pipeline(state *s, WGPURenderPipelineDescriptor *descriptor, bool accepted)
{
    if (!accepted) wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(s->device, descriptor);
    assert((pipeline != NULL) == accepted);
    wgpuRenderPipelineRelease(pipeline);
    if (!accepted) expect_validation(s);
}

int main(void)
{
    state s = {0};
    WGPUFeatureName feature = WGPUFeatureName_ShaderF16;
    consumer_init_features(&s, &feature, 1);
    WGPUShaderModule half = module(&s, "enable f16;"
        "struct Out{@builtin(position)pos:vec4f,@location(0)x:vec3h}"
        "@vertex fn vs(@location(0)x:vec3h)->Out{return Out(vec4f(vec3f(x),1.0),x);}"
        "@fragment fn fs(@location(0)x:vec3h)->@location(0)vec4h{return vec4h(x,1.0h);}");
    WGPUShaderModule full = module(&s,
        "@fragment fn fs(@location(0)x:vec3f)->@location(0)vec4f{return vec4f(x,1.0);}");
    WGPUVertexAttribute attribute = {.format=WGPUVertexFormat_Float32x3};
    WGPUVertexBufferLayout layout = {.arrayStride=12,.attributeCount=1,.attributes=&attribute};
    WGPUColorTargetState color = {.format=WGPUTextureFormat_RGBA8Unorm,.writeMask=WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module=half,.targetCount=1,.targets=&color};
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex = (WGPUVertexState){.module=half,.bufferCount=1,.buffers=&layout};
    descriptor.fragment = &fragment;
    expect_pipeline(&s, &descriptor, true);
    attribute.format = WGPUVertexFormat_Float16x4;
    layout.arrayStride = 8;
    expect_pipeline(&s, &descriptor, true);
    attribute.format = WGPUVertexFormat_Uint32x2;
    expect_pipeline(&s, &descriptor, false);
    attribute.format = WGPUVertexFormat_Float16x4;
    color.format = WGPUTextureFormat_RGBA8Uint;
    expect_pipeline(&s, &descriptor, false);
    color.format = WGPUTextureFormat_RGBA8Unorm;
    fragment.module = full;
    expect_pipeline(&s, &descriptor, false); /* Equal carriers do not erase WGSL type identity. */
    fragment.module = half;
    descriptor.multisample.count = 4;
    descriptor.multisample.alphaToCoverageEnabled = true;
    expect_pipeline(&s, &descriptor, true);
    wgpuShaderModuleRelease(full);
    wgpuShaderModuleRelease(half);
    consumer_finish(&s);
    puts("PASS half-stage floating fetch/targets, exact interstage identity, and alpha coverage");
    return 0;
}
