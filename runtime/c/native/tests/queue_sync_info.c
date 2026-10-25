/* SPDX-License-Identifier: MIT
 * Doe queue synchronization state extension on software Vulkan: the queue
 * reports Vulkan and its existing fence pool, never a timeline semaphore, and deferred
 * submissions exactly while an accepted submission's completion is
 * unobserved. A GNU wrapper at the real fence poll withholds that
 * observation, so the deferred state is deterministic rather than a race with
 * the pending progress worker; work done and host buffer access then clear it. */
#include "doe_vk.h"
#include "consumer.h"
#include "doe_native_queue.h"
#include <stdatomic.h>
#include <stdint.h>

static atomic_bool withheld;
doe_vk_status __real_doe_vk_commands_poll(doe_vk_commands *, doe_vk_error *);
doe_vk_status __wrap_doe_vk_commands_poll(doe_vk_commands *commands, doe_vk_error *error)
{
    if (atomic_load(&withheld)) return DOE_VK_PENDING;
    return __real_doe_vk_commands_poll(commands, error);
}

enum { WORDS = 64, BYTES = WORDS * 4 };

static bool vulkan(state *s)
{
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    assert(wgpuAdapterGetInfo(s->adapter, &info) == WGPUStatus_Success);
    const bool result = info.backendType == WGPUBackendType_Vulkan;
    wgpuAdapterInfoFreeMembers(info);
    return result;
}

static void copy(state *s, WGPUBuffer source, WGPUBuffer destination)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(s->device, NULL);
    assert(encoder);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, source, 0, destination, 0, BYTES);
    submit(s, encoder);
}

int main(void)
{
    state s = {0};
    consumer_init(&s);
    assert(!doeNativeQueueSyncInfo(NULL));
    if (!vulkan(&s)) {
        assert(!doeNativeQueueSyncInfo(s.queue));
        consumer_finish(&s);
        puts("queue sync info: non-Vulkan queue reports no Vulkan state");
        return 0;
    }
    const uint32_t settled = DOE_NATIVE_QUEUE_SYNC_BACKEND_VULKAN | DOE_NATIVE_QUEUE_SYNC_FENCE_POOL;
    const uint32_t deferred = settled | DOE_NATIVE_QUEUE_SYNC_DEFERRED_SUBMISSIONS;
    assert(doeNativeQueueSyncInfo(s.queue) == settled);
    uint32_t input[WORDS];
    for (size_t i = 0; i < WORDS; ++i) input[i] = (uint32_t)i * 7u + 1u;
    WGPUBuffer source = buffer(&s, BYTES, WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    WGPUBuffer readback = buffer(&s, BYTES, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(s.queue, source, 0, input, sizeof(input));
    work_done(&s);
    assert(doeNativeQueueSyncInfo(s.queue) == settled);

    /* Work done observes every withheld submission. */
    atomic_store(&withheld, true);
    copy(&s, source, readback);
    assert(doeNativeQueueSyncInfo(s.queue) == deferred);
    copy(&s, source, readback);
    assert(doeNativeQueueSyncInfo(s.queue) == deferred);
    atomic_store(&withheld, false);
    work_done(&s);
    assert(doeNativeQueueSyncInfo(s.queue) == settled);

    /* Host access to the copied buffer observes the submission using it. */
    atomic_store(&withheld, true);
    copy(&s, source, readback);
    assert(doeNativeQueueSyncInfo(s.queue) == deferred);
    atomic_store(&withheld, false);
    const uint32_t *output = map(&s, readback, BYTES);
    assert(doeNativeQueueSyncInfo(s.queue) == settled);
    assert(!memcmp(output, input, sizeof(input)));
    wgpuBufferUnmap(readback);

    wgpuBufferRelease(readback);
    wgpuBufferRelease(source);
    consumer_finish(&s);
    puts("queue sync info: Vulkan state, deferred while unobserved, cleared by work done and buffer access");
    return 0;
}
