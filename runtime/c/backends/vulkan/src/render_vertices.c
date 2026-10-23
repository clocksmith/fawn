/* SPDX-License-Identifier: MIT */
#include "render_internal.h"

/* Native fetch bounds shared by direct admission and GPU indirect preparation.
 * Public required-slot and usage-scope policy stays with the resource owner. */
doe_vk_status doe_vk_render_vertex_limits(doe_vk_commands *commands, doe_vk_pipeline *pipeline,
    const doe_vk_buffer_binding *vertices, size_t count, uint32_t out_limits[2]) {
    if (count != pipeline->vertex_count || (count != 0 && vertices == NULL))
        return DOE_VK_INVALID_ARGUMENT;
    uint32_t limits[2] = {UINT32_MAX, UINT32_MAX};
    for (size_t i = 0; i < count; ++i) {
        const doe_vk_buffer_binding *binding = &vertices[i];
        uint32_t attribute_end = pipeline->vertex_attribute_end[i];
        if (binding->buffer == NULL && attribute_end == 0 && binding->offset == 0 && binding->size == 0)
            continue;
        if (binding->buffer == NULL || binding->buffer->context != commands->context ||
            !(binding->buffer->usage & DOE_VK_BUFFER_VERTEX) || binding->offset % 4 != 0 ||
            !doe_vk_range(binding->buffer->size, binding->offset, binding->size)) return DOE_VK_INVALID_ARGUMENT;
        if (binding->buffer->content_status != DOE_VK_OK) return binding->buffer->content_status;
        if (attribute_end == 0) continue;
        uint64_t capacity = 0;
        if (attribute_end <= binding->size) {
            uint32_t stride = pipeline->vertices[i].stride;
            capacity = stride == 0 ? UINT32_MAX : (binding->size - attribute_end) / stride + 1;
        }
        unsigned axis = pipeline->vertices[i].per_instance ? 1 : 0;
        if (capacity < limits[axis]) limits[axis] = (uint32_t)capacity;
    }
    memcpy(out_limits, limits, sizeof(limits));
    return DOE_VK_OK;
}

_Static_assert((int)DOE_VK_INDEX_UINT16 == (int)VK_INDEX_TYPE_UINT16 &&
    (int)DOE_VK_INDEX_UINT32 == (int)VK_INDEX_TYPE_UINT32,
    "named index formats are native VkIndexType values");

doe_vk_status doe_vk_render_index_limit(doe_vk_commands *commands,
    const doe_vk_render_indices *indices, uint32_t *out_limit) {
    if (indices->format != DOE_VK_INDEX_UINT16 && indices->format != DOE_VK_INDEX_UINT32)
        return DOE_VK_INVALID_ARGUMENT;
    if (indices->format == DOE_VK_INDEX_UINT32 && !commands->context->caps.full_draw_index_uint32)
        return DOE_VK_UNSUPPORTED;
    const uint64_t bytes = indices->format == DOE_VK_INDEX_UINT16 ? 2 : 4;
    const doe_vk_buffer *buffer = indices->buffer;
    if (buffer == NULL || buffer->context != commands->context ||
        !(buffer->usage & DOE_VK_BUFFER_INDEX) || indices->offset % bytes != 0 ||
        !doe_vk_range(buffer->size, indices->offset, indices->size)) return DOE_VK_INVALID_ARGUMENT;
    if (buffer->content_status != DOE_VK_OK) return buffer->content_status;
    const uint64_t capacity = indices->size / bytes;
    *out_limit = capacity > UINT32_MAX ? UINT32_MAX : (uint32_t)capacity;
    return DOE_VK_OK;
}
