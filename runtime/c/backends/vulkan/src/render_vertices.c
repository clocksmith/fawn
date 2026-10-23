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

/* One table: public vertex enum values are the retained native format numbers. */
_Static_assert((int)DOE_VK_VERTEX_UNORM8 == (int)VK_FORMAT_R8_UNORM, "vertex format UNORM8");
_Static_assert((int)DOE_VK_VERTEX_SNORM8 == (int)VK_FORMAT_R8_SNORM, "vertex format SNORM8");
_Static_assert((int)DOE_VK_VERTEX_UINT8 == (int)VK_FORMAT_R8_UINT, "vertex format UINT8");
_Static_assert((int)DOE_VK_VERTEX_SINT8 == (int)VK_FORMAT_R8_SINT, "vertex format SINT8");
_Static_assert((int)DOE_VK_VERTEX_UNORM8X2 == (int)VK_FORMAT_R8G8_UNORM, "vertex format UNORM8X2");
_Static_assert((int)DOE_VK_VERTEX_SNORM8X2 == (int)VK_FORMAT_R8G8_SNORM, "vertex format SNORM8X2");
_Static_assert((int)DOE_VK_VERTEX_UINT8X2 == (int)VK_FORMAT_R8G8_UINT, "vertex format UINT8X2");
_Static_assert((int)DOE_VK_VERTEX_SINT8X2 == (int)VK_FORMAT_R8G8_SINT, "vertex format SINT8X2");
_Static_assert((int)DOE_VK_VERTEX_UNORM8X4 == (int)VK_FORMAT_R8G8B8A8_UNORM, "vertex format UNORM8X4");
_Static_assert((int)DOE_VK_VERTEX_SNORM8X4 == (int)VK_FORMAT_R8G8B8A8_SNORM, "vertex format SNORM8X4");
_Static_assert((int)DOE_VK_VERTEX_UINT8X4 == (int)VK_FORMAT_R8G8B8A8_UINT, "vertex format UINT8X4");
_Static_assert((int)DOE_VK_VERTEX_SINT8X4 == (int)VK_FORMAT_R8G8B8A8_SINT, "vertex format SINT8X4");
_Static_assert((int)DOE_VK_VERTEX_UNORM8X4_BGRA == (int)VK_FORMAT_B8G8R8A8_UNORM, "vertex format UNORM8X4_BGRA");
_Static_assert((int)DOE_VK_VERTEX_UNORM10_10_10_2 == (int)VK_FORMAT_A2B10G10R10_UNORM_PACK32, "vertex format UNORM10_10_10_2");
_Static_assert((int)DOE_VK_VERTEX_UNORM16 == (int)VK_FORMAT_R16_UNORM, "vertex format UNORM16");
_Static_assert((int)DOE_VK_VERTEX_SNORM16 == (int)VK_FORMAT_R16_SNORM, "vertex format SNORM16");
_Static_assert((int)DOE_VK_VERTEX_UINT16 == (int)VK_FORMAT_R16_UINT, "vertex format UINT16");
_Static_assert((int)DOE_VK_VERTEX_SINT16 == (int)VK_FORMAT_R16_SINT, "vertex format SINT16");
_Static_assert((int)DOE_VK_VERTEX_FLOAT16 == (int)VK_FORMAT_R16_SFLOAT, "vertex format FLOAT16");
_Static_assert((int)DOE_VK_VERTEX_UNORM16X2 == (int)VK_FORMAT_R16G16_UNORM, "vertex format UNORM16X2");
_Static_assert((int)DOE_VK_VERTEX_SNORM16X2 == (int)VK_FORMAT_R16G16_SNORM, "vertex format SNORM16X2");
_Static_assert((int)DOE_VK_VERTEX_UINT16X2 == (int)VK_FORMAT_R16G16_UINT, "vertex format UINT16X2");
_Static_assert((int)DOE_VK_VERTEX_SINT16X2 == (int)VK_FORMAT_R16G16_SINT, "vertex format SINT16X2");
_Static_assert((int)DOE_VK_VERTEX_FLOAT16X2 == (int)VK_FORMAT_R16G16_SFLOAT, "vertex format FLOAT16X2");
_Static_assert((int)DOE_VK_VERTEX_UNORM16X4 == (int)VK_FORMAT_R16G16B16A16_UNORM, "vertex format UNORM16X4");
_Static_assert((int)DOE_VK_VERTEX_SNORM16X4 == (int)VK_FORMAT_R16G16B16A16_SNORM, "vertex format SNORM16X4");
_Static_assert((int)DOE_VK_VERTEX_UINT16X4 == (int)VK_FORMAT_R16G16B16A16_UINT, "vertex format UINT16X4");
_Static_assert((int)DOE_VK_VERTEX_SINT16X4 == (int)VK_FORMAT_R16G16B16A16_SINT, "vertex format SINT16X4");
_Static_assert((int)DOE_VK_VERTEX_FLOAT16X4 == (int)VK_FORMAT_R16G16B16A16_SFLOAT, "vertex format FLOAT16X4");
_Static_assert((int)DOE_VK_VERTEX_UINT32 == (int)VK_FORMAT_R32_UINT, "vertex format UINT32");
_Static_assert((int)DOE_VK_VERTEX_SINT32 == (int)VK_FORMAT_R32_SINT, "vertex format SINT32");
_Static_assert((int)DOE_VK_VERTEX_FLOAT32 == (int)VK_FORMAT_R32_SFLOAT, "vertex format FLOAT32");
_Static_assert((int)DOE_VK_VERTEX_UINT32X2 == (int)VK_FORMAT_R32G32_UINT, "vertex format UINT32X2");
_Static_assert((int)DOE_VK_VERTEX_SINT32X2 == (int)VK_FORMAT_R32G32_SINT, "vertex format SINT32X2");
_Static_assert((int)DOE_VK_VERTEX_FLOAT32X2 == (int)VK_FORMAT_R32G32_SFLOAT, "vertex format FLOAT32X2");
_Static_assert((int)DOE_VK_VERTEX_UINT32X3 == (int)VK_FORMAT_R32G32B32_UINT, "vertex format UINT32X3");
_Static_assert((int)DOE_VK_VERTEX_SINT32X3 == (int)VK_FORMAT_R32G32B32_SINT, "vertex format SINT32X3");
_Static_assert((int)DOE_VK_VERTEX_FLOAT32X3 == (int)VK_FORMAT_R32G32B32_SFLOAT, "vertex format FLOAT32X3");
_Static_assert((int)DOE_VK_VERTEX_UINT32X4 == (int)VK_FORMAT_R32G32B32A32_UINT, "vertex format UINT32X4");
_Static_assert((int)DOE_VK_VERTEX_SINT32X4 == (int)VK_FORMAT_R32G32B32A32_SINT, "vertex format SINT32X4");
_Static_assert((int)DOE_VK_VERTEX_FLOAT32X4 == (int)VK_FORMAT_R32G32B32A32_SFLOAT, "vertex format FLOAT32X4");

uint32_t doe_vk_vertex_format_bytes(doe_vk_vertex_format format) {
    switch (format) {
    case DOE_VK_VERTEX_UNORM8: return 1;
    case DOE_VK_VERTEX_SNORM8: return 1;
    case DOE_VK_VERTEX_UINT8: return 1;
    case DOE_VK_VERTEX_SINT8: return 1;
    case DOE_VK_VERTEX_UNORM8X2: return 2;
    case DOE_VK_VERTEX_SNORM8X2: return 2;
    case DOE_VK_VERTEX_UINT8X2: return 2;
    case DOE_VK_VERTEX_SINT8X2: return 2;
    case DOE_VK_VERTEX_UNORM8X4: return 4;
    case DOE_VK_VERTEX_SNORM8X4: return 4;
    case DOE_VK_VERTEX_UINT8X4: return 4;
    case DOE_VK_VERTEX_SINT8X4: return 4;
    case DOE_VK_VERTEX_UNORM8X4_BGRA: return 4;
    case DOE_VK_VERTEX_UNORM10_10_10_2: return 4;
    case DOE_VK_VERTEX_UNORM16: return 2;
    case DOE_VK_VERTEX_SNORM16: return 2;
    case DOE_VK_VERTEX_UINT16: return 2;
    case DOE_VK_VERTEX_SINT16: return 2;
    case DOE_VK_VERTEX_FLOAT16: return 2;
    case DOE_VK_VERTEX_UNORM16X2: return 4;
    case DOE_VK_VERTEX_SNORM16X2: return 4;
    case DOE_VK_VERTEX_UINT16X2: return 4;
    case DOE_VK_VERTEX_SINT16X2: return 4;
    case DOE_VK_VERTEX_FLOAT16X2: return 4;
    case DOE_VK_VERTEX_UNORM16X4: return 8;
    case DOE_VK_VERTEX_SNORM16X4: return 8;
    case DOE_VK_VERTEX_UINT16X4: return 8;
    case DOE_VK_VERTEX_SINT16X4: return 8;
    case DOE_VK_VERTEX_FLOAT16X4: return 8;
    case DOE_VK_VERTEX_UINT32: return 4;
    case DOE_VK_VERTEX_SINT32: return 4;
    case DOE_VK_VERTEX_FLOAT32: return 4;
    case DOE_VK_VERTEX_UINT32X2: return 8;
    case DOE_VK_VERTEX_SINT32X2: return 8;
    case DOE_VK_VERTEX_FLOAT32X2: return 8;
    case DOE_VK_VERTEX_UINT32X3: return 12;
    case DOE_VK_VERTEX_SINT32X3: return 12;
    case DOE_VK_VERTEX_FLOAT32X3: return 12;
    case DOE_VK_VERTEX_UINT32X4: return 16;
    case DOE_VK_VERTEX_SINT32X4: return 16;
    case DOE_VK_VERTEX_FLOAT32X4: return 16;
    }
    return 0;
}
