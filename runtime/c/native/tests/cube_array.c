/* SPDX-License-Identifier: MIT
 * Public cube-array sampling: two cubes (12 uniformly filled rgba8unorm faces)
 * sampled in compute through texture_cube_array<f32>/textureSampleLevel along
 * each face axis and array index, textureNumLayers, an automatic CubeArray
 * layout, and view/layout controls. Requires the device's imageCubeArray
 * capability, which the Vulkan owner enables only when offered. */
#include "consumer.h"
#include <math.h>

enum { FACES = 6, CUBES = 2, LAYERS = FACES * CUBES, EDGE = 2 };

static unsigned char face_value(uint32_t layer) { return (unsigned char)(10 + 20 * layer); }

static WGPUComputePipeline reader(state *s)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){
        "@group(0) @binding(0) var t:texture_cube_array<f32>;@group(0) @binding(1) var s:sampler;"
        "@group(0) @binding(2) var<storage,read_write> o:array<f32>;"
        "@compute @workgroup_size(1) fn main(){"
        "var axes=array<vec3f,6>(vec3f(1,0,0),vec3f(-1,0,0),vec3f(0,1,0),vec3f(0,-1,0),vec3f(0,0,1),vec3f(0,0,-1));"
        "for(var c=0u;c<2u;c++){for(var f=0u;f<6u;f++){"
        "o[c*6u+f]=textureSampleLevel(t,s,axes[f],i32(c),0.0).x;}}"
        "o[12]=f32(textureNumLayers(t));}",WGPU_STRLEN};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain=&source.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s->device,&descriptor);
    assert(module);
    WGPUComputePipelineDescriptor pipeline = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipeline.compute.module = module;
    WGPUComputePipeline result = wgpuDeviceCreateComputePipeline(s->device,&pipeline);
    wgpuShaderModuleRelease(module);
    assert(result);
    return result;
}
static void rejected_view(state *s, WGPUTexture texture, uint32_t layers, WGPUTextureViewDimension dimension)
{
    WGPUTextureViewDescriptor descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    descriptor.dimension = dimension;
    descriptor.arrayLayerCount = layers;
    wgpuDevicePushErrorScope(s->device,WGPUErrorFilter_Validation);
    assert(!wgpuTextureCreateView(texture,&descriptor));
    expect_validation(s);
}
int main(void)
{
    state s = {0};
    consumer_init(&s);
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.format = WGPUTextureFormat_RGBA8Unorm;
    descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    descriptor.size = (WGPUExtent3D){EDGE,EDGE,LAYERS};
    WGPUTexture texture = wgpuDeviceCreateTexture(s.device,&descriptor);
    assert(texture);
    for (uint32_t layer = 0; layer < LAYERS; ++layer) {
        unsigned char texels[EDGE * EDGE * 4];
        for (size_t i = 0; i < EDGE * EDGE; ++i) {
            texels[i * 4] = face_value(layer);
            texels[i * 4 + 1] = (unsigned char)layer;
            texels[i * 4 + 2] = 0;
            texels[i * 4 + 3] = 255;
        }
        WGPUTexelCopyTextureInfo destination = {.texture=texture,.origin={0,0,layer}};
        WGPUTexelCopyBufferLayout layout = {.bytesPerRow=EDGE * 4,.rowsPerImage=EDGE};
        WGPUExtent3D size = {EDGE,EDGE,1};
        wgpuQueueWriteTexture(s.queue,&destination,texels,sizeof(texels),&layout,&size);
    }
    /* Controls: partial cube array; cube-array storage layouts are invalid. */
    rejected_view(&s,texture,8,WGPUTextureViewDimension_CubeArray);
    rejected_view(&s,texture,12,WGPUTextureViewDimension_Cube);
    WGPUBindGroupLayoutEntry storage = {.binding=0,.visibility=WGPUShaderStage_Compute,
        .storageTexture={.access=WGPUStorageTextureAccess_WriteOnly,.format=WGPUTextureFormat_RGBA8Unorm,
            .viewDimension=WGPUTextureViewDimension_CubeArray}};
    WGPUBindGroupLayoutDescriptor storage_layout = {.entryCount=1,.entries=&storage};
    wgpuDevicePushErrorScope(s.device,WGPUErrorFilter_Validation);
    assert(!wgpuDeviceCreateBindGroupLayout(s.device,&storage_layout));
    expect_validation(&s);

    WGPUTextureViewDescriptor view_descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    view_descriptor.dimension = WGPUTextureViewDimension_CubeArray;
    WGPUTextureView view = wgpuTextureCreateView(texture,&view_descriptor);
    assert(view);
    WGPUSamplerDescriptor sampler_descriptor = WGPU_SAMPLER_DESCRIPTOR_INIT;
    WGPUSampler sampler = wgpuDeviceCreateSampler(s.device,&sampler_descriptor);
    WGPUComputePipeline pipeline = reader(&s);
    WGPUBuffer output = buffer(&s,256,WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(&s,256,WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(pipeline,0);
    WGPUBindGroupEntry entries[3] = {{.binding=0,.textureView=view},{.binding=1,.sampler=sampler},
        {.binding=2,.buffer=output,.size=256}};
    WGPUBindGroupDescriptor group_descriptor = {.layout=layout,.entryCount=3,.entries=entries};
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(s.device,&group_descriptor);
    assert(group);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    wgpuComputePassEncoderSetPipeline(pass,pipeline);
    wgpuComputePassEncoderSetBindGroup(pass,0,group,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,output,0,readback,0,256);
    wgpuBindGroupRelease(group);
    wgpuBindGroupLayoutRelease(layout);
    wgpuComputePipelineRelease(pipeline);
    wgpuSamplerRelease(sampler);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(texture);
    wgpuBufferRelease(output);
    submit(&s,encoder);
    const float *values = map(&s,readback,256);
    /* Vulkan's unorm8->float conversion is not bit-specified (lavapipe differs
     * from k/255 by an ulp), so the exact oracle is the decoded 8-bit value. */
    for (uint32_t layer = 0; layer < LAYERS; ++layer) {
        float scaled = values[layer] * 255.0f;
        assert(lrintf(scaled) == face_value(layer) && fabsf(scaled - (float)face_value(layer)) < 0.01f);
    }
    assert(values[12] == 2.0f);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    consumer_finish(&s);
    printf("PASS public cube arrays: enabled imageCubeArray, two uniformly filled cubes sampled along all six face axes per array index via texture_cube_array/textureSampleLevel, textureNumLayers, automatic CubeArray layout, partial/cube-dimension view and storage-layout controls, early releases; exact 12 face values and 1 layer count %s\n",
        adapter_class_scope());
}
