/* SPDX-License-Identifier: MIT */
#include "backend.h"
#include "doe_native_adapter.h"
#include "internal.h"
#include "surface.h"
#include "d3d12_frontend.h"
#include <stdlib.h>
#include <string.h>

static bool d3d12_surface_supported(uint32_t platforms)
{
    return !platforms || platforms == NATIVE_SURFACE_WINDOWS_HWND;
}

native_result native_backend_open_luid(uint32_t low, int32_t high,
    uint32_t surface_platforms, native_backend *out)
{
    return d3d12_surface_supported(surface_platforms) ?
        native_d3d12_open_luid(low, high, out) : NATIVE_UNSUPPORTED;
}

native_result native_backend_open(WGPUBackendType kind, bool fallback, uint32_t surface_platforms,
    native_backend *out)
{
    if (kind == WGPUBackendType_D3D12)
        return d3d12_surface_supported(surface_platforms) ?
            native_d3d12_open(fallback, out) : NATIVE_UNSUPPORTED;
    native_backend result = {.kind=kind};
    native_result status;
    if (kind == WGPUBackendType_Vulkan) {
        /* First device of the requested class, never enumeration ordinal 0
         * filtered afterwards: CPU only for fallback; otherwise the first
         * non-CPU device, else the CPU device WebGPU may answer a default
         * request with, from one instance and enumeration. */
        result.value.vulkan.options = (doe_vk_options){.allow_cpu_adapter=true,
            .require_graphics_queue=true,.enable_shader_demote_if_supported=true,
            .surface_platforms=surface_platforms,
            .adapter_selection=fallback ? DOE_VK_SELECT_SOFTWARE : DOE_VK_SELECT_HARDWARE_ELSE_SOFTWARE,
            /* An adapter answers capability and surface queries; each device
             * opens its own native device below. */
            .adapter_only=true};
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
        if (surface_platforms && surface_platforms != NATIVE_SURFACE_METAL_LAYER) return NATIVE_UNSUPPORTED;
        status = native_from_metal(doe_mtl_context_create(&result.value.metal.context, NULL));
        /* The adapter context never submits user work. Probe a real event with
         * the existing timeline owner before offering shared-event import;
         * device synchronization policy is still selected independently. */
        if (status == NATIVE_OK)
            status = native_from_metal(doe_mtl_context_prefer_timeline(result.value.metal.context, NULL));
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
    return native_backend_device_policy(adapter, NULL, out);
}

native_result native_backend_device_policy(const native_backend *adapter, const DoeNativeDevicePolicy *policy,
    native_backend *out)
{
    if (policy && ((adapter->kind != WGPUBackendType_Vulkan && adapter->kind != WGPUBackendType_Metal && adapter->kind != WGPUBackendType_D3D12) || policy->queue_selection > 2 ||
        policy->synchronization > 1 || policy->upload_policy > DOE_NATIVE_UPLOAD_ALLOW_MAPPED || policy->subgroup_policy > 3 ||
        policy->reserved[0] || policy->reserved[1] || policy->reserved[2] || policy->reserved[3]))
        return NATIVE_UNSUPPORTED;
    if (policy && adapter->kind == WGPUBackendType_Metal &&
        (policy->queue_selection == DOE_NATIVE_QUEUE_REQUIRE_COMPUTE_ONLY ||
         policy->synchronization != DOE_NATIVE_SYNC_PREFER_TIMELINE || policy->subgroup_policy))
        return NATIVE_UNSUPPORTED;
    if (adapter->kind == WGPUBackendType_D3D12) {
        /* Existing direct queue is graphics+compute; its one monotonic native
         * fence is a timeline, not a reusable fence pool. All public writes
         * already stage through UPLOAD -> DEFAULT on the ordinary command owner. */
        if (policy && (policy->queue_selection == DOE_NATIVE_QUEUE_REQUIRE_COMPUTE_ONLY ||
            policy->synchronization != DOE_NATIVE_SYNC_PREFER_TIMELINE ||
            policy->upload_policy == DOE_NATIVE_UPLOAD_ALLOW_MAPPED || policy->subgroup_policy))
            return NATIVE_UNSUPPORTED;
        native_backend result = {0};
        native_result status = native_d3d12_reopen(adapter, &result);
        if (status == NATIVE_OK) {
            result.upload_policy = policy ? policy->upload_policy : DOE_NATIVE_UPLOAD_DEFAULT;
            *out = result;
        }
        return status;
    }
    native_backend result = {.kind=adapter->kind};
    native_result status;
    if (adapter->kind == WGPUBackendType_Vulkan) {
        /* The device context must open the adapter's own hardware: select by its
         * device/driver UUID pair, so reordered or replaced devices cannot substitute. */
        result.value.vulkan.options = adapter->value.vulkan.options;
        result.value.vulkan.options.adapter_selection = DOE_VK_SELECT_IDENTITY;
        result.value.vulkan.options.adapter_only = false;
        if (policy) {
            result.upload_policy = policy->upload_policy;
            result.value.vulkan.options.queue_selection = (doe_vk_queue_selection)policy->queue_selection;
            result.value.vulkan.options.sync_selection = (doe_vk_sync_selection)policy->synchronization;
            result.value.vulkan.options.subgroup_selection = (doe_vk_subgroup_selection)policy->subgroup_policy;
            if (policy->queue_selection == DOE_NATIVE_QUEUE_REQUIRE_COMPUTE_ONLY)
                result.value.vulkan.options.require_graphics_queue = false;
        }
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
        if (status == NATIVE_OK && policy) {
            result.upload_policy = policy->upload_policy;
            status = native_from_metal(doe_mtl_context_prefer_timeline(result.value.metal.context, NULL));
        }
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

void doeNativeAdapterGetPciIdentity(WGPUAdapter adapter, uint32_t *vendor_id, uint32_t *device_id,
    uint32_t *driver_version)
{
    uint32_t vendor = 0, device = 0, driver = 0;
    const native_backend *backend = adapter ? &((const native_adapter *)adapter)->backend : NULL;
    if (backend && backend->kind == WGPUBackendType_Vulkan) {
        const doe_vk_caps *caps = &backend->value.vulkan.caps;
        vendor = caps->vendor_id;
        device = caps->device_id;
        driver = caps->driver_version;
    } else if (backend && backend->kind == WGPUBackendType_D3D12) {
        vendor = backend->value.d3d12.caps.vendor_id;
        device = backend->value.d3d12.caps.device_id;
    }
    if (vendor_id) *vendor_id = vendor;
    if (device_id) *device_id = device;
    if (driver_version) *driver_version = driver;
}

WGPUStatus native_backend_info(const native_backend *backend, WGPUAdapterInfo *out)
{
    if (!out || out->nextInChain) return WGPUStatus_Error;
    const char *name;
    /* Without the requested feature, retain WebGPU default range. */
    WGPUAdapterInfo result = {.backendType=backend->kind,.adapterType=WGPUAdapterType_Unknown,
        .subgroupMinSize=4,.subgroupMaxSize=128};
    if (backend->kind == WGPUBackendType_Vulkan) {
        const doe_vk_caps *caps = &backend->value.vulkan.caps;
        if (native_vulkan_subgroups_supported(caps)) {
            result.subgroupMinSize = caps->subgroup_min_size;
            result.subgroupMaxSize = caps->subgroup_max_size;
        }
        name = caps->device_name;
        result.vendorID = caps->vendor_id;
        result.deviceID = caps->device_id;
        if (caps->software_adapter) result.adapterType = WGPUAdapterType_CPU;
        else if (caps->device_type == 1) result.adapterType = WGPUAdapterType_IntegratedGPU;
        else if (caps->device_type == 2) result.adapterType = WGPUAdapterType_DiscreteGPU;
    } else if (backend->kind == WGPUBackendType_Metal) {
        const doe_mtl_caps *caps = &backend->value.metal.caps;
        name = caps->device_name;
        if (caps->subgroups) {
            result.subgroupMinSize = caps->subgroup_min_size;
            result.subgroupMaxSize = caps->subgroup_max_size;
        }
    }
    else if (backend->kind == WGPUBackendType_D3D12) {
        const doe_dx_caps *caps = &backend->value.d3d12.caps;
        if (native_adapter_features(backend) & NATIVE_FEATURE(WGPUFeatureName_Subgroups)) {
            result.subgroupMinSize = caps->wave_lane_count_min;
            result.subgroupMaxSize = caps->wave_lane_count_max;
        }
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
