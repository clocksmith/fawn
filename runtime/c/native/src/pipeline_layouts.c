/* SPDX-License-Identifier: MIT */
#include "internal.h"

static bool resource_entry(const doe_shader_view *view, uint32_t index, doe_resource_layout_entry *out)
{
    const DoeCompilerResourceV4 *resource = &view->resources[index];
    if (resource->group >= DOE_RESOURCE_MAX_GROUPS || resource->binding >= DOE_RESOURCE_MAX_BINDINGS) return false;
    *out = (doe_resource_layout_entry){.binding=resource->binding, .visibility=WGPUShaderStage_Compute};
    if (resource->kind == DOE_COMPILER_UNIFORM_BUFFER || resource->kind == DOE_COMPILER_STORAGE_BUFFER) {
        out->kind = DOE_BIND_BUFFER;
        out->buffer_type = resource->kind == DOE_COMPILER_UNIFORM_BUFFER ? WGPUBufferBindingType_Uniform :
            resource->access == DOE_COMPILER_READ ? WGPUBufferBindingType_ReadOnlyStorage : WGPUBufferBindingType_Storage;
        out->minimum_size = resource->minimum_binding_size_bytes;
        return true;
    }
    const DoeCompilerHandleV6 *handle = NULL;
    for (size_t i = 0; i < view->handle_count; ++i)
        if (view->handles[i].resource_index == index) handle = &view->handles[i];
    if (!handle) return false;
    if (resource->kind == DOE_COMPILER_SAMPLER) {
        out->kind = DOE_BIND_SAMPLER;
        out->sampler_type = handle->flags & DOE_COMPILER_HANDLE_COMPARISON ?
            WGPUSamplerBindingType_Comparison : WGPUSamplerBindingType_Filtering;
        return true;
    }
    if (handle->flags & (DOE_COMPILER_HANDLE_DEPTH | DOE_COMPILER_HANDLE_MULTISAMPLED)) return false;
    switch (handle->view_dimension) {
    case DOE_COMPILER_VIEW_1D: out->view_dimension = WGPUTextureViewDimension_1D; break;
    case DOE_COMPILER_VIEW_2D: out->view_dimension = WGPUTextureViewDimension_2D; break;
    case DOE_COMPILER_VIEW_2D_ARRAY: out->view_dimension = WGPUTextureViewDimension_2DArray; break;
    case DOE_COMPILER_VIEW_3D: out->view_dimension = WGPUTextureViewDimension_3D; break;
    case DOE_COMPILER_VIEW_CUBE: out->view_dimension = WGPUTextureViewDimension_Cube; break;
    default: return false;
    }
    if (resource->kind == DOE_COMPILER_SAMPLED_TEXTURE) {
        out->kind = DOE_BIND_TEXTURE;
        switch (handle->sample_scalar) {
        case DOE_COMPILER_LEAF_F32: out->sample_type = WGPUTextureSampleType_Float; break;
        case DOE_COMPILER_LEAF_I32: out->sample_type = WGPUTextureSampleType_Sint; break;
        case DOE_COMPILER_LEAF_U32: out->sample_type = WGPUTextureSampleType_Uint; break;
        default: return false;
        }
    } else if (resource->kind == DOE_COMPILER_STORAGE_TEXTURE) {
        out->kind = DOE_BIND_STORAGE_TEXTURE;
        if (!native_storage_format(handle->storage_format, &out->storage_format)) return false;
        switch (resource->access) {
        case DOE_COMPILER_READ: out->storage_access = WGPUStorageTextureAccess_ReadOnly; break;
        case DOE_COMPILER_WRITE: out->storage_access = WGPUStorageTextureAccess_WriteOnly; break;
        case DOE_COMPILER_READ_WRITE: out->storage_access = WGPUStorageTextureAccess_ReadWrite; break;
        default: return false;
        }
    } else return false;
    return true;
}

doe_resource_status native_automatic_layout(native_device *device,
    const doe_shader_view *view, const DoeCompilerEntryV5 *entry, doe_resource_pipeline_layout **out)
{
    doe_resource_layout_entry entries[DOE_RESOURCE_MAX_GROUPS][DOE_RESOURCE_MAX_BINDINGS];
    size_t counts[DOE_RESOURCE_MAX_GROUPS] = {0};
    size_t group_count = 0;
    for (uint32_t i = 0; i < entry->resource_reference_count; ++i) {
        uint32_t index = view->entry_references[entry->first_resource_reference + i];
        const DoeCompilerResourceV4 *resource = &view->resources[index];
        doe_resource_layout_entry converted;
        if (!resource_entry(view, index, &converted) || counts[resource->group] == DOE_RESOURCE_MAX_BINDINGS) return DOE_RESOURCE_UNSUPPORTED;
        entries[resource->group][counts[resource->group]++] = converted;
        if (group_count <= resource->group) group_count = resource->group + 1;
    }
    doe_resource_layout *groups[DOE_RESOURCE_MAX_GROUPS] = {0};
    doe_resource_status status = DOE_RESOURCE_OK;
    for (size_t i = 0; i < group_count; ++i) {
        status = doe_resource_layout_create(&device->resources, entries[i], counts[i], true, &groups[i]);
        if (status != DOE_RESOURCE_OK) goto finished;
    }
    status = doe_resource_pipeline_layout_create(&device->resources, groups, group_count, 0, out);
finished:
    for (size_t i = 0; i < group_count; ++i) doe_resource_layout_release(groups[i]);
    return status;
}

static bool compatible(const doe_resource_layout_entry *actual, const doe_resource_layout_entry *required,
    doe_vk_binding_kind *kind)
{
    if (actual->kind != required->kind || !(actual->visibility & WGPUShaderStage_Compute)) return false;
    switch (required->kind) {
    case DOE_BIND_BUFFER:
        *kind = required->buffer_type == WGPUBufferBindingType_Uniform ? DOE_VK_BINDING_UNIFORM : DOE_VK_BINDING_STORAGE;
        return actual->buffer_type == required->buffer_type &&
            (!actual->minimum_size || actual->minimum_size >= required->minimum_size);
    case DOE_BIND_SAMPLER:
        *kind = DOE_VK_BINDING_SAMPLER;
        return (actual->sampler_type == WGPUSamplerBindingType_Comparison) ==
            (required->sampler_type == WGPUSamplerBindingType_Comparison);
    case DOE_BIND_TEXTURE:
        *kind = DOE_VK_BINDING_SAMPLED_TEXTURE;
        return actual->view_dimension == required->view_dimension && actual->multisampled == required->multisampled &&
            (actual->sample_type == required->sample_type || (required->sample_type == WGPUTextureSampleType_Float &&
                actual->sample_type == WGPUTextureSampleType_UnfilterableFloat));
    case DOE_BIND_STORAGE_TEXTURE:
        *kind = DOE_VK_BINDING_STORAGE_TEXTURE;
        return actual->view_dimension == required->view_dimension &&
            actual->storage_access == required->storage_access && actual->storage_format == required->storage_format;
    default: return false;
    }
}

bool native_pipeline_bindings(native_pipeline *payload, const doe_shader_view *view,
    const DoeCompilerEntryV5 *entry, doe_resource_pipeline_layout *layout)
{
    bool unfilterable = false, filtering = false;
    for (uint32_t i = 0; i < entry->resource_reference_count; ++i) {
        uint32_t index = view->entry_references[entry->first_resource_reference + i];
        const DoeCompilerResourceV4 *resource = &view->resources[index];
        doe_resource_layout_entry required;
        if (!resource_entry(view, index, &required)) return false;
        doe_resource_layout *group = doe_resource_pipeline_layout_group(layout, resource->group);
        size_t count = 0;
        const doe_resource_layout_entry *entries = doe_resource_layout_entries(group, &count);
        const doe_resource_layout_entry *actual = NULL;
        for (size_t j = 0; j < count; ++j) if (entries[j].binding == resource->binding) actual = &entries[j];
        doe_vk_binding_kind kind;
        if (!actual || !compatible(actual, &required, &kind)) return false;
        if (actual->kind == DOE_BIND_TEXTURE && actual->sample_type == WGPUTextureSampleType_UnfilterableFloat) unfilterable = true;
        if (actual->kind == DOE_BIND_SAMPLER && actual->sampler_type == WGPUSamplerBindingType_Filtering) filtering = true;
        size_t at = payload->binding_count++;
        payload->bindings[at] = (doe_vk_binding_layout){resource->group, resource->binding, kind};
        payload->minimum_sizes[at] = resource->minimum_binding_size_bytes;
    }
    /* ABI6 owns resource closure, not texture/sampler pairing through helper
     * formals. Reject this ambiguous combination rather than guessing usage. */
    return !(unfilterable && filtering);
}
