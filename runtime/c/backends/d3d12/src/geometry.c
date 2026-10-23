/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <math.h>

static doe_dx_status view_preflight(doe_dx_commands *commands, const doe_dx_buffer_binding *binding) {
    doe_dx_status status = doe_dx_buffer_preflight(commands, binding->buffer, true);
    if (status != DOE_DX_OK) return status;
    if (binding->buffer->memory != DOE_DX_DEFAULT) return DOE_DX_UNSUPPORTED;
    if (!binding->size || binding->size > UINT32_MAX ||
        !doe_dx_range(binding->buffer->size, binding->offset, binding->size)) return DOE_DX_RANGE;
    return DOE_DX_OK;
}

static bool shader_alias(const doe_dx_compute_resources *resources, doe_dx_buffer *buffer) {
    for (size_t index = 0; index < resources->buffer_count; index++)
        if (resources->buffers[index].buffer == buffer) return true;
    return false;
}

doe_dx_status doe_dx_prepare_geometry(doe_dx_commands *commands, doe_dx_pipeline *pipeline,
    const doe_dx_compute_resources *resources, const doe_dx_mesh_draw_desc *mesh,
    doe_dx_native_geometry *native, size_t *added, doe_dx_error *error) {
    const doe_dx_draw_desc *draw = &mesh->draw;
    if (mesh->vertex_count != pipeline->vertex_buffer_count || (mesh->vertex_count && !mesh->vertices) ||
        pipeline->depth_enabled != (mesh->depth != NULL))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "draw.geometry_layout", error);
    if (mesh->indices) {
        if (draw->vertex_count || draw->first_vertex || !mesh->indices->count)
            return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "draw.index_counts", error);
        const doe_dx_index_binding *indices = mesh->indices;
        doe_dx_status status = view_preflight(commands, &indices->binding);
        if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "draw.index_buffer", error);
        uint32_t size = indices->format == DOE_DX_INDEX_UINT16 ? 2 : 4;
        if (indices->format != DOE_DX_INDEX_UINT16 && indices->format != DOE_DX_INDEX_UINT32)
            return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "draw.index_format", error);
        if (indices->binding.offset % size || indices->binding.size % size)
            return doe_dx_fail(DOE_DX_ALIGNMENT, 0, "draw.index_alignment", error);
        if ((uint64_t)indices->first + indices->count > indices->binding.size / size)
            return doe_dx_fail(DOE_DX_RANGE, 0, "draw.index_range", error);
        if (shader_alias(resources, indices->binding.buffer))
            return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "draw.index_alias", error);
        native->indices = (doe_dx_native_binding){indices->binding.buffer->native, indices->binding.offset, indices->binding.size};
        if (!doe_dx_find_buffer(commands, indices->binding.buffer)) (*added)++;
    }
    for (size_t index = 0; index < mesh->vertex_count; index++) {
        const doe_dx_buffer_binding *binding = &mesh->vertices[index];
        doe_dx_status status = view_preflight(commands, binding);
        if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "draw.vertex_buffer", error);
        if (binding->offset % 4) return doe_dx_fail(DOE_DX_ALIGNMENT, 0, "draw.vertex_alignment", error);
        if (shader_alias(resources, binding->buffer) ||
            (mesh->indices && mesh->indices->binding.buffer == binding->buffer))
            return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "draw.vertex_alias", error);
        for (size_t prior = 0; prior < index; prior++)
            if (mesh->vertices[prior].buffer == binding->buffer)
                return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "draw.vertex_alias", error);
        const doe_dx_vertex_layout *layout = &pipeline->vertex_buffers[index];
        uint64_t last = 0;
        if (layout->step == DOE_DX_PER_INSTANCE)
            last = (uint64_t)draw->first_instance + draw->instance_count - 1;
        else if (!mesh->indices)
            last = (uint64_t)draw->first_vertex + draw->vertex_count - 1;
        uint64_t required = last * layout->stride + pipeline->vertex_ends[index];
        if (required > binding->size) return doe_dx_fail(DOE_DX_RANGE, 0, "draw.vertex_span", error);
        native->vertices[index] = (doe_dx_native_binding){binding->buffer->native, binding->offset, binding->size};
        if (!doe_dx_find_buffer(commands, binding->buffer)) (*added)++;
    }
    if (mesh->depth) {
        if (mesh->depth == draw->color || mesh->depth->desc.format != DOE_DX_D32_FLOAT ||
            mesh->depth->desc.width != draw->color->desc.width || mesh->depth->desc.height != draw->color->desc.height)
            return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "draw.depth_shape", error);
        if (mesh->clear_depth && (!isfinite(mesh->depth_clear) || mesh->depth_clear < 0 || mesh->depth_clear > 1))
            return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "draw.depth_clear", error);
        doe_dx_status status = doe_dx_texture_preflight(commands, mesh->depth, DOE_DX_TEXTURE_DEPTH_ATTACHMENT, !mesh->clear_depth);
        if (status != DOE_DX_OK) return doe_dx_fail(status, 0, "draw.depth", error);
        for (size_t index = 0; index < resources->texture_count; index++)
            if (resources->textures[index] == mesh->depth)
                return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "draw.depth_alias", error);
        native->depth = mesh->depth->native;
        if (!doe_dx_find_texture(commands, mesh->depth)) (*added)++;
    } else if (mesh->clear_depth) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "draw.depth_missing", error);
    return DOE_DX_OK;
}

void doe_dx_use_geometry(doe_dx_commands *commands, const doe_dx_mesh_draw_desc *draw) {
    for (size_t index = 0; index < draw->vertex_count; index++) doe_dx_use_buffer(commands, draw->vertices[index].buffer);
    if (draw->indices) doe_dx_use_buffer(commands, draw->indices->binding.buffer);
    if (draw->depth) {
        doe_dx_use_texture(commands, draw->depth);
        if (draw->clear_depth) doe_dx_find_texture(commands, draw->depth)->initialized = true;
    }
}
