/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

void native_pipeline_release(void *userdata, void *handle)
{
    (void)userdata;
    native_pipeline *payload = handle;
    native_lock(payload->device);
    if (payload->device->backend.kind == WGPUBackendType_Vulkan) doe_vk_pipeline_release(payload->handle.vulkan);
    else if (payload->device->backend.kind == WGPUBackendType_Metal) doe_mtl_pipeline_release(payload->handle.metal);
    native_unlock(payload->device);
    free(payload);
}

/* SPIR-V strings use little-endian packed bytes independent of host byte order.
 * Require exact length and zero padding, not a prefix or an arbitrary extension. */
static bool extension_name(const uint32_t *words, size_t count, const char *name)
{
    size_t size = strlen(name) + 1;
    if (count != (size + 3) / 4) return false;
    for (size_t i = 0; i < count * 4; ++i) {
        unsigned expected = i < size ? (unsigned char)name[i] : 0;
        if (((words[i / 4] >> ((i % 4) * 8)) & 255u) != expected) return false;
    }
    return true;
}

/* Target capability admission, not source validation. The compiler owns valid
 * words. Demote is admitted only after actual extension/feature enablement. */
bool native_shader_capabilities(const doe_shader_view *view, const doe_vk_caps *caps)
{
    if (!view || !caps || view->counts.words < 5 || view->words[1] > UINT32_C(0x00010300)) return false;
    for (size_t at = 5; at < view->counts.words;) {
        uint32_t length = view->words[at] >> 16;
        uint32_t opcode = view->words[at] & 65535u;
        if (!length || length > view->counts.words - at) return false;
        if (opcode == 17) {
            if (length != 2) return false;
            uint32_t capability = view->words[at + 1];
            if (capability != 0 && capability != 1 && capability != 43 && capability != 44 && capability != 50 &&
                !(capability == 5379 && caps->shader_demote_to_helper_invocation)) return false;
        }
        if (opcode == 10 && (!caps->shader_demote_to_helper_invocation ||
            !extension_name(view->words + at + 1, length - 1, "SPV_EXT_demote_to_helper_invocation"))) return false;
        at += length;
    }
    return true;
}

static void pipeline_failure(native_pipeline_error *error, WGPUErrorType type, const char *message)
{
    *error = (native_pipeline_error){type, message};
}

WGPUComputePipeline native_create_compute_pipeline(native_device *device,
    const WGPUComputePipelineDescriptor *descriptor, native_pipeline_error *error)
{
    if (!device) return NULL;
    if (!descriptor || descriptor->nextInChain || descriptor->compute.nextInChain ||
        descriptor->compute.constantCount || !descriptor->compute.module ||
        doe_resource_shader_device((doe_resource_shader *)descriptor->compute.module) != device) {
        pipeline_failure(error, WGPUErrorType_Validation, "compute descriptor unsupported or shader device mismatch");
        return NULL;
    }
    native_stage stage;
    if (!native_stage_load(device, descriptor->compute.module, descriptor->compute.entryPoint,
            DOE_COMPILER_STAGE_COMPUTE, &stage)) {
        pipeline_failure(error, WGPUErrorType_Validation, "compute shader entry/capability admission failed");
        return NULL;
    }
    const doe_shader_view *view = &stage.view;
    const DoeCompilerEntryV5 *entry = stage.entry;
    /* The C compiler emits guarded storage accesses, including OpArrayLength
     * for runtime tails (compiler/src/spirv.c access_condition). This baseline
     * Vulkan context also requires robustBufferAccess. No TranslationInfo
     * metadata is fabricated; texture bounds use the compiler image guards; subgroup paths remain unprovided. */
    if (!entry || entry->stage != DOE_COMPILER_STAGE_COMPUTE || entry->workgroup_storage_bytes > device->backend.limits.maxComputeWorkgroupStorageSize) {
        pipeline_failure(error, WGPUErrorType_Validation,
            "selected entry missing or workgroup storage exceeds native limit");
        return NULL;
    }
    uint32_t dimensions[3] = {device->backend.limits.maxComputeWorkgroupSizeX,
        device->backend.limits.maxComputeWorkgroupSizeY,device->backend.limits.maxComputeWorkgroupSizeZ};
    uint64_t invocations = 1;
    for (size_t i = 0; i < 3; ++i) {
        if (!entry->workgroup_size[i] || entry->workgroup_size[i] > dimensions[i] ||
            invocations > device->backend.limits.maxComputeInvocationsPerWorkgroup / entry->workgroup_size[i]) {
            pipeline_failure(error, WGPUErrorType_Validation, "workgroup size exceeds native limit");
            return NULL;
        }
        invocations *= entry->workgroup_size[i];
    }
    native_pipeline *payload = calloc(1, sizeof(*payload));
    if (!payload) { pipeline_failure(error, WGPUErrorType_OutOfMemory, "pipeline allocation failed"); return NULL; }
    payload->device = device;
    native_lock(device);
    doe_resource_pipeline_layout *layout = (doe_resource_pipeline_layout *)descriptor->layout;
    bool automatic = !layout;
    doe_resource_status resource_status = automatic ?
        native_automatic_layout(device, &stage, 1, &layout) : DOE_RESOURCE_OK;
    doe_resource_pipeline *pipeline = NULL;
    native_result backend_status = native_device_result(device);
    if (resource_status == DOE_RESOURCE_OK && (!layout || doe_resource_pipeline_layout_device(layout) != device ||
        !native_device_alive(device) || !native_pipeline_bindings(payload, &stage, 1, layout)))
        resource_status = DOE_RESOURCE_LAYOUT;
    if (resource_status == DOE_RESOURCE_OK && backend_status == NATIVE_OK) {
        if (device->backend.kind == WGPUBackendType_Vulkan) {
            doe_vk_binding_layout native_bindings[DOE_RESOURCE_MAX_GROUPS * DOE_RESOURCE_MAX_BINDINGS];
            if (!native_vulkan_binding_layout(payload,native_bindings)) backend_status = NATIVE_UNSUPPORTED;
            else backend_status = native_from_vulkan(doe_vk_pipeline_create(device->backend.value.vulkan.context,
                view->words, view->counts.words, stage.name, native_bindings, payload->binding_count,
                &payload->handle.vulkan, NULL));
        } else if (device->backend.kind == WGPUBackendType_Metal)
            backend_status = native_metal_pipeline_create(payload,&stage);
        else backend_status = NATIVE_UNSUPPORTED;
        if (backend_status == NATIVE_OK)
            resource_status = doe_resource_pipeline_create(&device->resources, layout,
                (doe_resource_native){payload, NULL, native_pipeline_release}, entry->workgroup_size, &pipeline);
    }
    if (automatic) doe_resource_pipeline_layout_release(layout);
    native_unlock(device);
    if (!pipeline) {
        native_pipeline_release(NULL, payload);
        WGPUErrorType type = backend_status != NATIVE_OK ?
            native_result_error(backend_status) : native_resource_error(resource_status);
        pipeline_failure(error, type, "compute pipeline/layout failed or precise unfilterable texture/sampler pairing analysis unsupported");
    }
    if (pipeline) {
        size_t label_size;
        doe_label_status label_status = DOE_LABEL_INVALID_ARGUMENT;
        if (native_string_size(descriptor->label, &label_size))
            label_status = doe_label_store_set(device->labels, pipeline,
                (const unsigned char *)descriptor->label.data, label_size);
        if (label_status != DOE_LABEL_OK) {
            doe_resource_pipeline_release(pipeline);
            pipeline = NULL;
            pipeline_failure(error, native_label_error(label_status), "pipeline label storage failed");
        }
    }
    return (WGPUComputePipeline)pipeline;
}
WGPUComputePipeline wgpuDeviceCreateComputePipeline(WGPUDevice handle,
    const WGPUComputePipelineDescriptor *descriptor)
{
    native_device *device = (native_device *)handle;
    if (!device) return NULL;
    native_pipeline_error error = {WGPUErrorType_Internal, "compute pipeline creation failed"};
    WGPUComputePipeline pipeline = native_create_compute_pipeline(device, descriptor, &error);
    if (!pipeline) native_error(device, error.type, error.message);
    return pipeline;
}

void wgpuComputePipelineAddRef(WGPUComputePipeline pipeline) { doe_resource_pipeline_retain((doe_resource_pipeline *)pipeline); }
void wgpuComputePipelineRelease(WGPUComputePipeline pipeline) { doe_resource_pipeline_release((doe_resource_pipeline *)pipeline); }
WGPUBindGroupLayout wgpuComputePipelineGetBindGroupLayout(WGPUComputePipeline handle, uint32_t index)
{
    doe_resource_pipeline *pipeline = (doe_resource_pipeline *)handle;
    native_device *device = doe_resource_pipeline_device(pipeline);
    if (!device) return NULL;
    native_lock(device);
    doe_resource_layout *layout = NULL;
    doe_resource_status status = index < device->backend.limits.maxBindGroups ?
        doe_resource_pipeline_layout_get_group(doe_resource_pipeline_get_layout(pipeline), index, &layout) :
        DOE_RESOURCE_RANGE;
    if (status == DOE_RESOURCE_OK) doe_resource_layout_retain(layout);
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, native_resource_error(status),
        "pipeline bind group layout unavailable or index out of range");
    return (WGPUBindGroupLayout)layout;
}
