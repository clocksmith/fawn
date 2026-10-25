/* SPDX-License-Identifier: MIT */
#include "internal.h"

/* SPEC DEVIATION, not finished: WebGPU default layouts report 'float' only for
 * an f32 texture statically used in a texture builtin call together with a
 * sampler (https://gpuweb.github.io/gpuweb/#default-pipeline-layout), otherwise
 * 'unfilterable-float'. The compiler reflects no texture/sampler pair use, so
 * once a pipeline references any sampler every f32 texture becomes 'float': an
 * r32float texture used only by textureLoad is then rejected at createBindGroup.
 * Only a pipeline without samplers is exact. The reflection needed from the
 * compiler is requested in the team log. */
static bool stages_sample(const native_stage *stages, size_t stage_count)
{
    for (size_t s = 0; s < stage_count; ++s) {
        const DoeCompilerEntryV5 *entry = stages[s].entry;
        for (uint32_t i = 0; i < entry->resource_reference_count; ++i) {
            uint32_t index = stages[s].view.entry_references[entry->first_resource_reference + i];
            if (stages[s].view.resources[index].kind == DOE_COMPILER_SAMPLER) return true;
        }
    }
    return false;
}

static bool resource_entry(const doe_shader_view *view, uint32_t index, WGPUShaderStage visibility, bool sampled,
    doe_resource_layout_entry *out)
{
    const DoeCompilerResourceV4 *resource = &view->resources[index];
    if (resource->group >= DOE_RESOURCE_MAX_GROUPS || resource->binding >= DOE_RESOURCE_MAX_BINDING_NUMBER) return false;
    *out = (doe_resource_layout_entry){.binding=resource->binding, .visibility=visibility};
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
    /* texture_multisampled_2d<f32> defaults to 'unfilterable-float' (multisampled
     * float is invalid); depth multisampled stays 'depth'. */
    out->multisampled = (handle->flags & DOE_COMPILER_HANDLE_MULTISAMPLED) != 0;
    switch (handle->view_dimension) {
    case DOE_COMPILER_VIEW_1D: out->view_dimension = WGPUTextureViewDimension_1D; break;
    case DOE_COMPILER_VIEW_2D: out->view_dimension = WGPUTextureViewDimension_2D; break;
    case DOE_COMPILER_VIEW_2D_ARRAY: out->view_dimension = WGPUTextureViewDimension_2DArray; break;
    case DOE_COMPILER_VIEW_3D: out->view_dimension = WGPUTextureViewDimension_3D; break;
    case DOE_COMPILER_VIEW_CUBE: out->view_dimension = WGPUTextureViewDimension_Cube; break;
    case DOE_COMPILER_VIEW_CUBE_ARRAY: out->view_dimension = WGPUTextureViewDimension_CubeArray; break;
    default: return false;
    }
    if (resource->kind == DOE_COMPILER_SAMPLED_TEXTURE && (handle->flags & DOE_COMPILER_HANDLE_DEPTH)) {
        /* texture_depth_* reflects the WebGPU default 'depth' sample type. */
        out->kind = DOE_BIND_TEXTURE;
        out->sample_type = WGPUTextureSampleType_Depth;
    } else if (resource->kind == DOE_COMPILER_SAMPLED_TEXTURE) {
        out->kind = DOE_BIND_TEXTURE;
        switch (handle->sample_scalar) {
        case DOE_COMPILER_LEAF_F32:
            out->sample_type = sampled && !out->multisampled ? WGPUTextureSampleType_Float :
                WGPUTextureSampleType_UnfilterableFloat;
            break;
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

static bool merge_entry(doe_resource_layout_entry *actual, const doe_resource_layout_entry *next)
{
    if (actual->kind != next->kind || actual->buffer_type != next->buffer_type ||
        actual->sampler_type != next->sampler_type || actual->sample_type != next->sample_type ||
        actual->view_dimension != next->view_dimension || actual->storage_access != next->storage_access ||
        actual->storage_format != next->storage_format || actual->multisampled != next->multisampled) return false;
    actual->visibility |= next->visibility;
    if (actual->minimum_size < next->minimum_size) actual->minimum_size = next->minimum_size;
    return true;
}

doe_resource_status native_automatic_layout(native_device *device,
    const native_stage *stages, size_t stage_count, doe_resource_pipeline_layout **out)
{
    /* One group holds at most every resource the pipeline references. */
    doe_resource_layout_entry entries[DOE_RESOURCE_MAX_GROUPS][NATIVE_MAX_PIPELINE_BINDINGS];
    size_t counts[DOE_RESOURCE_MAX_GROUPS] = {0};
    size_t group_count = 0;
    const bool sampled = stages_sample(stages, stage_count); /* Spec deviation: see stages_sample. */
    for (size_t stage_index = 0; stage_index < stage_count; ++stage_index) {
        const native_stage *stage = &stages[stage_index];
        const doe_shader_view *view = &stage->view;
        const DoeCompilerEntryV5 *entry = stage->entry;
        for (uint32_t i = 0; i < entry->resource_reference_count; ++i) {
            uint32_t index = view->entry_references[entry->first_resource_reference + i];
            const DoeCompilerResourceV4 *resource = &view->resources[index];
            doe_resource_layout_entry converted;
            if (!resource_entry(view, index, stage->visibility, sampled, &converted)) return DOE_RESOURCE_UNSUPPORTED;
            if (resource->binding >= device->backend.limits.maxBindingsPerBindGroup ||
                resource->group >= device->backend.limits.maxBindGroups) return DOE_RESOURCE_RANGE;
            size_t at = 0;
            while (at < counts[resource->group] && entries[resource->group][at].binding != resource->binding) ++at;
            if (at < counts[resource->group]) {
                if (!merge_entry(&entries[resource->group][at], &converted)) return DOE_RESOURCE_LAYOUT;
            } else {
                if (at == NATIVE_MAX_PIPELINE_BINDINGS) return DOE_RESOURCE_UNSUPPORTED;
                entries[resource->group][at] = converted;
                ++counts[resource->group];
            }
            if (group_count <= resource->group) group_count = resource->group + 1;
        }
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

static bool compatible(const doe_resource_layout_entry *actual, const doe_resource_layout_entry *required)
{
    if (actual->kind != required->kind || (actual->visibility & required->visibility) != required->visibility) return false;
    switch (required->kind) {
    case DOE_BIND_BUFFER:
        return actual->buffer_type == required->buffer_type &&
            (!actual->minimum_size || actual->minimum_size >= required->minimum_size);
    case DOE_BIND_SAMPLER:
        return (actual->sampler_type == WGPUSamplerBindingType_Comparison) ==
            (required->sampler_type == WGPUSamplerBindingType_Comparison);
    case DOE_BIND_TEXTURE:
        return actual->view_dimension == required->view_dimension && actual->multisampled == required->multisampled &&
            (actual->sample_type == required->sample_type ||
                (required->sample_type == WGPUTextureSampleType_Float &&
                actual->sample_type == WGPUTextureSampleType_UnfilterableFloat) ||
                (required->sample_type == WGPUTextureSampleType_UnfilterableFloat &&
                actual->sample_type == WGPUTextureSampleType_Float));
    case DOE_BIND_STORAGE_TEXTURE:
        /* https://gpuweb.github.io/gpuweb/#abstract-opdef-validating-shader-binding:
         * a "read-write" entry admits shader access read_write or write. */
        return actual->view_dimension == required->view_dimension && actual->storage_format == required->storage_format &&
            (actual->storage_access == required->storage_access ||
                (actual->storage_access == WGPUStorageTextureAccess_ReadWrite &&
                required->storage_access == WGPUStorageTextureAccess_WriteOnly));
    default: return false;
    }
}

bool native_pipeline_bindings(native_pipeline *payload, const native_stage *stages,
    size_t stage_count, doe_resource_pipeline_layout *layout)
{
    bool unfilterable = false, filtering = false;
    const bool sampled = stages_sample(stages, stage_count);
    for (size_t stage_index = 0; stage_index < stage_count; ++stage_index) {
        const native_stage *stage = &stages[stage_index];
        const doe_shader_view *view = &stage->view;
        const DoeCompilerEntryV5 *entry = stage->entry;
        const WGPULimits *limits = &payload->device->backend.limits;
        uint32_t uniform_count = 0, storage_count = 0, texture_count = 0, storage_texture_count = 0, sampler_count = 0;
        for (uint32_t i = 0; i < entry->resource_reference_count; ++i) {
            uint32_t index = view->entry_references[entry->first_resource_reference + i];
            const DoeCompilerResourceV4 *resource = &view->resources[index];
            switch (resource->kind) {
            case DOE_COMPILER_UNIFORM_BUFFER:
                if (++uniform_count > limits->maxUniformBuffersPerShaderStage) return false;
                break;
            case DOE_COMPILER_STORAGE_BUFFER:
                if (++storage_count > limits->maxStorageBuffersPerShaderStage) return false;
                break;
            case DOE_COMPILER_SAMPLED_TEXTURE:
                if (++texture_count > limits->maxSampledTexturesPerShaderStage) return false;
                break;
            case DOE_COMPILER_STORAGE_TEXTURE:
                if (++storage_texture_count > limits->maxStorageTexturesPerShaderStage) return false;
                break;
            case DOE_COMPILER_SAMPLER:
                if (++sampler_count > limits->maxSamplersPerShaderStage) return false;
                break;
            default: return false;
            }
            /* Vertex stores remain forbidden. Vulkan requires its enabled
             * fragmentStoresAndAtomics capability. Metal device-buffer stores
             * and atomics use the same typed resource/command owner as compute;
             * fragment storage textures retain their existing rejection. */
            if (stage->visibility != WGPUShaderStage_Compute && resource->access != DOE_COMPILER_READ &&
                (resource->kind == DOE_COMPILER_STORAGE_BUFFER || resource->kind == DOE_COMPILER_STORAGE_TEXTURE)) {
                const bool vulkan = payload->device->backend.kind == WGPUBackendType_Vulkan &&
                    payload->device->backend.value.vulkan.caps.fragment_stores_and_atomics;
                const bool metal_buffer = payload->device->backend.kind == WGPUBackendType_Metal &&
                    resource->kind == DOE_COMPILER_STORAGE_BUFFER;
                if (stage->visibility != WGPUShaderStage_Fragment || (!vulkan && !metal_buffer)) return false;
            }
            doe_resource_layout_entry required;
            if (!resource_entry(view, index, stage->visibility, sampled, &required)) return false;
            doe_resource_layout *group = doe_resource_pipeline_layout_group(layout, resource->group);
            size_t count = 0;
            const doe_resource_layout_entry *entries = doe_resource_layout_entries(group, &count);
            const doe_resource_layout_entry *actual = NULL;
            for (size_t j = 0; j < count; ++j) if (entries[j].binding == resource->binding) actual = &entries[j];
            if (!actual || !compatible(actual, &required)) return false;
            /* A multisampled texture never pairs with a sampler (textureLoad only),
             * so only single-sample unfilterable textures enter the pair check. */
            if (actual->kind == DOE_BIND_TEXTURE && !actual->multisampled &&
                actual->sample_type == WGPUTextureSampleType_UnfilterableFloat) unfilterable = true;
            if (actual->kind == DOE_BIND_SAMPLER && actual->sampler_type == WGPUSamplerBindingType_Filtering) filtering = true;
            size_t at = 0;
            while (at < payload->binding_count && (payload->bindings[at].group != resource->group ||
                payload->bindings[at].binding != resource->binding)) ++at;
            if (at == payload->binding_count) {
                if (at == NATIVE_MAX_PIPELINE_BINDINGS) return false;
                payload->bindings[at] = (native_binding){resource->group, resource->binding, required.kind, required.buffer_type};
                ++payload->binding_count;
            } else if (payload->bindings[at].kind != required.kind ||
                payload->bindings[at].buffer_type != required.buffer_type) return false;
            if (payload->minimum_sizes[at] < resource->minimum_binding_size_bytes)
                payload->minimum_sizes[at] = resource->minimum_binding_size_bytes;
        }
    }
    /* The compiler owns resource closure, not precise texture/sampler pair-use
     * through helper formals. This ambiguous combination remains unsupported. */
    return !(unfilterable && filtering);
}
