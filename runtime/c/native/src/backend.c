/* SPDX-License-Identifier: MIT */
#include "backend.h"
#include <stdlib.h>
#include <string.h>

static void metal_limits(const doe_mtl_caps *caps, WGPULimits *out)
{
    /* Frontend admission envelope, not queried hardware guarantees. Real per-
     * pipeline maxTotalThreadsPerThreadgroup remains checked by the backend.
     * 256-byte offsets are an explicit conservative subset across Metal families. */
    uint64_t binding_size = caps->max_buffer_length < UINT32_MAX ? caps->max_buffer_length : UINT32_MAX;
    uint32_t dimensions[3];
    for (size_t i = 0; i < 3; ++i)
        dimensions[i] = caps->max_threads_per_dimension[i] < 256 ?
            (uint32_t)caps->max_threads_per_dimension[i] : 256;
    *out = (WGPULimits){.maxBufferSize=caps->max_buffer_length,
        .maxBindGroups=4,.maxBindingsPerBindGroup=128,
        .maxDynamicUniformBuffersPerPipelineLayout=30,.maxDynamicStorageBuffersPerPipelineLayout=30,
        .maxUniformBuffersPerShaderStage=30,.maxStorageBuffersPerShaderStage=30,
        .maxUniformBufferBindingSize=binding_size < 65536 ? binding_size : 65536,
        .maxStorageBufferBindingSize=binding_size,
        .minUniformBufferOffsetAlignment=256,.minStorageBufferOffsetAlignment=256,
        .maxComputeWorkgroupStorageSize=caps->max_threadgroup_memory_length > UINT32_MAX ?
            UINT32_MAX : (uint32_t)caps->max_threadgroup_memory_length,
        .maxComputeInvocationsPerWorkgroup=256,.maxComputeWorkgroupSizeX=dimensions[0],
        .maxComputeWorkgroupSizeY=dimensions[1],.maxComputeWorkgroupSizeZ=dimensions[2],
        .maxComputeWorkgroupsPerDimension=65535,.maxImmediateSize=64};
}

native_result native_backend_open(WGPUBackendType kind, bool fallback, uint32_t surface_platforms,
    native_backend *out)
{
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
        if (status == NATIVE_OK) metal_limits(&result.value.metal.caps, &result.limits);
    } else return NATIVE_UNSUPPORTED;
    if (status != NATIVE_OK) { native_backend_close(&result); return status; }
    *out = result;
    return NATIVE_OK;
}

native_result native_backend_device(const native_backend *adapter, native_backend *out)
{
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
        if (status == NATIVE_OK) metal_limits(&result.value.metal.caps, &result.limits);
    } else return NATIVE_UNSUPPORTED;
    if (status != NATIVE_OK) { native_backend_close(&result); return status; }
    *out = result;
    return NATIVE_OK;
}

void native_backend_close(native_backend *backend)
{
    if (backend->kind == WGPUBackendType_Vulkan) doe_vk_context_release(backend->value.vulkan.context);
    else if (backend->kind == WGPUBackendType_Metal) doe_mtl_context_release(backend->value.metal.context);
    *backend = (native_backend){0};
}

native_result native_backend_status(const native_backend *backend)
{
    if (backend->kind == WGPUBackendType_Vulkan)
        return doe_vk_context_alive(backend->value.vulkan.context) ? NATIVE_OK : NATIVE_DEVICE_LOST;
    if (backend->kind == WGPUBackendType_Metal)
        return backend->value.metal.context ? NATIVE_OK : NATIVE_INVALID_STATE;
    return NATIVE_UNSUPPORTED;
}

WGPUStatus native_backend_info(const native_backend *backend, WGPUAdapterInfo *out)
{
    if (!out || out->nextInChain) return WGPUStatus_Error;
    const char *name;
    WGPUAdapterInfo result = {.backendType=backend->kind,.adapterType=WGPUAdapterType_Unknown};
    if (backend->kind == WGPUBackendType_Vulkan) {
        const doe_vk_caps *caps = &backend->value.vulkan.caps;
        name = caps->device_name;
        result.vendorID = caps->vendor_id;
        result.deviceID = caps->device_id;
        if (caps->software_adapter) result.adapterType = WGPUAdapterType_CPU;
        else if (caps->device_type == 1) result.adapterType = WGPUAdapterType_IntegratedGPU;
        else if (caps->device_type == 2) result.adapterType = WGPUAdapterType_DiscreteGPU;
    } else if (backend->kind == WGPUBackendType_Metal) name = backend->value.metal.caps.device_name;
    else return WGPUStatus_Error;
    size_t size = strlen(name);
    char *owned = malloc(size + 1);
    if (!owned) return WGPUStatus_Error;
    memcpy(owned, name, size + 1);
    result.device = (WGPUStringView){owned,size};
    *out = result;
    return WGPUStatus_Success;
}
