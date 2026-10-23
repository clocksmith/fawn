/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <string.h>

static doe_resource_status normalize_entry(const doe_resource_layout_entry *input,
    doe_resource_layout_entry *out)
{
    if (input->binding >= DOE_RESOURCE_MAX_BINDINGS) return DOE_RESOURCE_UNSUPPORTED;
    if (input->visibility & ~(WGPUShaderStage_Vertex | WGPUShaderStage_Fragment | WGPUShaderStage_Compute))
        return DOE_RESOURCE_LAYOUT;
    doe_resource_layout_entry result = {0};
    result.binding = input->binding;
    result.visibility = input->visibility;
    result.kind = input->kind;
    switch (input->kind) {
    case DOE_BIND_BUFFER:
        if (input->buffer_type < WGPUBufferBindingType_Uniform ||
            input->buffer_type > WGPUBufferBindingType_ReadOnlyStorage) return DOE_RESOURCE_LAYOUT;
        result.buffer_type = input->buffer_type;
        result.dynamic_offset = input->dynamic_offset;
        result.minimum_size = input->minimum_size;
        break;
    case DOE_BIND_EXTERNAL_TEXTURE:
        break;
    case DOE_BIND_SAMPLER:
        if (input->sampler_type < WGPUSamplerBindingType_Filtering ||
            input->sampler_type > WGPUSamplerBindingType_Comparison) return DOE_RESOURCE_LAYOUT;
        result.sampler_type = input->sampler_type;
        break;
    case DOE_BIND_TEXTURE:
        if (input->sample_type < WGPUTextureSampleType_Float || input->sample_type > WGPUTextureSampleType_Uint)
            return DOE_RESOURCE_LAYOUT;
        result.sample_type = input->sample_type;
        result.multisampled = input->multisampled;
        result.view_dimension = input->view_dimension;
        break;
    case DOE_BIND_STORAGE_TEXTURE: {
        if (input->storage_access < WGPUStorageTextureAccess_WriteOnly ||
            input->storage_access > WGPUStorageTextureAccess_ReadWrite) return DOE_RESOURCE_LAYOUT;
        uint32_t bytes, width, height;
        doe_resource_status status = resource_format_block(input->storage_format, &bytes, &width, &height);
        if (status != DOE_RESOURCE_OK) return status;
        if (resource_format_depth(input->storage_format) || width != 1 || height != 1)
            return DOE_RESOURCE_LAYOUT;
        result.storage_access = input->storage_access;
        result.storage_format = input->storage_format;
        result.view_dimension = input->view_dimension;
        break;
    }
    default: return DOE_RESOURCE_UNSUPPORTED;
    }
    if (result.kind == DOE_BIND_TEXTURE || result.kind == DOE_BIND_STORAGE_TEXTURE) {
        if (!result.view_dimension) result.view_dimension = WGPUTextureViewDimension_2D;
        if (result.view_dimension < WGPUTextureViewDimension_1D ||
            result.view_dimension > WGPUTextureViewDimension_3D) return DOE_RESOURCE_LAYOUT;
        if (result.kind == DOE_BIND_STORAGE_TEXTURE &&
            (result.view_dimension == WGPUTextureViewDimension_Cube ||
            result.view_dimension == WGPUTextureViewDimension_CubeArray)) return DOE_RESOURCE_LAYOUT;
        if (result.multisampled && result.view_dimension != WGPUTextureViewDimension_2D)
            return DOE_RESOURCE_LAYOUT;
    }
    if ((result.visibility & WGPUShaderStage_Vertex) &&
        ((result.kind == DOE_BIND_BUFFER && result.buffer_type == WGPUBufferBindingType_Storage) ||
         (result.kind == DOE_BIND_STORAGE_TEXTURE && result.storage_access != WGPUStorageTextureAccess_ReadOnly)))
        return DOE_RESOURCE_LAYOUT;
    if (result.kind == DOE_BIND_TEXTURE && result.multisampled && result.sample_type == WGPUTextureSampleType_Float)
        return DOE_RESOURCE_LAYOUT;
    *out = result;
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_layout_create(const doe_resource_config *config,
    const doe_resource_layout_entry *entries, size_t count, bool automatic,
    doe_resource_layout **out)
{
    if (!out || !resource_config_valid(config) || (count && !entries)) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (!config->device.alive(config->device.identity)) return DOE_RESOURCE_UNAVAILABLE;
    if (count > DOE_RESOURCE_MAX_BINDINGS) return DOE_RESOURCE_UNSUPPORTED;
    doe_resource_layout_entry sorted[DOE_RESOURCE_MAX_BINDINGS];
    for (size_t i = 0; i < count; i++) {
        doe_resource_layout_entry entry;
        doe_resource_status status = normalize_entry(&entries[i], &entry);
        if (status != DOE_RESOURCE_OK) return status;
        size_t position = i;
        for (size_t j = 0; j < i; j++)
            if (sorted[j].binding == entry.binding) return DOE_RESOURCE_LAYOUT;
        while (position && sorted[position - 1].binding > entry.binding) {
            sorted[position] = sorted[position - 1];
            position--;
        }
        sorted[position] = entry;
    }
    doe_resource_layout *layout = resource_allocate(config, sizeof(*layout), _Alignof(doe_resource_layout));
    if (!layout) return DOE_RESOURCE_OUT_OF_MEMORY;
    layout->count = count;
    layout->automatic = automatic;
    layout->automatic_claimed = false;
    if (count) memcpy(layout->entries, sorted, count * sizeof(*sorted));
    resource_init(&layout->base, config, (doe_resource_native){0});
    *out = layout;
    return DOE_RESOURCE_OK;
}

void doe_resource_layout_retain(doe_resource_layout *layout)
{
    if (layout) resource_retain(&layout->base);
}

void doe_resource_layout_release(doe_resource_layout *layout)
{
    if (layout && resource_last_reference(&layout->base))
        resource_finalize(&layout->base, sizeof(*layout), _Alignof(doe_resource_layout));
}

bool doe_resource_layout_compatible(const doe_resource_layout *a, const doe_resource_layout *b)
{
    if (!a || !b) return false;
    if (a == b) return true;
    if (a->automatic || b->automatic || a->count != b->count ||
        a->base.config.device.identity != b->base.config.device.identity) return false;
    for (size_t i = 0; i < a->count; i++) {
        const doe_resource_layout_entry *x = &a->entries[i], *y = &b->entries[i];
        if (x->binding != y->binding || x->visibility != y->visibility || x->kind != y->kind ||
            x->buffer_type != y->buffer_type || x->dynamic_offset != y->dynamic_offset ||
            x->minimum_size != y->minimum_size || x->sampler_type != y->sampler_type ||
            x->sample_type != y->sample_type || x->view_dimension != y->view_dimension ||
            x->multisampled != y->multisampled || x->storage_access != y->storage_access ||
            x->storage_format != y->storage_format) return false;
    }
    return true;
}

const doe_resource_layout_entry *doe_resource_layout_entries(const doe_resource_layout *layout,
    size_t *count)
{
    if (!layout || !count) return NULL;
    *count = layout->count;
    return layout->entries;
}

doe_resource_status doe_resource_pipeline_layout_create(const doe_resource_config *config,
    doe_resource_layout *const *groups, size_t count, uint32_t immediate_size,
    doe_resource_pipeline_layout **out)
{
    if (!out || !resource_config_valid(config) || (count && !groups)) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (!config->device.alive(config->device.identity)) return DOE_RESOURCE_UNAVAILABLE;
    if (immediate_size || count > DOE_RESOURCE_MAX_GROUPS) return DOE_RESOURCE_UNSUPPORTED;
    for (size_t i = 0; i < count; i++) {
        if (!groups[i]) continue;
        if (groups[i]->base.config.device.identity != config->device.identity) return DOE_RESOURCE_DEVICE_MISMATCH;
    }
    doe_resource_pipeline_layout *layout = resource_allocate(config, sizeof(*layout), _Alignof(doe_resource_pipeline_layout));
    if (!layout) return DOE_RESOURCE_OUT_OF_MEMORY;
    layout->count = count;
    layout->empty = NULL;
    for (size_t i = 0; i < count; i++) {
        layout->groups[i] = groups[i];
        if (!doe_resource_pipeline_layout_group_required(layout, i)) layout->groups[i] = NULL;
        doe_resource_layout_retain(layout->groups[i]);
    }
    resource_init(&layout->base, config, (doe_resource_native){0});
    *out = layout;
    return DOE_RESOURCE_OK;
}

void doe_resource_pipeline_layout_retain(doe_resource_pipeline_layout *layout)
{
    if (layout) resource_retain(&layout->base);
}

void doe_resource_pipeline_layout_release(doe_resource_pipeline_layout *layout)
{
    if (!layout || !resource_last_reference(&layout->base)) return;
    for (size_t i = 0; i < layout->count; i++) doe_resource_layout_release(layout->groups[i]);
    doe_resource_layout_release(layout->empty);
    resource_finalize(&layout->base, sizeof(*layout), _Alignof(doe_resource_pipeline_layout));
}

static doe_resource_status validate_binding(const doe_resource_layout *layout,
    const doe_resource_layout_entry *entry, const doe_resource_binding *binding,
    const doe_resource_binding_limits *limits, const doe_resource_binding_features *features,
    doe_resource_binding *out)
{
    if (binding->binding != entry->binding || binding->kind != entry->kind) return DOE_RESOURCE_LAYOUT;
    doe_resource_binding resolved = *binding;
    void *device = layout->base.config.device.identity;
    switch (entry->kind) {
    case DOE_BIND_BUFFER: {
        doe_resource_buffer *buffer = binding->resource.buffer.buffer;
        if (!resource_buffer_available(buffer)) return DOE_RESOURCE_UNAVAILABLE;
        if (buffer->base.config.device.identity != device) return DOE_RESOURCE_DEVICE_MISMATCH;
        bool uniform = entry->buffer_type == WGPUBufferBindingType_Uniform;
        uint64_t usage = uniform ? WGPUBufferUsage_Uniform : WGPUBufferUsage_Storage;
        uint64_t alignment = uniform ? limits->uniform_alignment : limits->storage_alignment;
        uint64_t maximum_size = uniform ? limits->max_uniform_size : limits->max_storage_size;
        if (!(buffer->desc.usage & usage)) return DOE_RESOURCE_USAGE;
        if (!alignment || binding->resource.buffer.offset % alignment) return DOE_RESOURCE_ALIGNMENT;
        uint64_t size;
        doe_resource_status status = resource_range(buffer->desc.size, binding->resource.buffer.offset,
            binding->resource.buffer.size, &size);
        if (status != DOE_RESOURCE_OK) return status;
        if (!size || size < entry->minimum_size || size > maximum_size || (!uniform && size % 4))
            return DOE_RESOURCE_RANGE;
        resolved.resource.buffer.size = size;
        break;
    }
    case DOE_BIND_TEXTURE:
    case DOE_BIND_STORAGE_TEXTURE: {
        doe_resource_view *view = binding->resource.view;
        if (!view || !resource_texture_available(view->texture)) return DOE_RESOURCE_UNAVAILABLE;
        if (view->base.config.device.identity != device) return DOE_RESOURCE_DEVICE_MISMATCH;
        if (entry->view_dimension != view->desc.dimension) return DOE_RESOURCE_LAYOUT;
        if (entry->kind == DOE_BIND_TEXTURE) {
            if (!(view->desc.usage & WGPUTextureUsage_TextureBinding)) return DOE_RESOURCE_USAGE;
            if (entry->multisampled != (view->texture->desc.samples > 1)) return DOE_RESOURCE_LAYOUT;
            /* Sample types are defined per depth/stencil aspect; a combined
             * view must select DepthOnly or StencilOnly to be sampled. */
            if (resource_format_aspects(view->desc.format) == 6) return DOE_RESOURCE_ASPECT;
            WGPUTextureSampleType sample;
            doe_resource_status status = doe_resource_format_sample_type(view->desc.format, view->desc.aspect, &sample);
            if (status != DOE_RESOURCE_OK) return status;
            if (sample == WGPUTextureSampleType_UnfilterableFloat && features->float32_filterable)
                sample = WGPUTextureSampleType_Float;
            /* Float views also bind as unfilterable-float; depth aspects bind as
             * 'depth' or 'unfilterable-float' (WebGPU depth-stencil format table). */
            if (entry->sample_type != sample &&
                !((sample == WGPUTextureSampleType_Float || sample == WGPUTextureSampleType_Depth) &&
                entry->sample_type == WGPUTextureSampleType_UnfilterableFloat))
                return DOE_RESOURCE_LAYOUT;
        } else {
            if (!(view->desc.usage & WGPUTextureUsage_StorageBinding)) return DOE_RESOURCE_USAGE;
            if (resource_format_depth(view->desc.format) || entry->storage_format != view->desc.format ||
                view->desc.mip_count != 1 || view->texture->desc.samples != 1) return DOE_RESOURCE_LAYOUT;
        }
        break;
    }
    case DOE_BIND_EXTERNAL_TEXTURE:
        if (!resource_external_available(binding->resource.external_texture)) return DOE_RESOURCE_UNAVAILABLE;
        if (binding->resource.external_texture->base.config.device.identity != device) return DOE_RESOURCE_DEVICE_MISMATCH;
        break;
    case DOE_BIND_SAMPLER: {
        doe_resource_sampler *sampler = binding->resource.sampler;
        if (!sampler || !resource_alive(&sampler->base)) return DOE_RESOURCE_UNAVAILABLE;
        if (sampler->base.config.device.identity != device) return DOE_RESOURCE_DEVICE_MISMATCH;
        bool comparison = sampler->desc.compare != WGPUCompareFunction_Undefined;
        if ((entry->sampler_type == WGPUSamplerBindingType_Comparison) != comparison) return DOE_RESOURCE_LAYOUT;
        if (entry->sampler_type == WGPUSamplerBindingType_NonFiltering &&
            (sampler->desc.minFilter == WGPUFilterMode_Linear || sampler->desc.magFilter == WGPUFilterMode_Linear ||
            sampler->desc.mipmapFilter == WGPUMipmapFilterMode_Linear)) return DOE_RESOURCE_LAYOUT;
        break;
    }
    default: return DOE_RESOURCE_UNSUPPORTED;
    }
    *out = resolved;
    return DOE_RESOURCE_OK;
}

static void retain_binding(const doe_resource_binding *binding)
{
    if (binding->kind == DOE_BIND_BUFFER) doe_resource_buffer_retain(binding->resource.buffer.buffer);
    else if (binding->kind == DOE_BIND_SAMPLER) doe_resource_sampler_retain(binding->resource.sampler);
    else if (binding->kind == DOE_BIND_EXTERNAL_TEXTURE) doe_resource_external_texture_retain(binding->resource.external_texture);
    else doe_resource_view_retain(binding->resource.view);
}

static void release_binding(const doe_resource_binding *binding)
{
    if (binding->kind == DOE_BIND_BUFFER) doe_resource_buffer_release(binding->resource.buffer.buffer);
    else if (binding->kind == DOE_BIND_SAMPLER) doe_resource_sampler_release(binding->resource.sampler);
    else if (binding->kind == DOE_BIND_EXTERNAL_TEXTURE) doe_resource_external_texture_release(binding->resource.external_texture);
    else doe_resource_view_release(binding->resource.view);
}

doe_resource_status doe_resource_bind_group_create(doe_resource_layout *layout,
    const doe_resource_binding *bindings, size_t count,
    const doe_resource_binding_limits *limits, doe_resource_bind_group **out)
{
    return doe_resource_bind_group_create_with_features(layout, bindings, count, limits, NULL, out);
}

doe_resource_status doe_resource_bind_group_create_with_features(doe_resource_layout *layout,
    const doe_resource_binding *bindings, size_t count,
    const doe_resource_binding_limits *limits, const doe_resource_binding_features *features,
    doe_resource_bind_group **out)
{
    if (!layout || !out || !limits || (count && !bindings)) return DOE_RESOURCE_INVALID_ARGUMENT;
    doe_resource_binding_features enabled = features ? *features : (doe_resource_binding_features){0};
    if (!resource_alive(&layout->base)) return DOE_RESOURCE_UNAVAILABLE;
    if (count != layout->count) return DOE_RESOURCE_LAYOUT;
    doe_resource_binding ordered[DOE_RESOURCE_MAX_BINDINGS];
    for (size_t i = 0; i < count; i++) {
        const doe_resource_binding *found = NULL;
        for (size_t j = 0; j < count; j++) {
            if (bindings[j].binding != layout->entries[i].binding) continue;
            if (found) return DOE_RESOURCE_LAYOUT;
            found = &bindings[j];
        }
        if (!found) return DOE_RESOURCE_LAYOUT;
        doe_resource_status status = validate_binding(layout, &layout->entries[i], found, limits, &enabled, &ordered[i]);
        if (status != DOE_RESOURCE_OK) return status;
    }
    doe_resource_bind_group *group = resource_allocate(&layout->base.config, sizeof(*group), _Alignof(doe_resource_bind_group));
    if (!group) return DOE_RESOURCE_OUT_OF_MEMORY;
    group->count = count;
    group->layout = layout;
    group->limits = *limits;
    group->features = enabled;
    for (size_t i = 0; i < count; i++) {
        group->bindings[i] = ordered[i];
        retain_binding(&ordered[i]);
    }
    doe_resource_layout_retain(layout);
    resource_init(&group->base, &layout->base.config, (doe_resource_native){0});
    *out = group;
    return DOE_RESOURCE_OK;
}

void doe_resource_bind_group_retain(doe_resource_bind_group *group)
{
    if (group) resource_retain(&group->base);
}

void doe_resource_bind_group_release(doe_resource_bind_group *group)
{
    if (!group || !resource_last_reference(&group->base)) return;
    for (size_t i = 0; i < group->count; i++) release_binding(&group->bindings[i]);
    doe_resource_layout_release(group->layout);
    resource_finalize(&group->base, sizeof(*group), _Alignof(doe_resource_bind_group));
}

const doe_resource_binding *doe_resource_bind_group_bindings(const doe_resource_bind_group *group,
    size_t *count)
{
    if (!group || !count) return NULL;
    *count = group->count;
    return group->bindings;
}

doe_resource_status resource_group_validate(const doe_resource_bind_group *group)
{
    if (!group || !resource_alive(&group->base)) return DOE_RESOURCE_UNAVAILABLE;
    for (size_t i = 0; i < group->count; i++) {
        doe_resource_binding resolved;
        doe_resource_status status = validate_binding(group->layout, &group->layout->entries[i],
            &group->bindings[i], &group->limits, &group->features, &resolved);
        if (status != DOE_RESOURCE_OK) return status;
        if (resolved.kind == DOE_BIND_BUFFER &&
            (resolved.resource.buffer.buffer->mapped || resolved.resource.buffer.buffer->map_pending))
            return DOE_RESOURCE_STATE;
    }
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_bind_group_resolve(const doe_resource_bind_group *group,
    const uint32_t *offsets, size_t offset_count, doe_resource_binding *out, size_t capacity)
{
    if (!group || (group->count && !out) || (offset_count && !offsets)) return DOE_RESOURCE_INVALID_ARGUMENT;
    if (capacity < group->count) return DOE_RESOURCE_RANGE;
    doe_resource_status status = resource_group_validate(group);
    if (status != DOE_RESOURCE_OK) return status;
    doe_resource_binding resolved[DOE_RESOURCE_MAX_BINDINGS];
    size_t next = 0;
    for (size_t i = 0; i < group->count; i++) {
        resolved[i] = group->bindings[i];
        const doe_resource_layout_entry *entry = &group->layout->entries[i];
        if (entry->kind != DOE_BIND_BUFFER || !entry->dynamic_offset) continue;
        if (next == offset_count) return DOE_RESOURCE_LAYOUT;
        uint64_t dynamic = offsets[next++];
        uint64_t alignment = entry->buffer_type == WGPUBufferBindingType_Uniform ?
            group->limits.uniform_alignment : group->limits.storage_alignment;
        if (!alignment || dynamic % alignment) return DOE_RESOURCE_ALIGNMENT;
        if (resolved[i].resource.buffer.offset > UINT64_MAX - dynamic) return DOE_RESOURCE_OVERFLOW;
        resolved[i].resource.buffer.offset += dynamic;
        doe_resource_binding checked;
        status = validate_binding(group->layout, entry, &resolved[i], &group->limits, &group->features, &checked);
        if (status != DOE_RESOURCE_OK) return status;
    }
    if (next != offset_count) return DOE_RESOURCE_LAYOUT;
    if (group->count) memcpy(out, resolved, group->count * sizeof(*out));
    return DOE_RESOURCE_OK;
}

doe_resource_status doe_resource_pipeline_create(const doe_resource_config *config,
    doe_resource_pipeline_layout *layout, doe_resource_native native,
    const uint32_t workgroup[3], doe_resource_pipeline **out)
{
    if (!resource_config_valid(config) || !layout || !workgroup || !out || !native.handle || !native.release)
        return DOE_RESOURCE_INVALID_ARGUMENT;
    if (!config->device.alive(config->device.identity)) return DOE_RESOURCE_UNAVAILABLE;
    if (layout->base.config.device.identity != config->device.identity) return DOE_RESOURCE_DEVICE_MISMATCH;
    if (!workgroup[0] || !workgroup[1] || !workgroup[2]) return DOE_RESOURCE_RANGE;
    for (size_t i = 0; i < layout->count; i++)
        if (doe_resource_pipeline_layout_group_required(layout, i) && layout->groups[i]->automatic && layout->groups[i]->automatic_claimed) return DOE_RESOURCE_LAYOUT;
    doe_resource_pipeline *pipeline = resource_allocate(config, sizeof(*pipeline), _Alignof(doe_resource_pipeline));
    if (!pipeline) return DOE_RESOURCE_OUT_OF_MEMORY;
    pipeline->layout = layout;
    memcpy(pipeline->workgroup, workgroup, sizeof(pipeline->workgroup));
    for (size_t i = 0; i < layout->count; i++)
        if (doe_resource_pipeline_layout_group_required(layout, i) && layout->groups[i]->automatic) layout->groups[i]->automatic_claimed = true;
    doe_resource_pipeline_layout_retain(layout);
    resource_init(&pipeline->base, config, native);
    *out = pipeline;
    return DOE_RESOURCE_OK;
}

void doe_resource_pipeline_retain(doe_resource_pipeline *pipeline)
{
    if (pipeline) resource_retain(&pipeline->base);
}

void doe_resource_pipeline_release(doe_resource_pipeline *pipeline)
{
    if (!pipeline || !resource_last_reference(&pipeline->base)) return;
    doe_resource_pipeline_layout *layout = pipeline->layout;
    resource_finalize(&pipeline->base, sizeof(*pipeline), _Alignof(doe_resource_pipeline));
    doe_resource_pipeline_layout_release(layout);
}

void *doe_resource_pipeline_native(const doe_resource_pipeline *pipeline)
{
    return pipeline ? pipeline->base.native.handle : NULL;
}
