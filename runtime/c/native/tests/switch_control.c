/* SPDX-License-Identifier: MIT
 * Exact public compute oracle for switch selection, nested breaks, outer-loop
 * continue and single selector evaluation. CPU runs are not GPU qualification. */
#include "consumer.h"

int main(void)
{
    state s = {0};
    consumer_init(&s);
    WGPUComputePipeline compute = pipeline(&s,
        "@group(0) @binding(0) var<storage,read_write> out:array<u32>;"
        "var<private> calls:u32;"
        "fn selector(x:u32)->u32 {calls+=1u;return x;}"
        "fn choose(x:u32)->u32 {switch x {"
        "default:{return 99u;}"
        "case 0u,1u:{return 11u;}"
        "case 2u:{var v=0u;loop {v=20u;break;}"
        "switch x+1u {case 3u:{v+=3u;}default:{v=24u;}}return v;}"
        "case 3u:{var v=0u;for(var i=0u;i<4u;i++){"
        "switch i {case 0u:{continue;}case 1u:{v+=5u;}default:{break;}}"
        "v+=1u;}return v;}}}"
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id:vec3u){"
        "out[id.x]=choose(selector(id.x));out[id.x+8u]=calls;}");
    WGPUBuffer output = buffer(&s, 64, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(&s, 64, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUBindGroupEntry entry = {.binding=0,.buffer=output,.size=64};
    WGPUBindGroup bindings = group(&s, compute, &entry, 1);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    wgpuComputePassEncoderSetPipeline(pass, compute);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindings, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 8, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, output, 0, readback, 0, 64);
    wgpuBindGroupRelease(bindings);
    wgpuComputePipelineRelease(compute);
    wgpuBufferRelease(output);
    submit(&s, encoder);
    const uint32_t expected[16] = {11,11,23,8,99,99,99,99,1,1,1,1,1,1,1,1};
    const uint32_t *actual = map(&s, readback, 64);
    for (size_t i = 0; i < 16; i++) assert(actual[i] == expected[i]);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    consumer_finish(&s);
    puts("switch control: exact 16-word compute oracle passed");
    return 0;
}
