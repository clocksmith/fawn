/* SPDX-License-Identifier: MIT */
#include "render_internal.h"
#include <math.h>

static bool attachment_alias(const doe_mtl_render_pass_desc *pass, const doe_mtl_texture *texture) {
    uint32_t i;
    for (i = 0; i < pass->color_count; ++i)
        if (pass->colors[i].view != NULL && pass->colors[i].view->texture == texture) return true;
    return (pass->depth.view != NULL && pass->depth.view->texture == texture) ||
        (pass->stencil.view != NULL && pass->stencil.view->texture == texture);
}
static doe_mtl_status bindings_check(doe_mtl_commands *commands, const doe_mtl_render_pass_desc *pass,
    const doe_mtl_render_bindings *bindings, uint32_t sizes_slot, doe_mtl_native_resources *native) {
    size_t i;
    doe_mtl_status status;
    status = doe_mtl_buffer_bindings_preflight(commands, bindings->buffers,
        bindings->buffer_count, sizes_slot, native->buffers);
    if (status != DOE_MTL_OK) return status;
    status = doe_mtl_image_bindings_preflight(commands, bindings->textures, bindings->texture_count,
        bindings->samplers, bindings->sampler_count, native);
    if (status != DOE_MTL_OK) return status;
    for (i = 0; i < bindings->texture_count; ++i)
        if (attachment_alias(pass, bindings->textures[i].view->texture)) return DOE_MTL_INVALID_ARGUMENT;
    return DOE_MTL_OK;
}
static doe_mtl_status vertex_fetch(const doe_mtl_render_draw *draw) {
    const doe_mtl_render_pipeline_desc *pipeline = &draw->pipeline->desc;
    size_t i, j;
    for (i = 0; i < pipeline->attribute_count; ++i) {
        const doe_mtl_vertex_attribute *attribute = &pipeline->attributes[i];
        const doe_mtl_vertex_layout *layout = NULL;
        const doe_mtl_buffer_binding *binding = NULL;
        uint64_t last = 0, bytes;
        for (j = 0; j < pipeline->layout_count; ++j)
            if (pipeline->layouts[j].slot == attribute->slot) layout = &pipeline->layouts[j];
        for (j = 0; j < draw->vertex.buffer_count; ++j)
            if (draw->vertex.buffers[j].slot == attribute->slot) binding = &draw->vertex.buffers[j];
        if (layout == NULL || binding == NULL) return DOE_MTL_INVALID_ARGUMENT;
        if (draw->count == 0 || draw->instance_count == 0) continue;
        if (layout->step == 2) last = (uint64_t)draw->first_instance + draw->instance_count - 1;
        else if (layout->step == 1 && draw->index_buffer == NULL) last = (uint64_t)draw->first_vertex + draw->count - 1;
        /* For indexed per-vertex input, the upstream admitted index-range proof
         * owns the maximum fetch. We still check the first attribute byte range. */
        bytes = last * layout->stride + attribute->offset + doe_mtl_vertex_format_size(attribute->format);
        if (bytes > binding->size) return DOE_MTL_RANGE;
    }
    return DOE_MTL_OK;
}
static doe_mtl_status dynamic_state(const doe_mtl_render_draw *draw, const doe_mtl_native_render_pass *pass) {
    const doe_mtl_viewport *viewport = &draw->viewport;
    const doe_mtl_scissor *scissor = &draw->scissor;
    unsigned i;
    if (draw->topology > 4 || draw->winding > 1 || draw->cull > 2 ||
        !isfinite(draw->depth_bias) || !isfinite(draw->depth_slope) || !isfinite(draw->depth_clamp)) return DOE_MTL_INVALID_ARGUMENT;
    for (i = 0; i < 4; ++i) if (!isfinite(draw->blend_constant[i])) return DOE_MTL_INVALID_ARGUMENT;
    if (draw->viewport_enabled && (!isfinite(viewport->x) || !isfinite(viewport->y) ||
        !isfinite(viewport->width) || !isfinite(viewport->height) || !isfinite(viewport->min_depth) || !isfinite(viewport->max_depth) ||
        viewport->width <= 0 || viewport->height <= 0 || viewport->x < 0 || viewport->y < 0 ||
        viewport->x > pass->width || viewport->width > (double)pass->width - viewport->x ||
        viewport->y > pass->height || viewport->height > (double)pass->height - viewport->y ||
        viewport->min_depth < 0 || viewport->max_depth > 1 || viewport->min_depth > viewport->max_depth)) return DOE_MTL_RANGE;
    if (draw->scissor_enabled && (scissor->width == 0 || scissor->height == 0 ||
        scissor->x > pass->width || scissor->width > pass->width - scissor->x ||
        scissor->y > pass->height || scissor->height > pass->height - scissor->y)) return DOE_MTL_RANGE;
    return DOE_MTL_OK;
}
doe_mtl_status doe_mtl_render_draw_preflight(doe_mtl_commands *commands,
    const doe_mtl_render_pass_desc *pass, const doe_mtl_native_render_pass *native_pass,
    const doe_mtl_render_draw *draw, doe_mtl_native_render_draw *native) {
    const doe_mtl_render_pipeline_desc *pipeline;
    doe_mtl_status status;
    uint32_t i;
    if (draw->pipeline == NULL) return DOE_MTL_INVALID_ARGUMENT;
    if (draw->pipeline->context != commands->context) return DOE_MTL_DEVICE_MISMATCH;
    pipeline = &draw->pipeline->desc;
    if (pipeline->color_count != pass->color_count ||
        pipeline->depth_stencil.depth_format != (pass->depth.view != NULL ? pass->depth.view->desc.format : 0) ||
        pipeline->depth_stencil.stencil_format != (pass->stencil.view != NULL ? pass->stencil.view->desc.format : 0))
        return DOE_MTL_INVALID_ARGUMENT;
    for (i = 0; i < pipeline->color_count; ++i)
        if (pipeline->colors[i].format != (pass->colors[i].view != NULL ? pass->colors[i].view->desc.format : 0))
            return DOE_MTL_INVALID_ARGUMENT;
    status = dynamic_state(draw, native_pass);
    if (status == DOE_MTL_OK) status = bindings_check(commands, pass, &draw->vertex, pipeline->vertex.sizes_slot, &native->vertex);
    if (status == DOE_MTL_OK) status = bindings_check(commands, pass, &draw->fragment, pipeline->fragment.sizes_slot, &native->fragment);
    if (status == DOE_MTL_OK) status = vertex_fetch(draw);
    if (status != DOE_MTL_OK) return status;
    if (draw->index_buffer != NULL) {
        if (draw->first_vertex != 0) return DOE_MTL_INVALID_ARGUMENT;
        const uint64_t element_size = draw->index_type == 0 ? 2 : 4;
        if (draw->index_type > 1) return DOE_MTL_UNSUPPORTED;
        status = doe_mtl_buffer_preflight(commands, draw->index_buffer, draw->count != 0 && draw->instance_count != 0);
        if (status != DOE_MTL_OK) return status;
        if (draw->index_offset % element_size != 0) return DOE_MTL_ALIGNMENT;
        if (!doe_mtl_range(draw->index_buffer->size, draw->index_offset, (uint64_t)draw->count * element_size)) return DOE_MTL_RANGE;
        native->index_buffer = draw->index_buffer->native;
        native->index_offset = (size_t)draw->index_offset;
    } else if (draw->base_vertex != 0) return DOE_MTL_INVALID_ARGUMENT;
    native->pipeline = draw->pipeline->native;
    native->depth_stencil = draw->pipeline->depth_stencil;
    native->vertex_sizes_slot = pipeline->vertex.sizes_slot;
    native->fragment_sizes_slot = pipeline->fragment.sizes_slot;
    native->values = *draw;
    return DOE_MTL_OK;
}
