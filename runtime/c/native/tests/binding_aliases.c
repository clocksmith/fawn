/* SPDX-License-Identifier: MIT
 * Public aliases: canonical usage/ranges, native descriptor state and leases. */
#include "consumer.h"
#include <stdlib.h>

static WGPUBindGroup group_at(state *s, WGPUComputePipeline compute, uint32_t index,
    const WGPUBindGroupEntry *entries, size_t count)
{
    WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(compute, index);
    assert(layout);
    WGPUBindGroupDescriptor descriptor = {.layout=layout, .entryCount=count, .entries=entries};
    WGPUBindGroup result = wgpuDeviceCreateBindGroup(s->device, &descriptor);
    wgpuBindGroupLayoutRelease(layout);
    assert(result);
    return result;
}

static uint32_t alias_stride(state *s)
{
    WGPULimits limits = WGPU_LIMITS_INIT;
    assert(wgpuDeviceGetLimits(s->device, &limits) == WGPUStatus_Success);
    uint32_t stride = limits.minStorageBufferOffsetAlignment;
    if (stride < limits.minUniformBufferOffsetAlignment) stride = limits.minUniformBufferOffsetAlignment;
    if (stride < 64) stride = 64;
    assert(stride && !(stride & (stride - 1)));
    return stride;
}

static void readonly_aliases(state *s, uint32_t stride)
{
    WGPUComputePipeline compute = pipeline(s,
        "@group(0) @binding(999) var<uniform> params:vec4<u32>;"
        "@group(0) @binding(17) var<storage,read_write> result:array<u32>;"
        "@group(3) @binding(998) var<storage,read> first:array<u32>;"
        "@group(3) @binding(999) var<storage,read> overlap:array<u32>;"
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id:vec3<u32>,"
        "@builtin(num_workgroups) groups:vec3<u32>){"
        "let i=id.x+groups.x*(id.y+groups.y*id.z);"
        "result[i]=params.x+params.y*i+first[i]+overlap[i%4u]*3u"
        "+arrayLength(&first)*5u+arrayLength(&overlap)*7u"
        "+groups.x*101u+groups.y*103u+groups.z*107u;}");
    uint64_t source_size = (uint64_t)stride + 16;
    size_t count = (size_t)(source_size / sizeof(uint32_t));
    uint32_t *input = calloc(count, sizeof(*input));
    assert(input);
    for (size_t i = 0; i < count; ++i) input[i] = (uint32_t)i * 13u + 5u;
    input[0] = 7;
    input[1] = 11;
    input[2] = 13;
    input[3] = 17;
    const uint32_t tail[4] = {101, 203, 307, 409};
    memcpy(input + stride / sizeof(uint32_t), tail, sizeof(tail));
    WGPUBuffer source = buffer(s, source_size,
        WGPUBufferUsage_Uniform | WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue, source, 0, input, (size_t)source_size);
    enum { RESULT_WORDS = 12 };
    WGPUBuffer output = buffer(s, RESULT_WORDS * sizeof(uint32_t),
        WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(s, RESULT_WORDS * sizeof(uint32_t),
        WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUBindGroupEntry group0_entries[2] = {
        {.binding=999, .buffer=source, .size=16},
        {.binding=17, .buffer=output, .size=RESULT_WORDS * sizeof(uint32_t)}
    };
    WGPUBindGroupEntry group3_entries[2] = {
        {.binding=998, .buffer=source, .size=source_size},
        {.binding=999, .buffer=source, .offset=stride, .size=16}
    };
    WGPUBindGroup group0 = group_at(s, compute, 0, group0_entries, 2);
    WGPUBindGroup group3 = group_at(s, compute, 3, group3_entries, 2);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    assert(encoder && pass);
    wgpuComputePassEncoderSetPipeline(pass, compute);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group0, 0, NULL);
    wgpuComputePassEncoderSetBindGroup(pass, 3, group3, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 3, 2, 2);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, output, 0, readback, 0,
        RESULT_WORDS * sizeof(uint32_t));
    /* The existing command owner retains every resource before native lowering. */
    wgpuBindGroupRelease(group0);
    wgpuBindGroupRelease(group3);
    wgpuComputePipelineRelease(compute);
    wgpuBufferRelease(source);
    wgpuBufferRelease(output);
    submit(s, encoder);
    const uint32_t *actual = map(s, readback, RESULT_WORDS * sizeof(uint32_t));
    for (uint32_t i = 0; i < RESULT_WORDS; ++i) {
        uint32_t expected = 7u + 11u*i + input[i] + tail[i%4u]*3u + (uint32_t)count*5u +
            4u*7u + 3u*101u + 2u*103u + 2u*107u;
        assert(actual[i] == expected);
    }
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    free(input);
}

static void writable_ranges(state *s, uint32_t stride)
{
    WGPUComputePipeline compute = pipeline(s,
        "@group(3) @binding(0) var<storage,read_write> left:array<u32>;"
        "@group(3) @binding(999) var<storage,read_write> right:array<u32>;"
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id:vec3<u32>,"
        "@builtin(num_workgroups) groups:vec3<u32>){"
        "left[id.x]=1000u+id.x*7u+arrayLength(&right)+groups.x;"
        "right[id.x]=2000u+id.x*11u+arrayLength(&left)+groups.x*3u;}");
    WGPUBuffer data = buffer(s, (uint64_t)stride + 16,
        WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    WGPUBuffer readback = buffer(s, 32, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    WGPUBindGroupEntry entries[2] = {
        {.binding=0, .buffer=data, .size=16},
        {.binding=999, .buffer=data, .size=16}
    };
    WGPUBindGroup overlapping = group_at(s, compute, 3, entries, 2);
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    assert(encoder && pass);
    wgpuComputePassEncoderSetPipeline(pass, compute);
    wgpuComputePassEncoderSetBindGroup(pass, 3, overlapping, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 4, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    finish_invalid(encoder);
    wgpuCommandEncoderRelease(encoder);
    expect_validation(s);
    wgpuBindGroupRelease(overlapping);

    entries[1].offset = stride;
    WGPUBindGroup disjoint = group_at(s, compute, 3, entries, 2);
    encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    assert(encoder && pass);
    wgpuComputePassEncoderSetPipeline(pass, compute);
    wgpuComputePassEncoderSetBindGroup(pass, 3, disjoint, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 4, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, data, 0, readback, 0, 16);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, data, stride, readback, 16, 16);
    wgpuBindGroupRelease(disjoint);
    wgpuComputePipelineRelease(compute);
    wgpuBufferRelease(data);
    submit(s, encoder);
    const uint32_t *actual = map(s, readback, 32);
    for (uint32_t i = 0; i < 4; ++i) {
        assert(actual[i] == 1008u + i*7u);
        assert(actual[4+i] == 2016u + i*11u);
    }
    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
}

int main(void)
{
    setbuf(stdout, NULL);
    state s = {0};
    consumer_init(&s);
    uint32_t stride = alias_stride(&s);
    readonly_aliases(&s, stride);
    writable_ranges(&s, stride);
    assert(s.validation_errors == 1 && s.mapped == 2);
    consumer_finish(&s);
    printf("PASS public binding aliases: Uniform+readonly storage and overlapping readonly views, sparse groups/binding999, num_workgroups/arrayLength, disjoint writable ranges, overlapping writable rejection and early releases; exact20words %s\n",
        adapter_class_scope());
}
