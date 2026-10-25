/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_TEST_SAMPLING_PAIR_CASES_H
#define DOE_NATIVE_TEST_SAMPLING_PAIR_CASES_H
static void exact_pair_layouts(state *s)
{
    const char *code =
        "@group(0) @binding(0) var loaded:texture_2d<f32>;"
        "@group(0) @binding(1) var sampled:texture_2d<f32>;"
        "@group(0) @binding(2) var sampling:sampler;"
        "@group(0) @binding(3) var<storage,read_write> result:array<f32>;"
        "fn leaf(t:texture_2d<f32>,s:sampler)->f32{return textureSampleLevel(t,s,vec2f(0.5),0.0).r;}"
        "@compute @workgroup_size(1) fn main(){result[0]=textureLoad(loaded,vec2i(0),0).r+leaf(sampled,sampling);}";
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){code,WGPU_STRLEN};
    WGPUShaderModuleDescriptor shader_desc = {.nextInChain=&source.chain};
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(s->device,&shader_desc);
    assert(shader);
    for (unsigned variant = 0; variant < 3; ++variant) {
        WGPUBindGroupLayoutEntry entries[4] = {
            {.binding=0,.visibility=WGPUShaderStage_Compute,
                .texture={.sampleType=WGPUTextureSampleType_UnfilterableFloat,.viewDimension=WGPUTextureViewDimension_2D}},
            {.binding=1,.visibility=WGPUShaderStage_Compute,
                .texture={.sampleType=WGPUTextureSampleType_Float,.viewDimension=WGPUTextureViewDimension_2D}},
            {.binding=2,.visibility=WGPUShaderStage_Compute,.sampler={.type=WGPUSamplerBindingType_Filtering}},
            {.binding=3,.visibility=WGPUShaderStage_Compute,.buffer={.type=WGPUBufferBindingType_Storage}}
        };
        if (variant) entries[1].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        if (variant == 2) entries[2].sampler.type = WGPUSamplerBindingType_NonFiltering;
        WGPUBindGroupLayoutDescriptor group_desc = {.entryCount=4,.entries=entries};
        WGPUBindGroupLayout group_layout = wgpuDeviceCreateBindGroupLayout(s->device,&group_desc);
        assert(group_layout);
        WGPUPipelineLayoutDescriptor layout_desc = {.bindGroupLayoutCount=1,.bindGroupLayouts=&group_layout};
        WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(s->device,&layout_desc);
        assert(layout);
        WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        desc.layout = layout; desc.compute.module = shader;
        if (variant == 1) wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
        WGPUComputePipeline program = wgpuDeviceCreateComputePipeline(s->device,&desc);
        if (variant == 1) { assert(!program); expect_validation(s); }
        else assert(program);
        wgpuComputePipelineRelease(program);
        wgpuPipelineLayoutRelease(layout); wgpuBindGroupLayoutRelease(group_layout);
    }
    wgpuShaderModuleRelease(shader);
}

#endif
