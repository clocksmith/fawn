/* SPDX-License-Identifier: MIT
 * Independent exact finite table through public WGSL pipelines and GPU readback. */
#include "consumer.h"
#include <stdlib.h>
#include "../../compiler/tests/bitfield_fixture.h"

static const uint32_t expected[] = {
    0xf0u,0xfffffff0u,0xffffffffu,0xf0f00f0fu,0xf0f00f0fu,0,0,0,1,0xffffffffu,0,
    0xf0f00a5fu,0x12345678u,0xf0f00f0fu,0xf0f00f0fu,0x80000000u,
    0,0xffffffffu,0xffffffffu,0xfffffff8u,0x100u,0xffff00ffu,0x80000300u,0x1234aa78u,
    0xff000000u,0x00ffffffu,0x7f000000u,0x80ffffffu,
    0xfcu,0xfffffffcu,0xffffffffu,0xffffu,0xffffffffu,0xfffffff8u,0xffu,0,
    0x1234ab78u,1234,0x56u,123,123
};

int main(void)
{
    state s = {0};
    consumer_init(&s);
    WGPULimits limits = WGPU_LIMITS_INIT;
    assert(wgpuDeviceGetLimits(s.device, &limits) == WGPUStatus_Success);
    const size_t prefix = limits.minStorageBufferOffsetAlignment;
    assert(prefix >= sizeof(uint32_t) && prefix % sizeof(uint32_t) == 0);
    const size_t bytes = prefix + sizeof(expected) + 8;
    uint32_t *initial = malloc(bytes);
    assert(initial);
    for (size_t i = 0; i < bytes / sizeof(uint32_t); ++i) initial[i] = UINT32_C(0xdeadbeef);
    WGPUBuffer data = buffer(&s, bytes, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    WGPUBuffer readback = buffer(&s, bytes, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s.queue, data, 0, initial, bytes);
    free(initial);
    WGPUComputePipeline program = pipeline(&s, bitfield_module);
    const WGPUBindGroupEntry entry = {.binding = 0, .buffer = data, .offset = prefix, .size = sizeof(expected)};
    WGPUBindGroup bindings = group(&s, program, &entry, 1);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device, NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    assert(encoder && pass);
    wgpuComputePassEncoderSetPipeline(pass, program);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindings, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, data, 0, readback, 0, bytes);
    wgpuComputePipelineRelease(program);
    wgpuBindGroupRelease(bindings);
    wgpuBufferRelease(data);
    submit(&s, encoder);
    const uint32_t *actual = map(&s, readback, bytes);
    for (size_t i = 0; i < bytes / sizeof(uint32_t); ++i) {
        const size_t start = prefix / sizeof(uint32_t);
        const uint32_t wanted = i < start || i >= start + sizeof(expected) / sizeof(*expected) ?
            UINT32_C(0xdeadbeef) : expected[i - start];
        if (actual[i] != wanted)
            fprintf(stderr, "bitfield allocation word%zu: %08x != %08x\n", i, actual[i], wanted);
        assert(actual[i] == wanted);
    }
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    consumer_finish(&s);
    printf("PASS public bitfield: signed/unsigned extraction and insertion, scalar/vector/constant, "
        "runtime clamps, full/zero width, trace1234/123, 41 exact words, binding canaries and early releases %s\n",
        adapter_class_scope());
    return 0;
}
