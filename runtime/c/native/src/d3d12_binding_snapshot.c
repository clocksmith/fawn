/* SPDX-License-Identifier: MIT */
#include "d3d12_binding_internal.h"
#include "d3d12_texture_desc.h"

native_failure native_d3d12_binding_snapshot(const doe_resource_command *record,native_d3d12_snapshot *out)
{
#if defined(_WIN32)
    native_pipeline *pipeline = doe_resource_pipeline_native(record->value.dispatch.pipeline);
    const native_d3d12_compute *map = pipeline->d3d12_compute;
    if (!map) return native_failure_status(NATIVE_INVALID_STATE);
    doe_resource_binding resolved[NATIVE_MAX_PIPELINE_BINDINGS];
    doe_resource_status resource = native_resolve_resources(pipeline,
        doe_resource_pipeline_get_layout(record->value.dispatch.pipeline),record->value.dispatch.groups,
        record->value.dispatch.dynamic_offsets,resolved);
    if (resource != DOE_RESOURCE_OK) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    out->values = (doe_dx_dynamic_resources){.resources={.buffers=out->buffers,.buffer_count=map->buffer_count,
        .textures=out->textures,.texture_count=map->texture_count},.samplers=out->samplers,.sampler_count=map->sampler_count};
    out->metadata_index = map->dispatch_index;
    out->guard = map->guard;
    for (size_t i = 0; i < map->buffer_count; ++i) {
        if (i == map->dispatch_index) continue;
        size_t index = map->buffer_indices[i];
        if (index >= pipeline->binding_count || resolved[index].kind != DOE_BIND_BUFFER)
            return native_failure_status(NATIVE_INVALID_STATE);
        native_buffer *buffer = doe_resource_buffer_native(resolved[index].resource.buffer.buffer);
        uint64_t offset = resolved[index].resource.buffer.offset, size = resolved[index].resource.buffer.size;
        if (map->uniform[i]) {
            if (size > UINT64_MAX - 255) return native_failure_status(NATIVE_INVALID_ARGUMENT);
            size = (size + 255) & ~UINT64_C(255);
        }
        native_d3d12_buffer *native = buffer->handle.d3d12;
        if (!native || offset > native->allocation_size || size > native->allocation_size - offset)
            return native_failure_status(NATIVE_INVALID_ARGUMENT);
        out->buffers[i] = (doe_dx_buffer_binding){native->allocation,offset,size};
    }
    for (size_t i = 0; i < map->texture_count; ++i) {
        size_t index = map->texture_indices[i];
        if (index >= pipeline->binding_count ||
            (resolved[index].kind != DOE_BIND_TEXTURE && resolved[index].kind != DOE_BIND_STORAGE_TEXTURE))
            return native_failure_status(NATIVE_INVALID_STATE);
        doe_resource_view *view = resolved[index].resource.view;
        const doe_resource_view_desc *span = doe_resource_view_description(view);
        doe_resource_texture *texture = doe_resource_view_texture(view);
        const doe_resource_texture_desc *desc = doe_resource_texture_description(texture);
        if (!span || !desc || span->dimension != WGPUTextureViewDimension_2D || span->format != desc->format ||
            span->aspect != WGPUTextureAspect_All || span->base_mip || span->mip_count != 1 ||
            span->base_layer || span->layer_count != 1) return native_failure_status(NATIVE_UNSUPPORTED);
        native_texture *native = doe_resource_texture_native(texture);
        if (!native || !native->d3d12) return native_failure_status(NATIVE_INVALID_STATE);
        out->textures[i] = native->d3d12;
    }
    for (size_t i = 0; i < map->sampler_count; ++i) {
        size_t index = map->sampler_indices[i];
        if (index >= pipeline->binding_count || resolved[index].kind != DOE_BIND_SAMPLER ||
            !native_d3d12_sampler_descriptor(doe_resource_sampler_description(resolved[index].resource.sampler),&out->samplers[i]))
            return native_failure_status(NATIVE_INVALID_ARGUMENT);
    }
    return native_failure_status(NATIVE_OK);
#else
    (void)record;
    (void)out;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}
