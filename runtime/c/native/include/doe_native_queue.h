/* SPDX-License-Identifier: MIT
 * Doe queue synchronization state extension ABI (not webgpu.h). The signature
 * matches the retained Zig export in
 * runtime/zig/src/native/queue/doe_queue_submit_native.zig that the addon's
 * queueSyncInfo (doe-gpu nativeQueueSyncInfo) resolves. Listed in
 * ../EXTENSION_SYMBOLS.txt, never resolved by wgpuGetProcAddress. */
#ifndef DOE_NATIVE_QUEUE_H
#define DOE_NATIVE_QUEUE_H
#include "webgpu.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#if defined(__GNUC__) || defined(__clang__)
#define DOE_NATIVE_EXTENSION __attribute__((visibility("default")))
#else
#define DOE_NATIVE_EXTENSION
#endif

/* Queue synchronization state bits; 0 for a NULL queue or a non-Vulkan device. */
enum {
    DOE_NATIVE_QUEUE_SYNC_BACKEND_VULKAN = 1u << 0,
    DOE_NATIVE_QUEUE_SYNC_TIMELINE_SEMAPHORE = 1u << 1, /* Actual enabled timeline mode. */
    DOE_NATIVE_QUEUE_SYNC_FENCE_POOL = 1u << 2,         /* Proven-terminal tuples reused by command owner. */
    /* A submitted command record whose completion or failure has not yet been
     * observed by a public wait or the pending progress worker. */
    DOE_NATIVE_QUEUE_SYNC_DEFERRED_SUBMISSIONS = 1u << 3,
};
DOE_NATIVE_EXTENSION uint32_t doeNativeQueueSyncInfo(WGPUQueue queue);
#ifdef __cplusplus
}
#endif
#endif
