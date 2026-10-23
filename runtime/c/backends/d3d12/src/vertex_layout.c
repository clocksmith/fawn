/* SPDX-License-Identifier: MIT */
#include "internal.h"

uint32_t doe_dx_vertex_format_size(doe_dx_vertex_format format) {
    if (format < DOE_DX_FLOAT32 || format > DOE_DX_SINT32X4) return 0;
    return ((uint32_t)format % 4 + 1) * 4;
}

doe_dx_status doe_dx_validate_mesh(const doe_dx_mesh_pipeline_desc *desc, uint32_t *ends) {
    const doe_dx_graphics_pipeline_desc *graphics = &desc->graphics;
    if (!graphics->vertex_bytecode || !graphics->vertex_bytecode_size ||
        (desc->vertex_buffer_count && !desc->vertex_buffers) || (desc->attribute_count && !desc->attributes))
        return DOE_DX_INVALID_ARGUMENT;
    if (graphics->color_format != DOE_DX_RGBA8_UNORM && graphics->color_format != DOE_DX_BGRA8_UNORM)
        return DOE_DX_UNSUPPORTED;
    if (desc->vertex_buffer_count > DOE_DX_MAX_VERTEX_BUFFERS || desc->attribute_count > DOE_DX_MAX_VERTEX_ATTRIBUTES)
        return DOE_DX_RANGE;
    if ((desc->depth_enabled && (desc->depth_compare < DOE_DX_NEVER || desc->depth_compare > DOE_DX_ALWAYS)) ||
        (!desc->depth_enabled && desc->depth_write)) return DOE_DX_INVALID_ARGUMENT;
    for (size_t index = 0; index < desc->vertex_buffer_count; index++) {
        const doe_dx_vertex_layout *layout = &desc->vertex_buffers[index];
        if (layout->stride < 4 || layout->stride > 2048 || layout->stride % 4 ||
            (layout->step != DOE_DX_PER_VERTEX && layout->step != DOE_DX_PER_INSTANCE))
            return DOE_DX_INVALID_ARGUMENT;
    }
    for (size_t index = 0; index < desc->attribute_count; index++) {
        const doe_dx_vertex_attribute *attribute = &desc->attributes[index];
        uint32_t size = doe_dx_vertex_format_size(attribute->format);
        if (!size || attribute->buffer_slot >= desc->vertex_buffer_count || attribute->shader_location >= 32)
            return DOE_DX_INVALID_ARGUMENT;
        uint32_t stride = desc->vertex_buffers[attribute->buffer_slot].stride;
        if (attribute->offset % 4 || attribute->offset > stride || size > stride - attribute->offset)
            return DOE_DX_RANGE;
        for (size_t prior = 0; prior < index; prior++)
            if (desc->attributes[prior].shader_location == attribute->shader_location) return DOE_DX_INVALID_ARGUMENT;
        uint32_t end = attribute->offset + size;
        if (end > ends[attribute->buffer_slot]) ends[attribute->buffer_slot] = end;
    }
    for (size_t index = 0; index < desc->vertex_buffer_count; index++)
        if (!ends[index]) return DOE_DX_INVALID_ARGUMENT; /* Every declared slot is consumed. */
    return DOE_DX_OK;
}
