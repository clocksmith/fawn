/* SPDX-License-Identifier: MIT
 * Doe fused compute command extensions through the public library:
 * - one constructed command buffer runs its dispatches, in order, before its
 *   copy, after the caller released every input it was built from;
 * - a rejected construction returns NULL and reports one validation error;
 *   a NULL device or an empty batch returns NULL and reports nothing. */
#include "consumer.h"
#include "doe_native_commands.h"
#include <stdint.h>

static const char *const code =
    "@group(0) @binding(0) var<storage, read_write> data: array<u32>;\n"
    "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id: vec3u) {\n"
    "  data[id.x] = data[id.x] + 1u;\n"
    "}\n";

enum { WORDS = 4, BYTES = WORDS * 4 };
static const uint32_t input[WORDS] = {3, 10, 20, 40};

typedef struct fixture {
    WGPUComputePipeline pipeline;
    WGPUBuffer storage, readback;
    WGPUBindGroup group;
} fixture;

static fixture create_fixture(state *s)
{
    fixture f;
    f.pipeline = pipeline(s, code);
    f.storage = buffer(s, BYTES, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    f.readback = buffer(s, BYTES, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s->queue, f.storage, 0, input, sizeof(input));
    WGPUBindGroupEntry entry = {.binding = 0, .buffer = f.storage, .size = BYTES};
    f.group = group(s, f.pipeline, &entry, 1);
    return f;
}

static void release_fixture(fixture *f)
{
    if (f->group) wgpuBindGroupRelease(f->group);
    if (f->pipeline) wgpuComputePipelineRelease(f->pipeline);
    if (f->storage) wgpuBufferRelease(f->storage);
    wgpuBufferRelease(f->readback);
}

/* Releases every input, submits, and checks the readback holds each word
 * incremented once per dispatch. */
static void run(state *s, fixture *f, WGPUCommandBuffer commands, uint32_t dispatches)
{
    assert(commands);
    wgpuBindGroupRelease(f->group);
    wgpuComputePipelineRelease(f->pipeline);
    wgpuBufferRelease(f->storage);
    f->group = NULL;
    f->pipeline = NULL;
    f->storage = NULL;
    wgpuQueueSubmit(s->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    const uint32_t *output = map(s, f->readback, BYTES);
    for (size_t i = 0; i < WORDS; ++i) assert(output[i] == input[i] + dispatches);
    wgpuBufferUnmap(f->readback);
}

static void single(state *s)
{
    fixture f = create_fixture(s);
    WGPUCommandBuffer commands = doeNativeCreateComputeDispatchCopyCommandBuffer(s->device, f.pipeline,
        &f.group, 1, WORDS, 1, 1, f.storage, 0, f.readback, 0, BYTES);
    run(s, &f, commands, 1);
    release_fixture(&f);
}

static void batch(state *s)
{
    fixture f = create_fixture(s);
    WGPUComputePipeline pipelines[] = {f.pipeline, f.pipeline};
    WGPUBindGroup groups[2 * DOE_NATIVE_DISPATCH_BIND_GROUP_STRIDE] = {0};
    groups[0] = f.group;
    groups[DOE_NATIVE_DISPATCH_BIND_GROUP_STRIDE] = f.group;
    /* The first dispatch also names an empty slot 1, which stays unbound. */
    const uint32_t counts[] = {2, 1};
    const uint32_t dimensions[] = {WORDS, 1, 1, WORDS, 1, 1};
    WGPUCommandBuffer commands = doeNativeCreateComputeDispatchBatchCopyCommandBuffer(s->device, 2, pipelines,
        groups, counts, dimensions, f.storage, 0, f.readback, 0, BYTES);
    run(s, &f, commands, 2);
    release_fixture(&f);
}

static void rejected(state *s, WGPUCommandBuffer commands)
{
    assert(!commands);
    expect_validation(s);
}

static void failures(state *s)
{
    fixture f = create_fixture(s);
    WGPUBindGroup groups[DOE_NATIVE_DISPATCH_BIND_GROUP_STRIDE + 1] = {f.group};
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    rejected(s, doeNativeCreateComputeDispatchCopyCommandBuffer(s->device, f.pipeline, groups, 1, WORDS, 1, 1,
        f.storage, 0, NULL, 0, BYTES));
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    rejected(s, doeNativeCreateComputeDispatchCopyCommandBuffer(s->device, NULL, groups, 1, WORDS, 1, 1,
        f.storage, 0, f.readback, 0, BYTES));
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    rejected(s, doeNativeCreateComputeDispatchCopyCommandBuffer(s->device, f.pipeline, groups,
        DOE_NATIVE_DISPATCH_BIND_GROUP_STRIDE + 1, WORDS, 1, 1, f.storage, 0, f.readback, 0, BYTES));
    /* Group 0 is required by the pipeline layout. */
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    rejected(s, doeNativeCreateComputeDispatchCopyCommandBuffer(s->device, f.pipeline, groups, 0, WORDS, 1, 1,
        f.storage, 0, f.readback, 0, BYTES));
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    rejected(s, doeNativeCreateComputeDispatchCopyCommandBuffer(s->device, f.pipeline, groups, 1, UINT32_MAX, 1, 1,
        f.storage, 0, f.readback, 0, BYTES));
    /* The copy destination lacks CopyDst usage. */
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    WGPUBuffer storage_only = buffer(s, BYTES, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    rejected(s, doeNativeCreateComputeDispatchCopyCommandBuffer(s->device, f.pipeline, groups, 1, WORDS, 1, 1,
        f.storage, 0, storage_only, 0, BYTES));
    wgpuBufferRelease(storage_only);
    WGPUComputePipeline pipelines[] = {f.pipeline, NULL};
    WGPUBindGroup batch_groups[2 * DOE_NATIVE_DISPATCH_BIND_GROUP_STRIDE] = {f.group};
    const uint32_t counts[] = {1, 0};
    const uint32_t dimensions[] = {WORDS, 1, 1, WORDS, 1, 1};
    wgpuDevicePushErrorScope(s->device, WGPUErrorFilter_Validation);
    rejected(s, doeNativeCreateComputeDispatchBatchCopyCommandBuffer(s->device, 2, pipelines, batch_groups, counts,
        dimensions, f.storage, 0, f.readback, 0, BYTES));
    /* An empty batch has nothing to construct and reports nothing. */
    assert(!doeNativeCreateComputeDispatchBatchCopyCommandBuffer(s->device, 0, pipelines, batch_groups, counts,
        dimensions, f.storage, 0, f.readback, 0, BYTES));
    assert(!doeNativeCreateComputeDispatchCopyCommandBuffer(NULL, f.pipeline, groups, 1, WORDS, 1, 1,
        f.storage, 0, f.readback, 0, BYTES));
    release_fixture(&f);
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    single(&s);
    batch(&s);
    failures(&s);
    consumer_finish(&s);
    puts("fused commands: dispatch/copy order, batch, caller release, rejection");
    return 0;
}
