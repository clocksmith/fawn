/* SPDX-License-Identifier: MIT
 * Public shader ownership: large valid bodies and exact long entry identities.
 * Fixed literal output checks do not infer physical backend qualification. */
#include "consumer.h"

static void execute(state *s, WGPUShaderModule module, const char *entry_name, uint32_t expected)
{
    WGPUComputePipelineDescriptor descriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    descriptor.compute.module = module;
    descriptor.compute.entryPoint = (WGPUStringView){entry_name, strlen(entry_name)};
    WGPUComputePipeline compute = wgpuDeviceCreateComputePipeline(s->device, &descriptor);
    assert(compute && !s->errors);
    WGPUBuffer output = buffer(s, 4, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(s, 4, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUBindGroupEntry entry = {.binding=0,.buffer=output,.size=4};
    WGPUBindGroup bindings = group(s, compute, &entry, 1);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(pass, compute);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindings, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, output, 0, readback, 0, 4);
    wgpuBindGroupRelease(bindings);
    wgpuComputePipelineRelease(compute);
    wgpuBufferRelease(output);
    submit(s, encoder);
    const uint32_t *actual = map(s, readback, 4);
    assert(actual[0] == expected);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    char first[98], second[98];
    memset(first, 'a', sizeof(first) - 1);
    first[sizeof(first) - 1] = 0;
    memcpy(second, first, sizeof(first));
    second[sizeof(second) - 2] = 'b';
    enum { UPDATES = 2200, SOURCE_BYTES = 65536 };
    char *source = malloc(SOURCE_BYTES);
    assert(source);
    int prefix = snprintf(source, SOURCE_BYTES,
        "@group(0) @binding(0) var<storage,read_write> output:array<u32>;"
        "@compute @workgroup_size(1) fn %s(){var value=0u;", first);
    assert(prefix > 0 && prefix < SOURCE_BYTES);
    size_t used = (size_t)prefix;
    static const char update[] = "value=value+1u;";
    for (size_t i = 0; i < UPDATES; i++) {
        assert(sizeof(update) <= SOURCE_BYTES - used);
        memcpy(source + used, update, sizeof(update) - 1);
        used += sizeof(update) - 1;
    }
    int tail = snprintf(source + used, SOURCE_BYTES - used,
        "output[0]=value;} @compute @workgroup_size(1) fn %s(){output[0]=733u;}", second);
    assert(tail > 0 && (size_t)tail < SOURCE_BYTES - used);
    used += (size_t)tail;
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = (WGPUStringView){source, used};
    WGPUShaderModuleDescriptor descriptor = {.nextInChain=&wgsl.chain};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(s.device, &descriptor);
    assert(module && !s.errors);
    memset(source, '?', used); /* Compilation owns its immutable source snapshot. */
    free(source);
    execute(&s, module, first, UPDATES);
    execute(&s, module, second, 733);
    wgpuShaderModuleRelease(module);
    consumer_finish(&s);
    puts("native shader spans: 2200 updates and two distinct 97-byte entry names passed");
    return 0;
}
