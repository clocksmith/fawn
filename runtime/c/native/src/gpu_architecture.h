/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_GPU_ARCHITECTURE_H
#define DOE_NATIVE_GPU_ARCHITECTURE_H

#include <stdint.h>

/* The GPU architecture family for a PCI vendor and device ID, as reported in
 * WGPUAdapterInfo.architecture by the Vulkan and D3D12 backends. Returns a static
 * string; an ID pair the table in gpu_architecture.c does not list returns "" (the
 * WebGPU "unknown" value), never a guessed or broader family. */
const char *native_gpu_architecture(uint32_t vendor_id, uint32_t device_id);

#endif
