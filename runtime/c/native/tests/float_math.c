/* SPDX-License-Identifier: MIT
 * Independent exact finite table through public WGSL pipelines and GPU readback. */
#include "consumer.h"
#include "../../compiler/tests/float_math_fixture.h"

static const uint32_t expected[] = {
    UINT32_C(0x40200000), UINT32_C(0x40a00000), UINT32_C(0xc1200000), UINT32_C(0x3f800000),
    0, UINT32_C(0x3f800000), UINT32_C(0x3f800000), 0,
    0, 0, UINT32_C(0x3e800000), UINT32_C(0x3f800000),
    0, UINT32_C(0x3e200000), UINT32_C(0x3f000000), UINT32_C(0x3f800000),
    UINT32_C(0x3f800000), UINT32_C(0x3f580000), UINT32_C(0x3f000000), 0,
    UINT32_C(0x40a00000), UINT32_C(0x3f800000), UINT32_C(0x3f800000), UINT32_C(0x3f580000),
    123, UINT32_C(0x41200000), 123, UINT32_C(0x3f000000),
    12, 0, 1, UINT32_C(0x3f800000), UINT32_C(0x7f7fffff)
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
    WGPUComputePipeline program = pipeline(&s, float_math_module);
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
            fprintf(stderr, "float math allocation word%zu: %08x != %08x\n", i, actual[i], wanted);
        assert(actual[i] == wanted);
    }
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    consumer_finish(&s);
    printf("PASS public float math: fma/step/saturate/smoothstep, descending edges, constants, "
        "trace123/12/1, finite fused constant, 33 exact words, binding canaries and early releases %s\n",
        adapter_class_scope());
    return 0;
}
