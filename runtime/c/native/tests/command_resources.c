/* SPDX-License-Identifier: MIT
 * Public command retention beyond a private fixed resource-array capacity. */
#include "consumer.h"

static void dispatch_metadata_resources(state *s)
{
    WGPUComputePipeline compute = pipeline(s,
        "@group(0) @binding(0) var<storage,read_write> counter:u32;"
        "@compute @workgroup_size(1) fn main(@builtin(num_workgroups) n:vec3<u32>){"
        "counter+=n.x+2u*n.y+4u*n.z;}");
    WGPUBuffer counter = buffer(s, 4, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(s, 4, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUBindGroupEntry entry = {.binding=0, .buffer=counter, .size=4};
    WGPUBindGroup bound = group(s, compute, &entry, 1);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    assert(encoder && pass);
    wgpuComputePassEncoderSetPipeline(pass, compute);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bound, 0, NULL);
    /* Each lowered D3D12 dispatch owns its real metadata upload/default buffers.
     * Thirty-three dispatches exceed 64 native resources before caller data. */
    for (unsigned i = 0; i < 33; ++i)
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, counter, 0, readback, 0, 4);
    wgpuBindGroupRelease(bound);
    wgpuComputePipelineRelease(compute);
    wgpuBufferRelease(counter);
    submit(s, encoder);
    const uint32_t *actual = map(s, readback, 4);
    assert(actual[0] == 231u);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}

static void distinct_copy_resources(state *s)
{
    enum { COPIES = 129 };
    uint32_t expected[COPIES];
    WGPUBuffer readback = buffer(s, sizeof(expected), WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    assert(encoder);
    for (uint32_t i = 0; i < COPIES; ++i) {
        expected[i] = (UINT32_C(0x9e3779b9) * (i + 1u)) ^ UINT32_C(0xa5c3e217);
        WGPUBuffer source = buffer(s, sizeof(expected[i]), WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
        wgpuQueueWriteBuffer(s->queue, source, 0, &expected[i], sizeof(expected[i]));
        wgpuCommandEncoderCopyBufferToBuffer(encoder, source, 0, readback,
            (uint64_t)i * sizeof(expected[i]), sizeof(expected[i]));
        /* Every public source is gone before submission. The existing command
         * owner, not a test-side holder, must keep all 129 allocations alive. */
        wgpuBufferRelease(source);
    }
    submit(s, encoder);
    const uint32_t *actual = map(s, readback, sizeof(expected));
    for (size_t i = 0; i < COPIES; ++i) assert(actual[i] == expected[i]);
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}

int main(void)
{
    setbuf(stdout, NULL);
    state s = {0};
    consumer_init(&s);
    dispatch_metadata_resources(&s);
    distinct_copy_resources(&s);
    assert(s.mapped == 2);
    consumer_finish(&s);
    printf("PASS public command resources: 33 metadata-bearing dispatches accumulate 231, 129 distinct four-byte copies in one command, exact 130 words and early releases %s\n",
        adapter_class_scope());
}
