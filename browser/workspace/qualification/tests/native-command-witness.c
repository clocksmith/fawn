/* SPDX-License-Identifier: MIT
 * Private runtime overlay test. Two distinct commands share the same native batch.
 * The callback publishes status; reading the witness after it adds no wait. */
#define _POSIX_C_SOURCE 200809L
#include "consumer.h"
#include "doe_native_queue.h"
#include <inttypes.h>
#include <stdatomic.h>
#include <time.h>

typedef struct observed {
    WGPUQueue queue;
    WGPUCommandBuffer commands[2];
    uint64_t serials[2];
    uint32_t states[2];
    size_t count;
    atomic_uint done;
} observed;

static void observed_done(WGPUQueueWorkDoneStatus status, WGPUStringView message, void *data, void *unused)
{
    (void)message;
    (void)unused;
    observed *row = data;
    assert(status == WGPUQueueWorkDoneStatus_Success);
    atomic_store(&row->done, 1);
}

static void done(state *s, observed *row)
{
    assert(wgpuQueueOnSubmittedWorkDone(s->queue, (WGPUQueueWorkDoneCallbackInfo){
        .mode = WGPUCallbackMode_AllowSpontaneous, .callback = observed_done, .userdata1 = row}).id);
    for (unsigned i = 0; !atomic_load(&row->done); ++i) {
        assert(i < 30000);
        wgpuInstanceProcessEvents(s->instance);
        nanosleep(&(struct timespec){.tv_nsec = 100000}, NULL);
    }
    for (size_t i = 0; i < row->count; ++i)
        row->states[i] = doeNativeVulkanCommandWitnessV1(row->queue, row->commands[i], 0, &row->serials[i]);
}

static void arm(WGPUQueue queue, WGPUCommandBuffer command, uint32_t want)
{
    uint64_t serial = UINT64_MAX;
    uint32_t got = doeNativeVulkanCommandWitnessV1(queue, command, 1, &serial);
    if (got != want || serial != 0)
        fprintf(stderr, "arm expected %u, received %u at serial %" PRIu64 "\n", want, got, serial);
    assert(got == want && serial == 0);
}

static WGPUCommandBuffer encode(state *s, WGPUBuffer data, WGPUBuffer out,
                                WGPUComputePipeline pipe, WGPUBindGroup bind, bool compute)
{
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    if (compute) dispatch(enc, pipe, bind);
    else wgpuCommandEncoderCopyBufferToBuffer(enc, data, 0, out, 0, 16);
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(enc, NULL);
    wgpuCommandEncoderRelease(enc);
    assert(command);
    return command;
}

int main(void)
{
    assert(!adapter_class_physical());
    state s = {0}, other = {0};
    consumer_init(&s);
    consumer_init(&other);
    WGPUBuffer data = buffer(&s, 16, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    WGPUBuffer output = buffer(&s, 16, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);
    WGPUComputePipeline pipe = pipeline(&s, "@group(0) @binding(0) var<storage,read_write> a:array<u32>;"
        "@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) i:vec3<u32>){a[i.x]=a[i.x]+7u+i.x;}");
    WGPUBindGroupEntry entry = {.binding=0, .buffer=data, .size=16};
    WGPUBindGroup bind = group(&s, pipe, &entry, 1);
    uint64_t last = 0;
    for (unsigned trial = 0; trial < 3; ++trial) {
        uint32_t seed[] = {2 + trial, 4 + trial, 6 + trial, 8 + trial};
        wgpuQueueWriteBuffer(s.queue, data, 0, seed, 16);
        observed row = {.queue=s.queue, .count=2};
        row.commands[0] = encode(&s, data, output, pipe, bind, true);
        row.commands[1] = encode(&s, data, output, pipe, bind, false);
        uint64_t before_serial = UINT64_MAX;
        assert(doeNativeVulkanCommandWitnessV1(s.queue, row.commands[0], 0, &before_serial) == 0);
        assert(before_serial == 0);
        arm(other.queue, row.commands[0], 0);
        arm(s.queue, row.commands[0], 1);
        assert(doeNativeVulkanCommandWitnessV1(s.queue, row.commands[0], 0, &before_serial) == 1);
        assert(before_serial == 0);
        arm(s.queue, row.commands[0], 0);
        arm(s.queue, row.commands[1], 1);
        wgpuQueueSubmit(s.queue, 2, row.commands);
        done(&s, &row);
        assert(row.states[0] == 3 && row.serials[0] > last);
        assert(row.states[1] == 4 && !row.serials[1]);
        last = row.serials[0];
        const uint32_t *values = map(&s, output, 16);
        for (unsigned j = 0; j < 4; ++j) assert(values[j] == seed[j] + 7 + j);
        wgpuBufferUnmap(output);
        /* Reading an older success still refers to the first submit. A new arm fails. */
        arm(s.queue, row.commands[0], 0);
        wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_Validation);
        wgpuQueueSubmit(s.queue, 1, row.commands);
        expect_validation(&s);
        printf("valid batch %u: specific compute serial %" PRIu64 ", copy-only unqualified\n", trial, last);
        wgpuCommandBufferRelease(row.commands[0]);
        wgpuCommandBufferRelease(row.commands[1]);
    }
    observed failed = {.queue=s.queue, .count=1};
    failed.commands[0] = encode(&s, data, output, pipe, bind, true);
    arm(s.queue, failed.commands[0], 1);
    wgpuBufferDestroy(data);
    wgpuDevicePushErrorScope(s.device, WGPUErrorFilter_Validation);
    wgpuQueueSubmit(s.queue, 1, failed.commands);
    expect_validation(&s);
    done(&s, &failed);
    assert(failed.states[0] == 4 && !failed.serials[0]);
    puts("rejected compute had no serial although its queue callback succeeded");
    wgpuCommandBufferRelease(failed.commands[0]);
    wgpuBindGroupRelease(bind);
    wgpuComputePipelineRelease(pipe);
    wgpuBufferRelease(output);
    wgpuBufferRelease(data);
    consumer_finish(&other);
    consumer_finish(&s);
    return 0;
}
