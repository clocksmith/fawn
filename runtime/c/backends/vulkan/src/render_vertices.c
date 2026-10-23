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
