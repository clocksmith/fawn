/* SPDX-License-Identifier: MIT */
#include "objc_abi.h"

static void stage_bindings(doe_mtl_objc *r, void *encoder, const doe_mtl_native_resources *resources,
    uint32_t sizes_slot, bool vertex) {
    doe_mtl_objc_graphics *g = &r->graphics;
    uint32_t sizes[DOE_MTL_BUFFER_SLOTS] = {0};
    uint32_t i;
    for (i = 0; i < DOE_MTL_BUFFER_SLOTS; ++i) {
        r->bind_buffer_call(encoder, vertex ? g->set_vertex_buffer : g->set_fragment_buffer,
            resources->buffers[i].buffer, resources->buffers[i].offset, i);
        sizes[i] = resources->buffers[i].size;
    }
    if (sizes_slot != UINT32_MAX)
        r->bind_bytes_call(encoder, vertex ? g->set_vertex_bytes : g->set_fragment_bytes,
            sizes, ((size_t)sizes_slot + 1) * sizeof(uint32_t), sizes_slot);
    /* Each draw is an explicit binding snapshot. Clear absent slots, including
     * high slots left by the previous draw in this same native encoder. */
    for (i = 0; i < DOE_MTL_TEXTURE_SLOTS; ++i)
        r->bind_image_call(encoder, vertex ? g->set_vertex_texture : g->set_fragment_texture, resources->textures[i], i);
    for (i = 0; i < DOE_MTL_SAMPLER_SLOTS; ++i)
        r->bind_image_call(encoder, vertex ? g->set_vertex_sampler : g->set_fragment_sampler, resources->samplers[i], i);
}
void doe_mtl_objc_render_encode_draw(doe_mtl_objc *r, void *encoder,
    const doe_mtl_native_render_pass *pass, const doe_mtl_native_render_draw *draw) {
    doe_mtl_objc_graphics *g = &r->graphics;
    const doe_mtl_render_draw *v = &draw->values;
    doe_mtl_viewport viewport = {0, 0, pass->width, pass->height, 0, 1};
    doe_mtl_scissor_abi scissor = {0, 0, pass->width, pass->height};
    if (!draw->indirect_arguments && (v->count == 0 || v->instance_count == 0)) return;
    if (v->viewport_enabled) viewport = v->viewport;
    if (v->scissor_enabled) scissor = (doe_mtl_scissor_abi){v->scissor.x, v->scissor.y, v->scissor.width, v->scissor.height};
    /* Logical empty raster state still runs vertices/visibility ordering. Use a
     * nondegenerate native viewport and an empty scissor; never elide the draw. */
    if (viewport.width == 0 || viewport.height == 0) {
        viewport = (doe_mtl_viewport){0, 0, 1, 1, viewport.min_depth, viewport.max_depth};
        scissor = (doe_mtl_scissor_abi){0};
    } else if (scissor.width == 0 || scissor.height == 0) scissor = (doe_mtl_scissor_abi){0};
    r->set_object_call(encoder, g->set_pipeline, draw->pipeline);
    r->set_object_call(encoder, g->set_depth_state, draw->depth_stencil);
    stage_bindings(r, encoder, &draw->vertex, draw->vertex_sizes_slot, true);
    stage_bindings(r, encoder, &draw->fragment, draw->fragment_sizes_slot, false);
    g->viewport(encoder, g->set_viewport, viewport);
    g->scissor(encoder, g->set_scissor, scissor);
    r->set_size_call(encoder, g->set_winding, v->winding);
    r->set_size_call(encoder, g->set_cull, v->cull);
    g->blend_color(encoder, g->set_blend_color, v->blend_constant[0], v->blend_constant[1], v->blend_constant[2], v->blend_constant[3]);
    g->depth_bias(encoder, g->set_depth_bias, v->depth_bias, v->depth_slope, v->depth_clamp);
    g->set_u32(encoder, g->set_stencil_reference, v->stencil_reference);
    if (draw->indirect_arguments && draw->index_buffer)
        g->draw_indexed_indirect(encoder, g->draw_indexed_primitives_indirect, v->topology, v->index_type,
            draw->index_buffer, draw->index_offset, draw->indirect_arguments, 0);
    else if (draw->indirect_arguments)
        g->draw_indirect(encoder, g->draw_primitives_indirect, v->topology, draw->indirect_arguments, 0);
    else if (draw->index_buffer == NULL)
        g->draw(encoder, g->draw_primitives, v->topology, v->first_vertex, v->count, v->instance_count, v->first_instance);
    else g->draw_indexed(encoder, g->draw_indexed_primitives, v->topology, v->count, v->index_type,
        draw->index_buffer, draw->index_offset, v->instance_count, v->base_vertex, v->first_instance);
}
