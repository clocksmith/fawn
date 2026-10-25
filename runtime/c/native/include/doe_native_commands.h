/* SPDX-License-Identifier: MIT
 * Doe fused compute command extension ABI (not webgpu.h).
 * Signatures match the retained Zig exports in
 * runtime/zig/src/native/compute/doe_compute_fast.zig consumed by
 * runtime/zig/tests/native_recorded_compute.c. Listed in
 * ../EXTENSION_SYMBOLS.txt, never resolved by wgpuGetProcAddress. */
#ifndef DOE_NATIVE_COMMANDS_H
#define DOE_NATIVE_COMMANDS_H
#include "webgpu.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#if defined(__GNUC__) || defined(__clang__)
#define DOE_NATIVE_EXTENSION __attribute__((visibility("default")))
#else
#define DOE_NATIVE_EXTENSION
#endif

/* Bind group slots per dispatch in the batch arrays (the retained
 * MAX_COMPUTE_BIND_GROUPS). */
enum { DOE_NATIVE_DISPATCH_BIND_GROUP_STRIDE = 4 };

/* One command buffer recording a compute pass with the dispatch (bind groups
 * 0..group_count-1) followed by copyBufferToBuffer, validated exactly as the
 * equivalent public encoder calls. It retains its inputs; the caller may
 * release them before submission. Invalid input or recording returns NULL and
 * reports one validation error to the device (OutOfMemory for allocation
 * failure). A NULL device returns NULL without a report. */
DOE_NATIVE_EXTENSION WGPUCommandBuffer doeNativeCreateComputeDispatchCopyCommandBuffer(
    WGPUDevice device, WGPUComputePipeline pipeline, WGPUBindGroup *groups, uint32_t group_count,
    uint32_t x, uint32_t y, uint32_t z, WGPUBuffer source, uint64_t source_offset,
    WGPUBuffer destination, uint64_t destination_offset, uint64_t size);
/* dispatch_count dispatches in one compute pass, then the copy. Dispatch i uses
 * pipelines[i], groups[i * DOE_NATIVE_DISPATCH_BIND_GROUP_STRIDE ...] (the first
 * group_counts[i]) and dimensions[3 * i .. 3 * i + 2]. An empty batch returns
 * NULL without a report. */
DOE_NATIVE_EXTENSION WGPUCommandBuffer doeNativeCreateComputeDispatchBatchCopyCommandBuffer(
    WGPUDevice device, size_t dispatch_count, WGPUComputePipeline *pipelines, WGPUBindGroup *groups,
    const uint32_t *group_counts, const uint32_t *dimensions, WGPUBuffer source, uint64_t source_offset,
    WGPUBuffer destination, uint64_t destination_offset, uint64_t size);

#ifdef __cplusplus
}
#endif
#endif
