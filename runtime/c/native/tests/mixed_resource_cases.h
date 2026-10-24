/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_TEST_MIXED_RESOURCE_CASES_H
#define DOE_NATIVE_TEST_MIXED_RESOURCE_CASES_H
#include "consumer.h"

typedef struct mixed_resources {
    WGPUComputePipeline pipeline;
    WGPUTexture sampled, stored;
    WGPUTextureView sampled_view, stored_view;
    WGPUSampler sampler;
    WGPUBuffer uniform, output, readback;
    WGPUBindGroup groups[4];
} mixed_resources;

static void mixed_resources_create(state *s, mixed_resources *resources)
{
    const char *code =
        "@group(0) @binding(999) var<storage,read_write> result:array<u32>;"
        "@group(1) @binding(999) var input:texture_2d<f32>;"
        "@group(2) @binding(999) var nearest:sampler;"
        "@group(3) @binding(998) var image_output:texture_storage_2d<rgba32uint,write>;"
        "@group(3) @binding(999) var<uniform> params:vec4u;"
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id:vec3u){"
        "if(id.x<4u){let sampled=textureSampleLevel(input,nearest,vec2f((f32(id.x)+0.5)/4.0,0.5),0.0);"
        "let value=u32(sampled.x*255.0+0.5)*params.x+params.y+id.x;"
        "result[id.x]=value;textureStore(image_output,vec2i(i32(id.x),0),vec4u(value,17u+id.x,99u,255u));}}";
    resources->pipeline = pipeline(s, code);
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.size = (WGPUExtent3D){4,1,1};
    descriptor.format = WGPUTextureFormat_RGBA8Unorm;
    descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    resources->sampled = wgpuDeviceCreateTexture(s->device, &descriptor);
    descriptor.format = WGPUTextureFormat_RGBA32Uint;
    descriptor.usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc;
    resources->stored = wgpuDeviceCreateTexture(s->device, &descriptor);
    assert(resources->sampled && resources->stored);
    resources->sampled_view = wgpuTextureCreateView(resources->sampled, NULL);
    resources->stored_view = wgpuTextureCreateView(resources->stored, NULL);
    WGPUSamplerDescriptor sampler = WGPU_SAMPLER_DESCRIPTOR_INIT;
    resources->sampler = wgpuDeviceCreateSampler(s->device, &sampler);
    resources->uniform = buffer(s, 16, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
    resources->output = buffer(s, 16, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    resources->readback = buffer(s, 512, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    assert(resources->sampled_view && resources->stored_view && resources->sampler);
    const uint32_t params[4] = {3,7,0,0};
    wgpuQueueWriteBuffer(s->queue, resources->uniform, 0, params, sizeof(params));
    const unsigned char pixels[16] = {11,0,0,255, 37,0,0,255, 73,0,0,255, 191,0,0,255};
    WGPUTexelCopyTextureInfo destination = {.texture=resources->sampled};
    WGPUTexelCopyBufferLayout layout = {.bytesPerRow=16, .rowsPerImage=1};
    WGPUExtent3D size = {4,1,1};
    wgpuQueueWriteTexture(s->queue, &destination, pixels, sizeof(pixels), &layout, &size);
    const WGPUBindGroupEntry entries[5] = {
        {.binding=999, .buffer=resources->output, .size=16},
        {.binding=999, .textureView=resources->sampled_view},
        {.binding=999, .sampler=resources->sampler},
        {.binding=998, .textureView=resources->stored_view},
        {.binding=999, .buffer=resources->uniform, .size=16}
    };
    for (uint32_t i = 0; i < 4; ++i) {
        WGPUBindGroupLayout group_layout = wgpuComputePipelineGetBindGroupLayout(resources->pipeline, i);
        assert(group_layout);
        WGPUBindGroupDescriptor group_descriptor = {.layout=group_layout,
            .entryCount=i == 3 ? 2 : 1, .entries=&entries[i]};
        resources->groups[i] = wgpuDeviceCreateBindGroup(s->device, &group_descriptor);
        wgpuBindGroupLayoutRelease(group_layout);
        assert(resources->groups[i]);
    }
}

static WGPUCommandEncoder mixed_resources_record(state *s, const mixed_resources *resources, bool readback)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    assert(pass);
    wgpuComputePassEncoderSetPipeline(pass, resources->pipeline);
    for (uint32_t i = 0; i < 4; ++i) wgpuComputePassEncoderSetBindGroup(pass, i, resources->groups[i], 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 4, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    if (readback) {
        texture_to_buffer(encoder, resources->stored, resources->readback, 0, 4, 1);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, resources->output, 0, resources->readback, 256, 16);
    }
    return encoder;
}

static void mixed_resources_release_inputs(mixed_resources *resources)
{
    for (size_t i = 0; i < 4; ++i) wgpuBindGroupRelease(resources->groups[i]);
    wgpuComputePipelineRelease(resources->pipeline);
    wgpuTextureViewRelease(resources->sampled_view);
    wgpuTextureViewRelease(resources->stored_view);
    wgpuTextureRelease(resources->sampled);
    wgpuTextureRelease(resources->stored);
    wgpuSamplerRelease(resources->sampler);
    wgpuBufferRelease(resources->uniform);
    wgpuBufferRelease(resources->output);
}

static void mixed_resources_pixels(state *s)
{
    mixed_resources resources = {0};
    mixed_resources_create(s, &resources);
    WGPUCommandEncoder encoder = mixed_resources_record(s, &resources, true);
    mixed_resources_release_inputs(&resources);
    submit(s, encoder);
    const uint32_t *actual = map(s, resources.readback, 512);
    const uint32_t expected[4] = {40,119,228,583};
    for (size_t i = 0; i < 4; ++i) {
        assert(actual[i * 4] == expected[i] && actual[i * 4 + 1] == 17 + i);
        assert(actual[i * 4 + 2] == 99 && actual[i * 4 + 3] == 255);
        assert(actual[64 + i] == expected[i]);
    }
    wgpuBufferUnmap(resources.readback);
    wgpuBufferRelease(resources.readback);
}
#endif
