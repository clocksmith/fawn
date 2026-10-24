/* SPDX-License-Identifier: MIT */
#include "backend.h"
#include "d3d12_frontend.h"
#include <stdlib.h>
#include <string.h>

native_result native_backend_open(WGPUBackendType kind, bool fallback, uint32_t surface_platforms,
    native_backend *out)
{
    if (kind == WGPUBackendType_D3D12)
        return surface_platforms ? NATIVE_UNSUPPORTED : native_d3d12_open(fallback, out);
    native_backend result = {.kind=kind};
    native_result status;
    if (kind == WGPUBackendType_Vulkan) {
        /* First device of the requested class (CPU only for fallback), never
         * enumeration ordinal 0 filtered afterwards. */
        result.value.vulkan.options = (doe_vk_options){.allow_cpu_adapter=fallback,
            .require_graphics_queue=true,.enable_shader_demote_if_supported=true,
            .surface_platforms=surface_platforms,
            .adapter_selection=fallback ? DOE_VK_SELECT_SOFTWARE : DOE_VK_SELECT_HARDWARE};
        status = native_from_vulkan(doe_vk_context_create(&result.value.vulkan.options,
            &result.value.vulkan.context, NULL));
        if (status == NATIVE_OK)
            status = native_from_vulkan(doe_vk_context_caps(result.value.vulkan.context,
                &result.value.vulkan.caps));
        if (status == NATIVE_OK && fallback && !result.value.vulkan.caps.software_adapter)
            status = NATIVE_UNAVAILABLE;
        /* WebGPU core admits writable storage buffers and textures in fragment
         * shaders under the per-stage limits, and no limit can report their
         * absence: a device without fragmentStoresAndAtomics is refused, not
         * advertised with a hidden gap. */
        if (status == NATIVE_OK && !result.value.vulkan.caps.fragment_stores_and_atomics)
            status = NATIVE_UNAVAILABLE;
        if (status == NATIVE_OK && native_vulkan_limits(&result.value.vulkan.caps,
                &result.limits) != WGPUStatus_Success) status = NATIVE_FAILURE;
    } else if (kind == WGPUBackendType_Metal) {
        if (fallback) return NATIVE_UNAVAILABLE; /* No CPU adapter is advertised. */
        if (surface_platforms) return NATIVE_UNSUPPORTED; /* Metal layer surfaces are not composed. */
        status = native_from_metal(doe_mtl_context_create(&result.value.metal.context, NULL));
        if (status == NATIVE_OK)
            status = native_from_metal(doe_mtl_context_caps(result.value.metal.context, &result.value.metal.caps));
        if (status == NATIVE_OK && native_metal_limits(&result.value.metal.caps,
                &result.limits) != WGPUStatus_Success) status = NATIVE_FAILURE;
    } else return NATIVE_UNSUPPORTED;
    if (status != NATIVE_OK) { native_backend_close(&result); return status; }
    *out = result;
    return NATIVE_OK;
}

native_result native_backend_device(const native_backend *adapter, native_backend *out)
{
    if (adapter->kind == WGPUBackendType_D3D12) return native_d3d12_reopen(adapter, out);
    native_backend result = {.kind=adapter->kind};
    native_result status;
    if (adapter->kind == WGPUBackendType_Vulkan) {
        /* The device context must open the adapter's own hardware: select by its
         * device/driver UUID pair, so reordered or replaced devices cannot substitute. */
        result.value.vulkan.options = adapter->value.vulkan.options;
        result.value.vulkan.options.adapter_selection = DOE_VK_SELECT_IDENTITY;
        result.value.vulkan.options.required_identity = adapter->value.vulkan.caps.identity;
        status = native_from_vulkan(doe_vk_context_create(&result.value.vulkan.options,
            &result.value.vulkan.context, NULL));
        if (status == NATIVE_OK)
            status = native_from_vulkan(doe_vk_context_caps(result.value.vulkan.context, &result.value.vulkan.caps));
        if (status == NATIVE_OK &&
            (memcmp(&result.value.vulkan.caps.identity, &adapter->value.vulkan.caps.identity,
                sizeof(result.value.vulkan.caps.identity)) != 0 ||
             result.value.vulkan.caps.software_adapter != adapter->value.vulkan.caps.software_adapter))
            status = NATIVE_UNAVAILABLE;
        if (status == NATIVE_OK && native_vulkan_limits(&result.value.vulkan.caps,
                &result.limits) != WGPUStatus_Success) status = NATIVE_FAILURE;
    } else if (adapter->kind == WGPUBackendType_Metal) {
        status = native_from_metal(doe_mtl_context_create(&result.value.metal.context, NULL));
        /* Keep the adapter context alive and compare actual retained native
         * identity. A changed system default is unavailable, never a fallback. */
        if (status == NATIVE_OK && doe_mtl_context_device(result.value.metal.context) !=
                doe_mtl_context_device(adapter->value.metal.context)) status = NATIVE_UNAVAILABLE;
        if (status == NATIVE_OK)
            status = native_from_metal(doe_mtl_context_caps(result.value.metal.context, &result.value.metal.caps));
        if (status == NATIVE_OK && native_metal_limits(&result.value.metal.caps,
                &result.limits) != WGPUStatus_Success) status = NATIVE_FAILURE;
    } else return NATIVE_UNSUPPORTED;
    if (status != NATIVE_OK) { native_backend_close(&result); return status; }
    *out = result;
    return NATIVE_OK;
}

void native_backend_close(native_backend *backend)
{
    if (backend->kind == WGPUBackendType_Vulkan) doe_vk_context_release(backend->value.vulkan.context);
    else if (backend->kind == WGPUBackendType_Metal) doe_mtl_context_release(backend->value.metal.context);
    else if (backend->kind == WGPUBackendType_D3D12) native_d3d12_close(backend);
    *backend = (native_backend){0};
}

native_result native_backend_status(const native_backend *backend)
{
    if (backend->kind == WGPUBackendType_Vulkan)
        return doe_vk_context_alive(backend->value.vulkan.context) ? NATIVE_OK : NATIVE_DEVICE_LOST;
    if (backend->kind == WGPUBackendType_Metal)
        return backend->value.metal.context ? NATIVE_OK : NATIVE_INVALID_STATE;
    if (backend->kind == WGPUBackendType_D3D12) return native_d3d12_status(backend);
    return NATIVE_UNSUPPORTED;
}

/* Lowercase vendor names for PCI and Khronos vendor IDs; empty when unknown
 * (GPUAdapterInfo.vendor: "empty string otherwise"). */
static const char *vendor_name(uint32_t id)
{
    switch (id) {
    case 0x1002: case 0x1022: return "amd";
    case 0x10DE: return "nvidia";
    case 0x8086: return "intel";
    case 0x13B5: return "arm";
    case 0x5143: return "qualcomm";
    case 0x1010: return "imagination";
    case 0x106B: return "apple";
    case 0x14E4: return "broadcom";
    case 0x144D: return "samsung";
    case 0x15AD: return "vmware";
    case 0x1414: return "microsoft";
    case 0x1AE0: return "google";
    case 0x10005: return "mesa";
    default: return "";
    }
}

WGPUStatus native_backend_info(const native_backend *backend, WGPUAdapterInfo *out)
{
    if (!out || out->nextInChain) return WGPUStatus_Error;
    const char *name;
    /* No C backend offers "subgroups", so GPUAdapterInfo takes the spec's
     * defaults (4 and 128), not the driver's sizes; real sizes belong with that
     * feature once a backend offers it. */
    WGPUAdapterInfo result = {.backendType=backend->kind,.adapterType=WGPUAdapterType_Unknown,
        .subgroupMinSize=4,.subgroupMaxSize=128};
    if (backend->kind == WGPUBackendType_Vulkan) {
        const doe_vk_caps *caps = &backend->value.vulkan.caps;
        name = caps->device_name;
        result.vendorID = caps->vendor_id;
        result.deviceID = caps->device_id;
        if (caps->software_adapter) result.adapterType = WGPUAdapterType_CPU;
        else if (caps->device_type == 1) result.adapterType = WGPUAdapterType_IntegratedGPU;
        else if (caps->device_type == 2) result.adapterType = WGPUAdapterType_DiscreteGPU;
    } else if (backend->kind == WGPUBackendType_Metal) name = backend->value.metal.caps.device_name;
    else if (backend->kind == WGPUBackendType_D3D12) {
        const doe_dx_caps *caps = &backend->value.d3d12.caps;
        name = caps->name;
        result.vendorID = caps->vendor_id;
        result.deviceID = caps->device_id;
        result.adapterType = caps->software_adapter ? WGPUAdapterType_CPU :
            caps->unified_memory ? WGPUAdapterType_IntegratedGPU : WGPUAdapterType_DiscreteGPU;
    }
    else return WGPUStatus_Error;
    /* One owned block: vendor, architecture, device, description (the driver's
     * device name). No architecture family is known, so it stays empty. */
    const char *vendor = vendor_name(result.vendorID);
    const size_t vendor_size = strlen(vendor), size = strlen(name);
    char *owned = malloc(vendor_size + 1 + 1 + 2 * (size + 1));
    if (!owned) return WGPUStatus_Error;
    char *at = owned;
    memcpy(at, vendor, vendor_size + 1);
    result.vendor = (WGPUStringView){at,vendor_size};
    at += vendor_size + 1;
    *at = '\0';
    result.architecture = (WGPUStringView){at,0};
    at += 1;
    memcpy(at, name, size + 1);
    result.device = (WGPUStringView){at,size};
    at += size + 1;
    memcpy(at, name, size + 1);
    result.description = (WGPUStringView){at,size};
    *out = result;
    return WGPUStatus_Success;
}
