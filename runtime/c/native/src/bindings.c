/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <stdlib.h>

WGPUBindGroupLayout wgpuDeviceCreateBindGroupLayout(WGPUDevice handle,
    const WGPUBindGroupLayoutDescriptor *descriptor)
{
    native_device *device = (native_device *)handle;
    if (!device) return NULL;
    if (native_vulkan_dead(device)) return native_lost_bind_group_layout(device, (descriptor ? descriptor->label : (WGPUStringView){NULL, 0}));
    /* maxBindingsPerBindGroup bounds binding numbers, so distinct entries too. */
    bool valid = descriptor && !descriptor->nextInChain &&
        descriptor->entryCount <= device->backend.limits.maxBindingsPerBindGroup &&
        (!descriptor->entryCount || descriptor->entries);
    doe_resource_layout_entry *entries = valid ? calloc(descriptor->entryCount ? descriptor->entryCount : 1,
        sizeof(*entries)) : NULL;
    const bool out_of_memory = valid && !entries;
    valid = valid && entries;
    if (valid) for (size_t i = 0; i < descriptor->entryCount; ++i) {
        const WGPUBindGroupLayoutEntry *entry = &descriptor->entries[i];
        /* An externalTexture entry is a chained WGPUExternalTextureBindingLayout
         * and no other binding type. */
        const bool external = entry->nextInChain && !entry->nextInChain->next &&
            entry->nextInChain->sType == WGPUSType_ExternalTextureBindingLayout;
        unsigned kinds = (entry->buffer.type != WGPUBufferBindingType_BindingNotUsed) +
            (entry->sampler.type != WGPUSamplerBindingType_BindingNotUsed) +
            (entry->texture.sampleType != WGPUTextureSampleType_BindingNotUsed) +
            (entry->storageTexture.access != WGPUStorageTextureAccess_BindingNotUsed) + external;
        if ((entry->nextInChain && !external) || entry->bindingArraySize || entry->buffer.nextInChain ||
            entry->binding >= device->backend.limits.maxBindingsPerBindGroup ||
            entry->sampler.nextInChain || entry->texture.nextInChain || entry->storageTexture.nextInChain ||
            kinds != 1 || entry->visibility & ~(WGPUShaderStage_Compute | WGPUShaderStage_Vertex | WGPUShaderStage_Fragment)) {
            valid = false;
            break;
        }
        doe_resource_layout_entry converted = {.binding=entry->binding, .visibility=entry->visibility};
        if (external) {
            converted.kind = DOE_BIND_EXTERNAL_TEXTURE;
        } else if (entry->buffer.type) {
            converted.kind = DOE_BIND_BUFFER;
            converted.buffer_type = entry->buffer.type == WGPUBufferBindingType_Undefined ?
                WGPUBufferBindingType_Uniform : entry->buffer.type;
            converted.dynamic_offset = entry->buffer.hasDynamicOffset != 0;
            converted.minimum_size = entry->buffer.minBindingSize;
        } else if (entry->sampler.type) {
            converted.kind = DOE_BIND_SAMPLER;
            converted.sampler_type = entry->sampler.type == WGPUSamplerBindingType_Undefined ?
                WGPUSamplerBindingType_Filtering : entry->sampler.type;
        } else if (entry->texture.sampleType) {
            converted.kind = DOE_BIND_TEXTURE;
            converted.sample_type = entry->texture.sampleType == WGPUTextureSampleType_Undefined ?
                WGPUTextureSampleType_Float : entry->texture.sampleType;
            converted.view_dimension = entry->texture.viewDimension;
            converted.multisampled = entry->texture.multisampled != 0;
            /* Multisampled layouts follow the resource owner's float/dimension rules. */
            if ((converted.view_dimension == WGPUTextureViewDimension_CubeArray &&
                (device->backend.kind != WGPUBackendType_Vulkan || !device->backend.value.vulkan.caps.image_cube_array)))
                valid = false;
        } else {
            converted.kind = DOE_BIND_STORAGE_TEXTURE;
            converted.storage_access = entry->storageTexture.access == WGPUStorageTextureAccess_Undefined ?
                WGPUStorageTextureAccess_WriteOnly : entry->storageTexture.access;
            converted.storage_format = entry->storageTexture.format;
            converted.view_dimension = entry->storageTexture.viewDimension;
            doe_vk_texture_format format;
            if (!native_texture_format(device->features, converted.storage_format, &format)) valid = false;
        }
        entries[i] = converted;
    }
    doe_resource_layout *layout = NULL;
    native_lock(device);
    doe_resource_status status = out_of_memory ? DOE_RESOURCE_OUT_OF_MEMORY : valid ?
        doe_resource_layout_create(&device->resources, entries, descriptor->entryCount, false, &layout) :
        DOE_RESOURCE_UNSUPPORTED;
    native_unlock(device);
    free(entries);
    if (status != DOE_RESOURCE_OK) native_error(device, native_resource_error(status), "bind group layout unsupported or invalid");
    if (layout) native_label(device, layout, descriptor->label);
    return (WGPUBindGroupLayout)layout;
}
void wgpuBindGroupLayoutAddRef(WGPUBindGroupLayout layout) { doe_resource_layout_retain((doe_resource_layout *)layout); }
void wgpuBindGroupLayoutRelease(WGPUBindGroupLayout layout) { doe_resource_layout_release((doe_resource_layout *)layout); }

WGPUPipelineLayout wgpuDeviceCreatePipelineLayout(WGPUDevice handle,
    const WGPUPipelineLayoutDescriptor *descriptor)
{
    native_device *device = (native_device *)handle;
    if (!device) return NULL;
    if (native_vulkan_dead(device)) return native_lost_pipeline_layout(device, (descriptor ? descriptor->label : (WGPUStringView){NULL, 0}));
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
    if (status != DOE_RESOURCE_OK) native_error(device, native_resource_error(status), "pipeline layout unsupported or invalid");
    if (layout) native_label(device, layout, descriptor->label);
    return (WGPUPipelineLayout)layout;
}
void wgpuPipelineLayoutAddRef(WGPUPipelineLayout layout) { doe_resource_pipeline_layout_retain((doe_resource_pipeline_layout *)layout); }
void wgpuPipelineLayoutRelease(WGPUPipelineLayout layout) { doe_resource_pipeline_layout_release((doe_resource_pipeline_layout *)layout); }

static bool group_backend_compatible(native_device *device, doe_resource_layout *layout,
    doe_resource_bind_group *group)
{
    /* The resource owner has checked canonical format/feature filterability.
     * Vulkan additionally requires the selected device's native format caps. */
    if (device->backend.kind == WGPUBackendType_Metal) return true;
    size_t entry_count = 0, binding_count = 0;
    const doe_resource_layout_entry *entries = doe_resource_layout_entries(layout, &entry_count);
    if (device->backend.kind == WGPUBackendType_D3D12) {
        /* Canonical resources checked format/sample compatibility. D3D12 now
         * consumes whole 2D color views plus immutable runtime sampler values. */
        for (size_t i = 0; i < entry_count; ++i)
            if (entries[i].kind != DOE_BIND_BUFFER && entries[i].kind != DOE_BIND_TEXTURE &&
                entries[i].kind != DOE_BIND_STORAGE_TEXTURE && entries[i].kind != DOE_BIND_SAMPLER) return false;
        return true;
    }
    if (device->backend.kind != WGPUBackendType_Vulkan) return false;
    const doe_resource_binding *bindings = doe_resource_bind_group_bindings(group, &binding_count);
    for (size_t i = 0; i < entry_count; ++i) {
        if (entries[i].kind != DOE_BIND_TEXTURE || entries[i].sample_type != WGPUTextureSampleType_Float) continue;
        for (size_t j = 0; j < binding_count; ++j) if (entries[i].binding == bindings[j].binding) {
            const doe_resource_view_desc *view = doe_resource_view_description(bindings[j].resource.view);
            doe_vk_texture_format format;
            doe_vk_format_caps caps;
            if (!native_texture_format(device->features, view->format, &format) ||
                doe_vk_texture_format_caps(device->backend.value.vulkan.context, format, &caps) != DOE_VK_OK || !caps.filterable)
                return false;
        }
    }
    return true;
}

WGPUBindGroup wgpuDeviceCreateBindGroup(WGPUDevice handle, const WGPUBindGroupDescriptor *descriptor)
{
    native_device *device = (native_device *)handle;
    if (!device) return NULL;
    if (native_vulkan_dead(device)) return native_lost_bind_group(device, (descriptor ? descriptor->label : (WGPUStringView){NULL, 0}));
    bool valid = descriptor && !descriptor->nextInChain && descriptor->layout &&
        descriptor->entryCount <= device->backend.limits.maxBindingsPerBindGroup &&
        (!descriptor->entryCount || descriptor->entries);
    doe_resource_binding *bindings = valid ? calloc(descriptor->entryCount ? descriptor->entryCount : 1,
        sizeof(*bindings)) : NULL;
    const bool out_of_memory = valid && !bindings;
    valid = valid && bindings;
    if (valid) for (size_t i = 0; i < descriptor->entryCount; ++i) {
        const WGPUBindGroupEntry *entry = &descriptor->entries[i];
        unsigned kinds = (entry->buffer != NULL) + (entry->sampler != NULL) + (entry->textureView != NULL);
        if (entry->nextInChain || kinds != 1) { valid = false; break; }
        bindings[i] = (doe_resource_binding){.binding=entry->binding};
        if (entry->buffer) {
            bindings[i].kind = DOE_BIND_BUFFER;
            bindings[i].resource.buffer.buffer = (doe_resource_buffer *)entry->buffer;
            bindings[i].resource.buffer.offset = entry->offset;
            bindings[i].resource.buffer.size = entry->size;
        } else if (entry->sampler) {
            bindings[i].kind = DOE_BIND_SAMPLER;
            bindings[i].resource.sampler = (doe_resource_sampler *)entry->sampler;
        } else {
            bindings[i].resource.view = (doe_resource_view *)entry->textureView;
            size_t count = 0;
            const doe_resource_layout_entry *layout_entries =
                doe_resource_layout_entries((doe_resource_layout *)descriptor->layout, &count);
            for (size_t j = 0; j < count; ++j)
                if (layout_entries[j].binding == entry->binding) bindings[i].kind = layout_entries[j].kind;
            /* A view in an externalTexture slot binds as a sampled texture; the
             * resource owner checks the external-view rules. */
            if (bindings[i].kind == DOE_BIND_EXTERNAL_TEXTURE) bindings[i].kind = DOE_BIND_TEXTURE;
            if (bindings[i].kind != DOE_BIND_TEXTURE && bindings[i].kind != DOE_BIND_STORAGE_TEXTURE) valid = false;
        }
    }
    doe_resource_binding_limits limits = {
        .uniform_alignment = device->backend.limits.minUniformBufferOffsetAlignment,
        .storage_alignment = device->backend.limits.minStorageBufferOffsetAlignment,
        .max_uniform_size = device->backend.limits.maxUniformBufferBindingSize,
        .max_storage_size = device->backend.limits.maxStorageBufferBindingSize
    };
    doe_resource_bind_group *group = NULL;
    native_lock(device);
    if (valid && doe_resource_layout_device((doe_resource_layout *)descriptor->layout) != device) valid = false;
    /* float32-filterable promotes 32-bit float views to 'float' sample type; the
     * native filterability check below still requires linear-filter support. */
    const doe_resource_binding_features features = {
        .float32_filterable = native_feature_enabled(device, WGPUFeatureName_Float32Filterable)
    };
    doe_resource_status status = out_of_memory ? DOE_RESOURCE_OUT_OF_MEMORY : valid ?
        doe_resource_bind_group_create_with_features((doe_resource_layout *)descriptor->layout, bindings,
            descriptor->entryCount, &limits, &features, &group) : DOE_RESOURCE_INVALID_ARGUMENT;
    if (group && !group_backend_compatible(device, (doe_resource_layout *)descriptor->layout, group)) {
        doe_resource_bind_group_release(group);
        group = NULL;
        status = DOE_RESOURCE_UNSUPPORTED;
    }
    native_unlock(device);
    free(bindings);
    if (status != DOE_RESOURCE_OK) native_error(device, native_resource_error(status), "bind group invalid or device mismatch");
    if (group) native_label(device, group, descriptor->label);
    return (WGPUBindGroup)group;
}
void wgpuBindGroupAddRef(WGPUBindGroup group) { doe_resource_bind_group_retain((doe_resource_bind_group *)group); }
void wgpuBindGroupRelease(WGPUBindGroup group) { doe_resource_bind_group_release((doe_resource_bind_group *)group); }
