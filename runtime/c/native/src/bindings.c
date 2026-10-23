/* SPDX-License-Identifier: MIT */
#include "internal.h"

WGPUBindGroupLayout wgpuDeviceCreateBindGroupLayout(WGPUDevice handle,
    const WGPUBindGroupLayoutDescriptor *descriptor)
{
    native_device *device = (native_device *)handle;
    if (!device) return NULL;
    doe_resource_layout_entry entries[DOE_RESOURCE_MAX_BINDINGS];
    bool valid = descriptor && !descriptor->nextInChain &&
        descriptor->entryCount <= DOE_RESOURCE_MAX_BINDINGS &&
        (!descriptor->entryCount || descriptor->entries);
    if (valid) for (size_t i = 0; i < descriptor->entryCount; ++i) {
        const WGPUBindGroupLayoutEntry *entry = &descriptor->entries[i];
        if (entry->nextInChain || entry->bindingArraySize || entry->buffer.nextInChain ||
            entry->sampler.type || entry->texture.sampleType || entry->storageTexture.access ||
            !entry->buffer.type || entry->visibility & ~WGPUShaderStage_Compute) {
            valid = false;
            break;
        }
        entries[i] = (doe_resource_layout_entry){
            .binding = entry->binding, .visibility = entry->visibility,
            .kind = DOE_BIND_BUFFER, .buffer_type = entry->buffer.type,
            .dynamic_offset = entry->buffer.hasDynamicOffset != 0,
            .minimum_size = entry->buffer.minBindingSize
        };
    }
    doe_resource_layout *layout = NULL;
    native_lock(device);
    doe_resource_status status = valid ? doe_resource_layout_create(&device->resources,
        entries, descriptor->entryCount, false, &layout) : DOE_RESOURCE_UNSUPPORTED;
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, WGPUErrorType_Validation, "bind group layout unsupported or invalid");
    return (WGPUBindGroupLayout)layout;
}
void wgpuBindGroupLayoutAddRef(WGPUBindGroupLayout layout) { doe_resource_layout_retain((doe_resource_layout *)layout); }
void wgpuBindGroupLayoutRelease(WGPUBindGroupLayout layout) { doe_resource_layout_release((doe_resource_layout *)layout); }

WGPUPipelineLayout wgpuDeviceCreatePipelineLayout(WGPUDevice handle,
    const WGPUPipelineLayoutDescriptor *descriptor)
{
    native_device *device = (native_device *)handle;
    if (!device) return NULL;
    doe_resource_layout *groups[DOE_RESOURCE_MAX_GROUPS];
    bool valid = descriptor && !descriptor->nextInChain &&
        descriptor->bindGroupLayoutCount <= DOE_RESOURCE_MAX_GROUPS &&
        (!descriptor->bindGroupLayoutCount || descriptor->bindGroupLayouts);
    if (valid) for (size_t i = 0; i < descriptor->bindGroupLayoutCount; ++i)
        groups[i] = (doe_resource_layout *)descriptor->bindGroupLayouts[i];
    doe_resource_pipeline_layout *layout = NULL;
    native_lock(device);
    doe_resource_status status = valid ? doe_resource_pipeline_layout_create(&device->resources,
        groups, descriptor->bindGroupLayoutCount, descriptor->immediateSize, &layout) : DOE_RESOURCE_UNSUPPORTED;
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, WGPUErrorType_Validation, "pipeline layout unsupported or invalid");
    return (WGPUPipelineLayout)layout;
}
void wgpuPipelineLayoutAddRef(WGPUPipelineLayout layout) { doe_resource_pipeline_layout_retain((doe_resource_pipeline_layout *)layout); }
void wgpuPipelineLayoutRelease(WGPUPipelineLayout layout) { doe_resource_pipeline_layout_release((doe_resource_pipeline_layout *)layout); }

WGPUBindGroup wgpuDeviceCreateBindGroup(WGPUDevice handle, const WGPUBindGroupDescriptor *descriptor)
{
    native_device *device = (native_device *)handle;
    if (!device) return NULL;
    doe_resource_binding bindings[DOE_RESOURCE_MAX_BINDINGS];
    bool valid = descriptor && !descriptor->nextInChain && descriptor->layout &&
        descriptor->entryCount <= DOE_RESOURCE_MAX_BINDINGS && (!descriptor->entryCount || descriptor->entries);
    if (valid) for (size_t i = 0; i < descriptor->entryCount; ++i) {
        const WGPUBindGroupEntry *entry = &descriptor->entries[i];
        if (entry->nextInChain || !entry->buffer || entry->sampler || entry->textureView) { valid = false; break; }
        bindings[i] = (doe_resource_binding){
            .binding = entry->binding, .kind = DOE_BIND_BUFFER,
            .resource.buffer = {(doe_resource_buffer *)entry->buffer, entry->offset, entry->size}
        };
    }
    doe_resource_binding_limits limits = {
        .uniform_alignment = device->caps.uniform_offset_alignment,
        .storage_alignment = device->caps.storage_offset_alignment,
        .max_uniform_size = device->caps.max_uniform_buffer_range,
        .max_storage_size = device->caps.max_storage_buffer_range
    };
    doe_resource_bind_group *group = NULL;
    native_lock(device);
    if (valid && doe_resource_layout_device((doe_resource_layout *)descriptor->layout) != device) valid = false;
    doe_resource_status status = valid ? doe_resource_bind_group_create((doe_resource_layout *)descriptor->layout,
        bindings, descriptor->entryCount, &limits, &group) : DOE_RESOURCE_INVALID_ARGUMENT;
    native_unlock(device);
    if (status != DOE_RESOURCE_OK) native_error(device, WGPUErrorType_Validation, "bind group invalid or device mismatch");
    return (WGPUBindGroup)group;
}
void wgpuBindGroupAddRef(WGPUBindGroup group) { doe_resource_bind_group_retain((doe_resource_bind_group *)group); }
void wgpuBindGroupRelease(WGPUBindGroup group) { doe_resource_bind_group_release((doe_resource_bind_group *)group); }
