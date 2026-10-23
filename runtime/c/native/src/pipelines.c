/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

void native_pipeline_release(void *userdata, void *handle)
{
    (void)userdata;
    native_pipeline *payload = handle;
    native_lock(payload->device);
    doe_vk_pipeline_release(payload->pipeline);
    native_unlock(payload->device);
    free(payload);
}

static const DoeCompilerEntryV5 *select_entry(const doe_shader_view *view, WGPUStringView name)
{
    size_t size;
    if (!native_string_size(name, &size)) return NULL;
    if (!name.data) return view->counts.entries == 1 ? &view->entries[0] : NULL;
    if (!size || size > 63 || memchr(name.data, 0, size)) return NULL;
    for (size_t i = 0; i < view->counts.entries; ++i) {
        const DoeCompilerEntryV5 *entry = &view->entries[i];
        if (entry->name.length == size && !memcmp(view->names + entry->name.offset, name.data, size)) return entry;
    }
    return NULL;
}

static bool resource_entry(const DoeCompilerResourceV4 *resource, doe_resource_layout_entry *out)
{
    if (resource->group >= DOE_RESOURCE_MAX_GROUPS || resource->binding >= DOE_RESOURCE_MAX_BINDINGS) return false;
    WGPUBufferBindingType type;
    if (resource->kind == DOE_COMPILER_UNIFORM_BUFFER) type = WGPUBufferBindingType_Uniform;
    else if (resource->kind == DOE_COMPILER_STORAGE_BUFFER)
        type = resource->access == DOE_COMPILER_READ ? WGPUBufferBindingType_ReadOnlyStorage : WGPUBufferBindingType_Storage;
    else return false;
    *out = (doe_resource_layout_entry){.binding=resource->binding, .visibility=WGPUShaderStage_Compute,
        .kind=DOE_BIND_BUFFER, .buffer_type=type, .minimum_size=resource->minimum_binding_size_bytes};
    return true;
}

static doe_resource_pipeline_layout *automatic_layout(native_device *device,
    const doe_shader_view *view, const DoeCompilerEntryV5 *entry)
{
    doe_resource_layout_entry entries[DOE_RESOURCE_MAX_GROUPS][DOE_RESOURCE_MAX_BINDINGS];
    size_t counts[DOE_RESOURCE_MAX_GROUPS] = {0};
    size_t group_count = 0;
    for (uint32_t i = 0; i < entry->resource_reference_count; ++i) {
        uint32_t index = view->entry_references[entry->first_resource_reference + i];
        const DoeCompilerResourceV4 *resource = &view->resources[index];
        doe_resource_layout_entry converted;
        if (!resource_entry(resource, &converted) || counts[resource->group] == DOE_RESOURCE_MAX_BINDINGS) return NULL;
        entries[resource->group][counts[resource->group]++] = converted;
        if (group_count <= resource->group) group_count = resource->group + 1;
    }
    doe_resource_layout *groups[DOE_RESOURCE_MAX_GROUPS] = {0};
    doe_resource_pipeline_layout *layout = NULL;
    for (size_t i = 0; i < group_count; ++i)
        if (doe_resource_layout_create(&device->resources, entries[i], counts[i], true, &groups[i]) != DOE_RESOURCE_OK)
            goto finished;
    (void)doe_resource_pipeline_layout_create(&device->resources, groups, group_count, 0, &layout);
finished:
    for (size_t i = 0; i < group_count; ++i) doe_resource_layout_release(groups[i]);
    return layout;
}

static bool pipeline_bindings(native_pipeline *payload, const doe_shader_view *view,
    const DoeCompilerEntryV5 *entry, doe_resource_pipeline_layout *layout)
{
    for (uint32_t i = 0; i < entry->resource_reference_count; ++i) {
        uint32_t index = view->entry_references[entry->first_resource_reference + i];
        const DoeCompilerResourceV4 *resource = &view->resources[index];
        doe_resource_layout_entry required;
        if (!resource_entry(resource, &required)) return false;
        doe_resource_layout *group = doe_resource_pipeline_layout_group(layout, resource->group);
        size_t count = 0;
        const doe_resource_layout_entry *entries = doe_resource_layout_entries(group, &count);
        const doe_resource_layout_entry *actual = NULL;
        for (size_t j = 0; j < count; ++j) if (entries[j].binding == resource->binding) actual = &entries[j];
        if (!actual || actual->kind != DOE_BIND_BUFFER || !(actual->visibility & WGPUShaderStage_Compute) ||
            actual->buffer_type != required.buffer_type ||
            (actual->minimum_size && actual->minimum_size < required.minimum_size)) return false;
        size_t at = payload->binding_count++;
        payload->bindings[at] = (doe_vk_binding_layout){resource->group, resource->binding,
            resource->kind == DOE_COMPILER_UNIFORM_BUFFER ? DOE_VK_BINDING_UNIFORM : DOE_VK_BINDING_STORAGE};
        payload->minimum_sizes[at] = resource->minimum_binding_size_bytes;
    }
    return true;
}

/* Target capability admission, not source parsing or shader validation. The
 * compiler owns valid words; this backend enables only baseline Shader/Matrix. */
static bool native_shader_capabilities(const doe_shader_view *view)
{
    if (view->counts.words < 5 || view->words[1] > UINT32_C(0x00010300)) return false;
    for (size_t at = 5; at < view->counts.words;) {
        uint32_t length = view->words[at] >> 16;
        uint32_t opcode = view->words[at] & 65535u;
        if (!length || length > view->counts.words - at) return false;
        if (opcode == 17 && (length != 2 || view->words[at + 1] > 1)) return false;
        if (opcode == 10) return false;
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
    native_shader *shader_payload = doe_resource_shader_native((doe_resource_shader *)descriptor->compute.module);
    doe_shader *shader = shader_payload->compiled;
    if (!shader) {
        pipeline_failure(error, WGPUErrorType_Validation, "shader module contains a compilation error");
        return NULL;
    }
    doe_shader_view view;
    if (doe_shader_borrow(shader, &view) != DOE_SHADER_OK) return NULL;
    if (!native_shader_capabilities(&view)) {
        pipeline_failure(error, WGPUErrorType_Validation, "shader requires capabilities not enabled by the C Vulkan device");
        return NULL;
    }
    const DoeCompilerEntryV5 *entry = select_entry(&view, descriptor->compute.entryPoint);
    /* The C compiler emits guarded storage accesses, including OpArrayLength
     * for runtime tails (compiler/src/spirv.c access_condition). This baseline
     * Vulkan context also requires robustBufferAccess. No TranslationInfo
     * metadata is fabricated; texture/subgroup paths remain unprovided. */
    if (!entry || entry->stage != DOE_COMPILER_STAGE_COMPUTE || entry->workgroup_storage_bytes > device->caps.max_workgroup_storage_size) {
        pipeline_failure(error, WGPUErrorType_Validation,
            "selected entry missing or workgroup storage exceeds native limit");
        return NULL;
    }
    uint64_t invocations = 1;
    for (size_t i = 0; i < 3; ++i) {
        if (!entry->workgroup_size[i] || entry->workgroup_size[i] > device->caps.max_workgroup_size[i] ||
            invocations > device->caps.max_workgroup_invocations / entry->workgroup_size[i]) {
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
    if (automatic) layout = automatic_layout(device, &view, entry);
    doe_resource_pipeline *pipeline = NULL;
    doe_vk_status backend_status = DOE_VK_INVALID_ARGUMENT;
    doe_resource_status resource_status = DOE_RESOURCE_INVALID_ARGUMENT;
    if (layout && doe_resource_pipeline_layout_device(layout) == device &&
        native_device_alive(device) && pipeline_bindings(payload, &view, entry, layout)) {
        char name[64];
        memcpy(name, view.names + entry->name.offset, entry->name.length);
        name[entry->name.length] = 0;
        doe_vk_error error = {0};
        backend_status = doe_vk_pipeline_create(device->context, view.words, view.counts.words,
            name, payload->bindings, payload->binding_count, &payload->pipeline, &error);
        if (backend_status == DOE_VK_OK)
            resource_status = doe_resource_pipeline_create(&device->resources, layout,
                (doe_resource_native){payload, NULL, native_pipeline_release}, entry->workgroup_size, &pipeline);
    }
    if (automatic) doe_resource_pipeline_layout_release(layout);
    native_unlock(device);
    if (!pipeline) {
        native_pipeline_release(NULL, payload);
        WGPUErrorType type = WGPUErrorType_Validation;
        if (backend_status == DOE_VK_OUT_OF_MEMORY || resource_status == DOE_RESOURCE_OUT_OF_MEMORY)
            type = WGPUErrorType_OutOfMemory;
        else if (backend_status == DOE_VK_DEVICE_LOST || backend_status == DOE_VK_NATIVE_FAILURE)
            type = WGPUErrorType_Internal;
        pipeline_failure(error, type, "compute pipeline creation or layout validation failed");
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
            pipeline_failure(error, WGPUErrorType_OutOfMemory, "pipeline label storage failed");
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
    doe_resource_layout *layout = doe_resource_pipeline_layout_group(doe_resource_pipeline_get_layout(pipeline), index);
    doe_resource_layout_retain(layout);
    native_unlock(device);
    if (!layout) native_error(device, WGPUErrorType_Validation, "pipeline bind group index out of range");
    return (WGPUBindGroupLayout)layout;
}
