/* SPDX-License-Identifier: MIT
 * Doe compute-program extension ABI (not webgpu.h). Signatures match the
 * retained Zig exports consumed by runtime/bridge/webgpu-addon/doe_napi_program.c.
 * Listed in ../EXTENSION_SYMBOLS.txt, never resolved by wgpuGetProcAddress. */
#ifndef DOE_NATIVE_PROGRAMS_H
#define DOE_NATIVE_PROGRAMS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#if defined(__GNUC__) || defined(__clang__)
#define DOE_NATIVE_EXTENSION __attribute__((visibility("default")))
#else
#define DOE_NATIVE_EXTENSION
#endif

/* config/compute-program.schema.json $defs.nativeContractVersion at build time. */
DOE_NATIVE_EXTENSION uint32_t doeNativeComputeProgramContractVersion(void);
/* Nonzero when GPU-recorded programs can be prepared on this platform (Vulkan). */
DOE_NATIVE_EXTENSION uint32_t doeNativeComputeProgramSupported(void);
/* Timestamp queries resolve to GPU nanoseconds in-command: period 1, 64 valid
 * bits, only for a Vulkan device created with TimestampQuery. Returns 0 otherwise. */
DOE_NATIVE_EXTENSION uint32_t doeNativeComputeProgramTimestampNanoseconds(void *device,
    double *period_ns, uint32_t *valid_bits);
/* gpu-recorded: claims a finished, never-submitted WGPUCommandBuffer (buffer
 * copies, clears and direct dispatches only) and records ONE reusable native
 * command buffer. Preparation dispatches nothing. NULL reports a device error. */
DOE_NATIVE_EXTENSION void *doeNativeComputeProgramPrepare(void *queue, void *commands);
/* native-recorded: claims the command buffer for host replay; every submit
 * lowers the retained commands into a fresh one-shot native command buffer. */
DOE_NATIVE_EXTENSION void *doeNativeComputeProgramPrepareHostReplay(void *queue, void *commands);
/* Revalidates live, unmapped, healthy resources and submits once; 1 on proven
 * success. Failure reports a device error; unknown completion stays retained. */
DOE_NATIVE_EXTENSION uint32_t doeNativeComputeProgramSubmit(void *program);
/* Never frees in-flight work: an unresolved submission keeps its own holders. */
DOE_NATIVE_EXTENSION void doeNativeComputeProgramRelease(void *program);
#ifdef __cplusplus
}
#endif
#endif
