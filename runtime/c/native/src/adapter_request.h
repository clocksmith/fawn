/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_ADAPTER_REQUEST_H
#define DOE_NATIVE_ADAPTER_REQUEST_H
#include "webgpu.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Pinned Dawn758e5e6a native Windows extension. This is its Windows C ABI,
 * not the layout of a native C++ derived object constructed on another OS. */
#define NATIVE_DAWN_REQUEST_ADAPTER_LUID UINT32_C(0x0005000c)
typedef struct native_windows_adapter_luid {
    WGPUChainedStruct chain;
    uint32_t luid_low;
    int32_t luid_high;
} native_windows_adapter_luid;
typedef struct native_adapter_selection {
    bool has_luid;
    uint32_t luid_low;
    int32_t luid_high;
} native_adapter_selection;
bool native_adapter_request_decode(WGPUBackendType kind,
    const WGPURequestAdapterOptions *options, native_adapter_selection *out);
#ifdef __cplusplus
}
static_assert(offsetof(native_windows_adapter_luid,luid_low)==sizeof(WGPUChainedStruct));
#else
_Static_assert(offsetof(native_windows_adapter_luid,luid_low)==sizeof(WGPUChainedStruct), "LUID follows full chain");
#endif
#endif
