/* SPDX-License-Identifier: MIT
 * One adapter-class selection for every public native consumer. Software (CPU)
 * is the default; DOE_C_TEST_ADAPTER=physical requests a nonfallback adapter and
 * requires a GPU adapter type. Physical runs are developer evidence on the
 * reported adapter/driver only, never backend or hardware qualification. */
#ifndef DOE_NATIVE_TEST_ADAPTER_CLASS_H
#define DOE_NATIVE_TEST_ADAPTER_CLASS_H
#include "webgpu.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline bool adapter_class_physical(void)
{
    const char *value = getenv("DOE_C_TEST_ADAPTER");
    if (value == NULL || strcmp(value, "software") == 0) return false;
    if (strcmp(value, "physical") == 0) return true;
    fprintf(stderr, "DOE_C_TEST_ADAPTER must be software or physical, not %s\n", value);
    abort();
}

static inline WGPUBackendType adapter_class_backend(void)
{
    const char *value = getenv("DOE_C_TEST_BACKEND");
    if (!value || !strcmp(value, "vulkan")) return WGPUBackendType_Vulkan;
    if (!strcmp(value, "metal")) return WGPUBackendType_Metal;
    if (!strcmp(value, "d3d12")) return WGPUBackendType_D3D12;
    fprintf(stderr, "DOE_C_TEST_BACKEND must be vulkan, metal or d3d12, not %s\n", value);
    abort();
}

static inline void adapter_class_options(WGPURequestAdapterOptions *options)
{
    options->backendType = adapter_class_backend();
    options->forceFallbackAdapter = adapter_class_physical() ? WGPU_FALSE : WGPU_TRUE;
}

/* Run label for PASS lines; set by adapter_class_require from the actual adapter. */
static char adapter_class_label[160] = "(adapter not selected)";

static inline const char *adapter_class_scope(void)
{
    return adapter_class_label;
}

static inline void adapter_class_require(WGPUAdapter adapter)
{
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    assert(wgpuAdapterGetInfo(adapter, &info) == WGPUStatus_Success);
    assert(info.backendType == adapter_class_backend());
    if (info.backendType == WGPUBackendType_Metal) {
        /* Metal has no CPU fallback; subtype is not queried by this provider. */
        assert(adapter_class_physical() && info.device.length && info.device.data);
        assert(info.adapterType == WGPUAdapterType_Unknown || info.adapterType == WGPUAdapterType_IntegratedGPU ||
            info.adapterType == WGPUAdapterType_DiscreteGPU);
        snprintf(adapter_class_label, sizeof(adapter_class_label),
            "(native Metal device: %.*s; subtype unclassified; developer run, not qualification)",
            (int)info.device.length, info.device.data);
        printf("Selected adapter backend Metal, type %u: %.*s\n", (unsigned)info.adapterType,
            (int)info.device.length, info.device.data);
        wgpuAdapterInfoFreeMembers(info);
        return;
    }
    if (adapter_class_physical()) {
        assert(info.adapterType == WGPUAdapterType_DiscreteGPU ||
               info.adapterType == WGPUAdapterType_IntegratedGPU);
        snprintf(adapter_class_label, sizeof(adapter_class_label),
                 "(physical %s developer run: %.*s; not qualification)",
                 info.backendType == WGPUBackendType_D3D12 ? "D3D12" : "Vulkan",
                 (int)info.device.length, info.device.data);
    } else {
        assert(info.adapterType == WGPUAdapterType_CPU);
        snprintf(adapter_class_label, sizeof(adapter_class_label), "(CPU %s only)",
            info.backendType == WGPUBackendType_D3D12 ? "D3D12 WARP" : "Vulkan");
    }
    if (info.backendType == WGPUBackendType_D3D12)
        printf("Selected adapter backend D3D12, type %u, vendor %u, device %u: %.*s\n",
            (unsigned)info.adapterType, info.vendorID, info.deviceID,
            (int)info.device.length, info.device.data);
    wgpuAdapterInfoFreeMembers(info);
}
#endif
