/* SPDX-License-Identifier: MIT
 * Mapped-at-creation primary buffers may be recorded before unmapping. Queue
 * submission, not binding or Finish, requires them to be unmapped. */
#include "consumer.h"

static void binding_timing(state *s, bool uniform, unsigned outcome)
{
    const char *code = uniform ?
        "@group(0) @binding(0) var<uniform> input:vec4<u32>;"
        "@group(0) @binding(1) var<storage,read_write> output:array<u32>;"
        "@compute @workgroup_size(1) fn main(){output[0]=input.x+7u;}" :
        "@group(0) @binding(0) var<storage,read> input:array<u32>;"
        "@group(0) @binding(1) var<storage,read_write> output:array<u32>;"
        "@compute @workgroup_size(1) fn main(){output[0]=input[0]+7u;}";
    WGPUComputePipeline compute = pipeline(s, code);
    WGPUBufferDescriptor desc = {.size=16,.mappedAtCreation=true,
        .usage=uniform ? WGPUBufferUsage_Uniform : WGPUBufferUsage_Storage};
    WGPUBuffer input = wgpuDeviceCreateBuffer(s->device, &desc);
    assert(input);
    uint32_t *initial = wgpuBufferGetMappedRange(input, 0, 16);
    assert(initial);
    initial[0] = 35;
    WGPUBuffer output = buffer(s, 4, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(s, 4, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUBindGroupEntry entries[] = {{.binding=0,.buffer=input,.size=16},
        {.binding=1,.buffer=output,.size=4}};
    WGPUBindGroup bindings = group(s, compute, entries, 2);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(pass, compute);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindings, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, output, 0, readback, 0, 4);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    assert(commands && !s->errors);
    wgpuCommandEncoderRelease(encoder);
    /* Unmap only AFTER recording and Finish, retaining the original identity. */
    if (outcome != 1) wgpuBufferUnmap(input);
    if (outcome == 2) wgpuBufferDestroy(input);
    wgpuBindGroupRelease(bindings);
    wgpuComputePipelineRelease(compute);
    wgpuBufferRelease(output);
    if (outcome) wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    else wgpuBufferRelease(input); /* Command lease survives early public release. */
    wgpuQueueSubmit(s->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    if (outcome) {
        expect_validation(s);
        if (outcome == 1) wgpuBufferUnmap(input);
        wgpuBufferRelease(input);
    } else {
        const uint32_t *actual = map(s, readback, 4);
        assert(actual[0] == 42);
        wgpuBufferUnmap(readback);
    }
    wgpuBufferRelease(readback);
    assert(!s->errors);
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    for (unsigned uniform = 0; uniform < 2; uniform++)
        for (unsigned outcome = 0; outcome < 3; outcome++)
            binding_timing(&s, uniform != 0, outcome);
    consumer_finish(&s);
    puts("native mapped binding timing: 6 cases passed, including exact compute readback");
    return 0;
}
