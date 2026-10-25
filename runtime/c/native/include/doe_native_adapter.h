/* SPDX-License-Identifier: MIT
 * Doe adapter identity extension ABI (not webgpu.h). The signature matches the
 * retained Zig export in runtime/zig/src/native/support/doe_adapter_info_native.zig
 * that the addon's adapter info (doe-gpu GPUAdapter.info vendorID, deviceID and
 * driverVersion) resolves. Listed in ../EXTENSION_SYMBOLS.txt, never resolved by
 * wgpuGetProcAddress. */
#ifndef DOE_NATIVE_ADAPTER_H
#define DOE_NATIVE_ADAPTER_H
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

/* The adapter's PCI vendor and device IDs and the driver's raw version: Vulkan
 * VkPhysicalDeviceProperties vendorID, deviceID and driverVersion; D3D12 vendor
 * and device IDs with driver version 0 (not recorded); 0 for every field a
 * backend does not report and for a NULL adapter. NULL outputs are skipped. */
DOE_NATIVE_EXTENSION void doeNativeAdapterGetPciIdentity(WGPUAdapter adapter, uint32_t *vendor_id, uint32_t *device_id,
    uint32_t *driver_version);
#ifdef __cplusplus
}
#endif
#endif
