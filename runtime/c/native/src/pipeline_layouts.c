/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "native_import_layout.h"

/* ABI17 owns exact byte accessibility; native code never reconstructs WGSL layout. */
static bool immediate_requirements(const native_stage *stages, size_t count,
    uint32_t *bytes, uint64_t *mask, uint32_t *visibility)
{
    *bytes = 0;
    *mask = 0;
    *visibility = 0;
    for (size_t s = 0; s < count; ++s) {
        if (stages[s].imported) continue; /* Explicit native contract, no compiler completeness claim. */
        const doe_shader_view *view = &stages[s].view;
        if (!view->translation_complete || !view->entries || !stages[s].entry ||
            (view->translation_entry_count && !view->translation_entries)) return false;
        size_t index = 0;
        while (index < view->counts.entries && &view->entries[index] != stages[s].entry) ++index;
        if (index == view->counts.entries) return false;
        const DoeCompilerTranslationEntryV17 *found = NULL;
        for (size_t i = 0; i < view->translation_entry_count; ++i) {
            const DoeCompilerTranslationEntryV17 *row = &view->translation_entries[i];
            if (row->entry_index != index) continue;
            if (found || row->flags || row->reserved || row->immediate_bytes > 64) return false;
            found = row;
        }
        if (!found) return false;
        uint64_t allowed = found->immediate_bytes == 64 ? UINT64_MAX :
            ((UINT64_C(1) << found->immediate_bytes) - 1u);
        if (found->immediate_accessible_bytes_low64 & ~allowed) return false;
        uint32_t aligned = (found->immediate_bytes + 3u) & ~3u;
        if (aligned > *bytes) *bytes = aligned;
        *mask |= found->immediate_accessible_bytes_low64;
        if (found->immediate_bytes) *visibility |= stages[s].visibility;
    }
    return true;
}

/* Immutable compiler pairs describe actual sample/gather calls, including
 * helper formal substitution. Resource identities are local to each stage. */
static bool sampling_complete(const native_stage *stages, size_t count)
{
    for (size_t s = 0; s < count; ++s) {
        if (stages[s].imported) continue; /* Explicit native contract, no compiler completeness claim. */
        const doe_shader_view *view = &stages[s].view;
        if (!view->sampling_pairs_complete || !view->entries || !stages[s].entry ||
            (view->sampling_pair_count && (!view->sampling_pairs || !view->resources))) return false;
        size_t entry = 0;
        while (entry < view->counts.entries && &view->entries[entry] != stages[s].entry) ++entry;
        if (entry == view->counts.entries) return false;
        for (size_t i = 0; i < view->sampling_pair_count; ++i) {
            const DoeCompilerSamplingPairV16 *pair = &view->sampling_pairs[i];
            if (pair->reserved || pair->entry_index != entry ||
                pair->texture_resource_index >= view->counts.resources ||
                pair->sampler_resource_index >= view->counts.resources ||
                view->resources[pair->texture_resource_index].kind != DOE_COMPILER_SAMPLED_TEXTURE ||
                view->resources[pair->sampler_resource_index].kind != DOE_COMPILER_SAMPLER) return false;
        }
    }
    return true;
}
static bool texture_sampled(const native_stage *stages, size_t count, const DoeCompilerResourceV4 *resource)
{
    for (size_t s = 0; s < count; ++s) {
        if (stages[s].imported) continue; /* Explicit native contract, no compiler completeness claim. */
        const doe_shader_view *view = &stages[s].view;
        for (size_t i = 0; i < view->sampling_pair_count; ++i) {
            const DoeCompilerResourceV4 *texture = &view->resources[view->sampling_pairs[i].texture_resource_index];
            if (texture->group == resource->group && texture->binding == resource->binding) return true;
        }
    }
    return false;
}
static const doe_resource_layout_entry *layout_entry(doe_resource_pipeline_layout *layout,
    const DoeCompilerResourceV4 *resource)
{
    doe_resource_layout *group = doe_resource_pipeline_layout_group(layout, resource->group);
    size_t count = 0;
    const doe_resource_layout_entry *entries = doe_resource_layout_entries(group, &count);
    for (size_t i = 0; i < count; ++i) if (entries[i].binding == resource->binding) return &entries[i];
    return NULL;
}
static bool sampling_layout(const native_stage *stages, size_t count, doe_resource_pipeline_layout *layout)
{
    for (size_t s = 0; s < count; ++s) {
        if (stages[s].imported) continue; /* Explicit native contract, no compiler completeness claim. */
        const doe_shader_view *view = &stages[s].view;
        for (size_t i = 0; i < view->sampling_pair_count; ++i) {
            const DoeCompilerSamplingPairV16 *pair = &view->sampling_pairs[i];
            const doe_resource_layout_entry *texture = layout_entry(layout, &view->resources[pair->texture_resource_index]);
            const doe_resource_layout_entry *sampler = layout_entry(layout, &view->resources[pair->sampler_resource_index]);
            if (!texture || !sampler || (texture->sample_type == WGPUTextureSampleType_UnfilterableFloat &&
                sampler->sampler_type == WGPUSamplerBindingType_Filtering)) return false;
        }
    }
    return true;
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
    if (handle->flags & DOE_COMPILER_HANDLE_EXTERNAL) {
        if (resource->kind != DOE_COMPILER_SAMPLED_TEXTURE || resource->access != DOE_COMPILER_READ ||
            handle->flags != DOE_COMPILER_HANDLE_EXTERNAL || handle->view_dimension != DOE_COMPILER_VIEW_2D ||
            handle->sample_scalar != DOE_COMPILER_LEAF_F32) return false;
        /* Keep the external handle in the canonical layout/command leases.
         * Target adapters resolve its retained plane-zero compatibility view. */
        out->kind = DOE_BIND_EXTERNAL_TEXTURE;
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
    uint32_t immediate_bytes, immediate_visibility;
    uint64_t immediate_mask;
    if (!immediate_requirements(stages, stage_count, &immediate_bytes, &immediate_mask, &immediate_visibility))
        return DOE_RESOURCE_UNSUPPORTED;
    /* One group holds at most every resource the pipeline references. */
    doe_resource_layout_entry entries[DOE_RESOURCE_MAX_GROUPS][NATIVE_MAX_PIPELINE_BINDINGS];
    size_t counts[DOE_RESOURCE_MAX_GROUPS] = {0};
    size_t group_count = 0;
    if (!sampling_complete(stages, stage_count)) return DOE_RESOURCE_UNSUPPORTED;
    for (size_t stage_index = 0; stage_index < stage_count; ++stage_index) {
        const native_stage *stage = &stages[stage_index];
        if (stage->imported) continue; /* No inferred resources: automatic imported layout stays empty. */
        const doe_shader_view *view = &stage->view;
        const DoeCompilerEntryV5 *entry = stage->entry;
        for (uint32_t i = 0; i < entry->resource_reference_count; ++i) {
            uint32_t index = view->entry_references[entry->first_resource_reference + i];
            const DoeCompilerResourceV4 *resource = &view->resources[index];
            doe_resource_layout_entry converted;
            if (!resource_entry(view, index, stage->visibility, texture_sampled(stages, stage_count, resource), &converted)) return DOE_RESOURCE_UNSUPPORTED;
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
    status = doe_resource_pipeline_layout_create(&device->resources, groups, group_count, immediate_bytes, out);
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
    case DOE_BIND_EXTERNAL_TEXTURE:
        return true; /* Kind and stage visibility were checked above. */
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
    if (!sampling_complete(stages, stage_count)) return false;
    if (!immediate_requirements(stages, stage_count, &payload->immediate_bytes,
            &payload->immediate_required_bytes, &payload->immediate_stages) || payload->immediate_bytes >
            doe_resource_pipeline_layout_immediate_size(layout)) return false;
    for (size_t stage_index = 0; stage_index < stage_count; ++stage_index) {
        const native_stage *stage = &stages[stage_index];
        if (stage->imported) {
            if (!native_import_layout_bindings(payload,stage,layout)) return false;
            continue;
        }
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
             * and texture writes use the same typed resource/command owner as
             * compute, including attachment alias checks and retained views. */
            if (stage->visibility != WGPUShaderStage_Compute && resource->access != DOE_COMPILER_READ &&
                (resource->kind == DOE_COMPILER_STORAGE_BUFFER || resource->kind == DOE_COMPILER_STORAGE_TEXTURE)) {
                const bool vulkan = payload->device->backend.kind == WGPUBackendType_Vulkan &&
                    payload->device->backend.value.vulkan.caps.fragment_stores_and_atomics;
                const bool metal = payload->device->backend.kind == WGPUBackendType_Metal;
                const doe_dx_caps *dx = &payload->device->backend.value.d3d12.caps;
                const bool d3d12 = payload->device->backend.kind == WGPUBackendType_D3D12 &&
                    dx->feature_level >= 0xb000 && dx->shader_model >= 0x60;
                if (stage->visibility != WGPUShaderStage_Fragment || (!vulkan && !metal && !d3d12)) return false;
            }
            doe_resource_layout_entry required;
            if (!resource_entry(view, index, stage->visibility, texture_sampled(stages, stage_count, resource), &required)) return false;
            const doe_resource_layout_entry *actual = layout_entry(layout, resource);
            if (!actual || !compatible(actual, &required)) return false;
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
    return sampling_layout(stages, stage_count, layout);
}

size_t native_pipeline_minimums(const native_pipeline *payload,
    doe_resource_binding_minimum out[NATIVE_MAX_PIPELINE_BINDINGS])
{
    size_t count = 0;
    for (size_t i = 0; i < payload->binding_count; ++i)
        if (payload->bindings[i].kind == DOE_BIND_BUFFER && payload->minimum_sizes[i])
            out[count++] = (doe_resource_binding_minimum){payload->bindings[i].group,
                payload->bindings[i].binding, payload->minimum_sizes[i]};
    return count;
}
