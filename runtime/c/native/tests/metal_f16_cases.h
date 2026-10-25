/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_TEST_METAL_F16_CASES_H
#define DOE_NATIVE_TEST_METAL_F16_CASES_H
#include "shader_fixture.h"

static void metal_f16_capabilities(void)
{
    uint32_t words[] = {0x07230203,0x00010300,0,1,0,0x00020011,1,0x00020011,9,
        0x00020011,4433,0x00020011,4434};
    doe_shader_view view = {.words=words,.counts={.words=sizeof(words)/sizeof(words[0])}};
    doe_vk_caps caps = {0};
    assert(!native_shader_capabilities(&view, &caps));
    caps.shader_f16 = true;
    assert(native_shader_capabilities(&view, &caps));
    words[12] = 4436; /* Stage half IO is not enabled by the buffer feature. */
    assert(!native_shader_capabilities(&view, &caps));
    words[12] = 4434;
    words[8] = 10; /* Float64 is not implied by Float16. */
    assert(!native_shader_capabilities(&view, &caps));
}

static void metal_f16_disabled(state *s)
{
    assert(!wgpuDeviceHasFeature(s->device, WGPUFeatureName_ShaderF16));
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = (WGPUStringView){"enable f16; @compute @workgroup_size(1) fn main(){let x=1.0h;}",WGPU_STRLEN};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain=&source.chain};
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(s->device, &descriptor);
    expect_validation(s);
    assert(shader);
    native_shader *payload = doe_resource_shader_native((doe_resource_shader *)shader);
    assert(!payload->module && payload->error.diagnostic.message[0]);
    wgpuShaderModuleRelease(shader);
}

static void metal_f16_compact_storage(state *s, mock_native *mock)
{
    WGPUComputePipeline compute = pipeline(s,
        "enable f16; @group(0) @binding(0) var<storage,read_write> x:array<f16>;"
        "@compute @workgroup_size(1) fn main(){x[1]=x[0]+1.0h;}");
    assert(compute);
    WGPUBuffer data = buffer(s, 4, WGPUBufferUsage_Storage);
    WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(compute, 0);
    WGPUBindGroupEntry entry = {.binding=0,.buffer=data,.size=4};
    WGPUBindGroupDescriptor descriptor = {.layout=layout,.entryCount=1,.entries=&entry};
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(s->device, &descriptor);
    assert(group);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(pass, compute);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuBindGroupRelease(group);
    wgpuBindGroupLayoutRelease(layout);
    wgpuComputePipelineRelease(compute);
    wgpuBufferRelease(data);
    unsigned dispatches = mock->dispatches;
    submit(s, encoder);
    assert(!s->errors && mock->dispatches == dispatches + 1);
    puts("PASS compact two-half storage buffer public Metal binding and dispatch; host call ledger");
}

static void metal_f16_consumer(state *s, mock_native *mock)
{
    metal_f16_capabilities();
    assert(wgpuDeviceHasFeature(s->device, WGPUFeatureName_ShaderF16));
    char *code = native_test_shader("../../../bench/inference-pipeline/kernels/matmul_logits_forward_f16accum.wgsl");
    WGPUComputePipeline compute = pipeline(s, code);
    free(code);
    assert(compute);
    WGPUBuffer buffers[4];
    WGPUBindGroupEntry entries[4] = {0};
    for (uint32_t i = 0; i < 4; ++i) {
        buffers[i] = buffer(s, 16, (i ? WGPUBufferUsage_Storage : WGPUBufferUsage_Uniform) |
            WGPUBufferUsage_CopyDst);
        entries[i] = (WGPUBindGroupEntry){.binding=i,.buffer=buffers[i],.size=16};
    }
    const uint32_t params[4] = {1,4,0,0};
    const float hidden[4] = {1,2,3,4}, weights[4] = {4,3,2,1};
    wgpuQueueWriteBuffer(s->queue, buffers[0], 0, params, sizeof(params));
    wgpuQueueWriteBuffer(s->queue, buffers[1], 0, hidden, sizeof(hidden));
    wgpuQueueWriteBuffer(s->queue, buffers[2], 0, weights, sizeof(weights));
    WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(compute, 0);
    WGPUBindGroupDescriptor descriptor = {.layout=layout,.entryCount=4,.entries=entries};
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(s->device, &descriptor);
    assert(group);
    wgpuBindGroupLayoutRelease(layout);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(pass, compute);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuBindGroupRelease(group);
    wgpuComputePipelineRelease(compute);
    for (size_t i = 0; i < 4; ++i) wgpuBufferRelease(buffers[i]);
    unsigned dispatches = mock->dispatches;
    submit(s, encoder);
    assert(!s->errors && mock->dispatches == dispatches + 1);
    puts("PASS unchanged f16 matmul public Metal module/pipeline/bind/dispatch ownership; host call ledger, not numeric GPU execution");
}
#endif
