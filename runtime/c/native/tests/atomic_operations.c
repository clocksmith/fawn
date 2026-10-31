/* SPDX-License-Identifier: MIT
 * Unchanged retained WGSL through the public C module/pipeline/queue boundary. */
#include "consumer.h"
#include "shader_fixture.h"

enum { RESULT_WORDS = 336, TOTAL_WORDS = 348, READBACK_BYTES = 1408, PREFIX_WORDS = 2 };

/* Retained all_ops oracle from compiler/tests/test_atomic_vulkan.c. These
 * finite old-value/result tables are independent of every shader emitter.
 * Six storage cells give every lane its own cell. HLSL and SPIR-V clamp storage
 * indices (compiler/targets/hlsl/AGENTS.md, compiler/src/spirv_clamp.c): lanes
 * 1..5 use cells 0..4 and lane 0's -1 clamps to the last cell, 5. The MSL guard
 * drops lane 0's storage operations, leaving cell 5's seed. Workgroup indices
 * keep their guard on every target. */
static void expected_words(size_t binding,uint32_t *words,size_t count)
{
    const bool clamps = adapter_class_backend() != WGPUBackendType_Metal;
    if (binding < 2) {
        assert(count == 6);
        for (size_t i = 0; i < count; ++i) words[i] = 11;
        if (!clamps) words[5] = binding == 0 ? (uint32_t)-3 : 15u;
        return;
    }
    assert(count == RESULT_WORDS);
    const int32_t signed_values[13] = {-7,-4,-6,5,-5,3,7,1,9,1,11,0,11};
    const uint32_t unsigned_values[13] = {UINT32_MAX - 2u,2,UINT32_MAX,
        UINT32_MAX,9,0,3,5,9,1,11,0,11};
    for (size_t lane = 0; lane < 6; ++lane) {
        for (size_t root = 0; root < 4; ++root) {
            size_t base = lane * 56 + root * 14;
            for (size_t item = 0; item < 14; ++item) words[base + item] = 0;
            if ((root >= 2 || !clamps) && lane == 0) continue;
            if (root >= 2 && lane == 5) continue;
            const size_t cell = lane == 0 ? 5u : lane - 1u;
            if (root == 0) words[base] = (uint32_t)((int32_t)cell - 8);
            if (root == 1) words[base] = (uint32_t)(cell + 10u);
            for (size_t item = 0; item < 13; ++item)
                words[base + 1 + item] = root % 2 == 0 ? (uint32_t)signed_values[item] : unsigned_values[item];
        }
    }
}
int main(int argc,char **argv)
{
    assert(argc == 1 || argc == 2);
    const char *path = argc == 2 ? argv[1] : "../compiler/tests/atomic_operations.wgsl";
    char *source = native_test_shader(path);
    state s = {0};
    consumer_init(&s);
    WGPUComputePipeline program = pipeline(&s,source);
    free(source);
    const size_t sizes[3] = {24,24,1344};
    WGPUBuffer buffers[3];
    WGPUBindGroupEntry entries[3];
    uint32_t expected[TOTAL_WORDS];
    size_t cursor = 0;
    for (size_t binding = 0; binding < 3; ++binding) {
        buffers[binding] = buffer(&s,sizes[binding],
            WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc|WGPUBufferUsage_CopyDst);
        uint32_t seeds[RESULT_WORDS];
        const size_t count = sizes[binding] / sizeof(*seeds);
        for (size_t i = 0; i < count; ++i)
            seeds[i] = binding == 0 ? (uint32_t)((int32_t)i - 8) :
                binding == 1 ? (uint32_t)(10u + i) : UINT32_C(0xdeadbeef);
        wgpuQueueWriteBuffer(s.queue,buffers[binding],0,seeds,sizes[binding]);
        entries[binding] = (WGPUBindGroupEntry){.binding=(uint32_t)binding,
            .buffer=buffers[binding],.size=sizes[binding]};
        expected_words(binding,expected + cursor,count);
        cursor += count;
    }
    assert(cursor == TOTAL_WORDS);
    WGPUBindGroup bindings = group(&s,program,entries,3);
    WGPUBuffer readback = buffer(&s,READBACK_BYTES,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    uint32_t sentinels[READBACK_BYTES / 4];
    for (size_t i = 0; i < sizeof(sentinels)/sizeof(*sentinels); ++i) sentinels[i] = UINT32_C(0xdeadbeef);
    wgpuQueueWriteBuffer(s.queue,readback,0,sentinels,sizeof(sentinels));
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s.device,NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder,NULL);
    assert(pass);
    wgpuComputePassEncoderSetPipeline(pass,program);
    wgpuComputePassEncoderSetBindGroup(pass,0,bindings,0,NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    uint64_t offset = PREFIX_WORDS * sizeof(uint32_t);
    for (size_t binding = 0; binding < 3; ++binding) {
        wgpuCommandEncoderCopyBufferToBuffer(encoder,buffers[binding],0,readback,offset,sizes[binding]);
        offset += sizes[binding];
        wgpuBufferRelease(buffers[binding]);
    }
    assert(offset == READBACK_BYTES - 8);
    wgpuComputePipelineRelease(program);
    wgpuBindGroupRelease(bindings);
    submit(&s,encoder);
    const uint32_t *actual = map(&s,readback,READBACK_BYTES);
    for (size_t i = 0; i < READBACK_BYTES / 4; ++i) {
        const uint32_t wanted = i < PREFIX_WORDS || i >= PREFIX_WORDS + TOTAL_WORDS ?
            UINT32_C(0xdeadbeef) : expected[i - PREFIX_WORDS];
        if (actual[i] != wanted) fprintf(stderr,"atomic word%zu: %08x != %08x\n",i,actual[i],wanted);
        assert(actual[i] == wanted);
    }
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    consumer_finish(&s);
    printf("PASS public all11 atomics: unchanged retained WGSL, signed/unsigned storage/workgroup old values, wrap, min/max, exchange success/failure, clamped storage and guarded workgroup OOB, 348 exact words, prefix/suffix and early releases %s\n",adapter_class_scope());
    return 0;
}
