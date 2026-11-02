/* SPDX-License-Identifier: MIT */
#include "objc_abi.h"

static uint32_t first_slot(uint64_t mask) {
    uint32_t slot = 0;
    while (!(mask & 1)) { mask >>= 1; ++slot; }
    return slot;
}
static bool bytes_at(const doe_mtl_render_bindings *resources, uint32_t sizes_slot, uint32_t slot) {
    return sizes_slot == slot || (resources->immediates.size && resources->immediates.slot == slot);
}
static void stage_bindings(doe_mtl_objc *r, void *encoder, const doe_mtl_render_bindings *resources,
    uint32_t sizes_slot, doe_mtl_objc_stage_state *state, bool vertex) {
    doe_mtl_objc_graphics *g = &r->graphics;
    doe_mtl_native_resources *old = &state->bindings;
    uint32_t sizes[DOE_MTL_BUFFER_SLOTS] = {0}, buffers = 0, samplers = 0;
    uint64_t textures[2] = {0};
    bool sizes_changed = !state->valid || state->sizes_slot != sizes_slot;
    for (size_t i = 0; i < resources->buffer_count; ++i) {
        const doe_mtl_buffer_binding *binding = &resources->buffers[i];
        uint32_t slot = binding->slot;
        void *native = doe_mtl_buffer_native(binding->buffer);
        buffers |= UINT32_C(1) << slot;
        if (native != old->buffers[slot].buffer || binding->offset != old->buffers[slot].offset ||
            (state->valid && (state->sizes_slot == slot || (old->immediate_size && old->immediate_slot == slot))))
            r->bind_buffer_call(encoder, vertex ? g->set_vertex_buffer : g->set_fragment_buffer,
                native, (size_t)binding->offset, slot);
        sizes[slot] = (uint32_t)binding->size;
        if (slot <= sizes_slot && sizes[slot] != old->buffers[slot].size) sizes_changed = true;
        old->buffers[slot] = (doe_mtl_native_binding){native,(size_t)binding->offset,sizes[slot]};
    }
    uint32_t removed = state->buffer_mask & ~buffers;
    while (removed) {
        uint32_t slot = first_slot(removed); removed &= removed - 1;
        if (slot <= sizes_slot && old->buffers[slot].size) sizes_changed = true;
        old->buffers[slot] = (doe_mtl_native_binding){0};
        if (!bytes_at(resources, sizes_slot, slot))
            r->bind_buffer_call(encoder, vertex ? g->set_vertex_buffer : g->set_fragment_buffer, NULL, 0, slot);
    }
    if (state->valid) {
        uint32_t old_bytes[2] = {state->sizes_slot, old->immediate_size ? old->immediate_slot : UINT32_MAX};
        for (size_t i = 0; i < 2; ++i) if (old_bytes[i] != UINT32_MAX &&
            !bytes_at(resources, sizes_slot, old_bytes[i]) && !(buffers & (UINT32_C(1) << old_bytes[i])))
            r->bind_buffer_call(encoder, vertex ? g->set_vertex_buffer : g->set_fragment_buffer, NULL, 0, old_bytes[i]);
    }
    if (sizes_slot != UINT32_MAX && (sizes_changed || (old->immediate_size && old->immediate_slot == sizes_slot)))
        r->bind_bytes_call(encoder, vertex ? g->set_vertex_bytes : g->set_fragment_bytes,
            sizes, ((size_t)sizes_slot + 1) * sizeof(uint32_t), sizes_slot);
    if (resources->immediates.size && (!state->valid || resources->immediates.slot != old->immediate_slot ||
        resources->immediates.size != old->immediate_size ||
        memcmp(resources->immediates.bytes, old->immediate_words, resources->immediates.size) != 0))
        r->bind_bytes_call(encoder, vertex ? g->set_vertex_bytes : g->set_fragment_bytes,
            resources->immediates.bytes, resources->immediates.size, resources->immediates.slot);
    old->immediate_size = resources->immediates.size;
    old->immediate_slot = resources->immediates.slot;
    if (old->immediate_size) memcpy(old->immediate_words, resources->immediates.bytes, old->immediate_size);
    for (size_t i = 0; i < resources->texture_count; ++i) {
        uint32_t slot = resources->textures[i].slot;
        void *native = doe_mtl_texture_view_native(resources->textures[i].view);
        textures[slot / 64] |= UINT64_C(1) << (slot % 64);
        if (native != old->textures[slot]) r->bind_image_call(encoder,
            vertex ? g->set_vertex_texture : g->set_fragment_texture, native, slot);
        old->textures[slot] = native;
    }
    for (size_t word = 0; word < 2; ++word) {
        uint64_t missing = state->texture_mask[word] & ~textures[word];
        while (missing) {
            uint32_t slot = first_slot(missing) + (uint32_t)word * 64; missing &= missing - 1;
            r->bind_image_call(encoder, vertex ? g->set_vertex_texture : g->set_fragment_texture, NULL, slot);
            old->textures[slot] = NULL;
        }
        state->texture_mask[word] = textures[word];
    }
    for (size_t i = 0; i < resources->sampler_count; ++i) {
        uint32_t slot = resources->samplers[i].slot;
        void *native = doe_mtl_sampler_native(resources->samplers[i].sampler);
        samplers |= UINT32_C(1) << slot;
        if (native != old->samplers[slot]) r->bind_image_call(encoder,
            vertex ? g->set_vertex_sampler : g->set_fragment_sampler, native, slot);
        old->samplers[slot] = native;
    }
    removed = state->sampler_mask & ~samplers;
    while (removed) {
        uint32_t slot = first_slot(removed); removed &= removed - 1;
        r->bind_image_call(encoder, vertex ? g->set_vertex_sampler : g->set_fragment_sampler, NULL, slot);
        old->samplers[slot] = NULL;
    }
    state->buffer_mask = buffers; state->sampler_mask = samplers;
    state->sizes_slot = sizes_slot; state->valid = true;
}
static void raster_state(const doe_mtl_native_render_pass *pass, const doe_mtl_render_draw *draw,
    doe_mtl_viewport *viewport, doe_mtl_scissor_abi *scissor) {
    *viewport = draw->viewport_enabled ? draw->viewport : (doe_mtl_viewport){0,0,pass->width,pass->height,0,1};
    *scissor = draw->scissor_enabled ? (doe_mtl_scissor_abi){draw->scissor.x,draw->scissor.y,
        draw->scissor.width,draw->scissor.height} : (doe_mtl_scissor_abi){0,0,pass->width,pass->height};
    /* Empty raster state still executes vertex effects and visibility changes. */
    if (viewport->width == 0 || viewport->height == 0) {
        *viewport = (doe_mtl_viewport){0,0,1,1,viewport->min_depth,viewport->max_depth};
        *scissor = (doe_mtl_scissor_abi){0};
    } else if (scissor->width == 0 || scissor->height == 0) *scissor = (doe_mtl_scissor_abi){0};
}
static bool same_viewport(doe_mtl_viewport a, doe_mtl_viewport b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height &&
        a.min_depth == b.min_depth && a.max_depth == b.max_depth;
}
static bool same_scissor(doe_mtl_scissor_abi a, doe_mtl_scissor_abi b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

bool doe_mtl_objc_render_encode_draw(doe_mtl_objc *r, void *encoder,
    const doe_mtl_native_render_pass *pass, const doe_mtl_native_render_draw *draw,
    doe_mtl_objc_render_state *state) {
    doe_mtl_objc_graphics *g = &r->graphics;
    const doe_mtl_render_draw *v = &draw->values;
    doe_mtl_viewport viewport, old_viewport;
    doe_mtl_scissor_abi scissor, old_scissor;
    const doe_mtl_native_render_draw *previous = state->previous;
    const doe_mtl_render_draw *old = previous ? &previous->values : NULL;
    if (!draw->indirect_arguments && (v->count == 0 || v->instance_count == 0)) return false;
    raster_state(pass, v, &viewport, &scissor);
    if (old) raster_state(pass, old, &old_viewport, &old_scissor);
    if (!previous || previous->pipeline != draw->pipeline)
        r->set_object_call(encoder, g->set_pipeline, draw->pipeline);
    if (!previous || previous->depth_stencil != draw->depth_stencil)
        r->set_object_call(encoder, g->set_depth_state, draw->depth_stencil);
    stage_bindings(r, encoder, &draw->values.vertex, draw->vertex_sizes_slot,
        &state->vertex, true);
    stage_bindings(r, encoder, &draw->values.fragment, draw->fragment_sizes_slot,
        &state->fragment, false);
    if (!old || !same_viewport(viewport, old_viewport)) g->viewport(encoder, g->set_viewport, viewport);
    if (!old || !same_scissor(scissor, old_scissor)) g->scissor(encoder, g->set_scissor, scissor);
    if (!old || v->winding != old->winding) r->set_size_call(encoder, g->set_winding, v->winding);
    if (!old || v->cull != old->cull) r->set_size_call(encoder, g->set_cull, v->cull);
    if (!old || v->unclipped_depth != old->unclipped_depth)
        r->set_size_call(encoder, g->set_depth_clip, v->unclipped_depth ? 1 : 0);
    if (!old || memcmp(v->blend_constant, old->blend_constant, sizeof(v->blend_constant)))
        g->blend_color(encoder, g->set_blend_color, v->blend_constant[0], v->blend_constant[1], v->blend_constant[2], v->blend_constant[3]);
    if (!old || v->depth_bias != old->depth_bias || v->depth_slope != old->depth_slope || v->depth_clamp != old->depth_clamp)
        g->depth_bias(encoder, g->set_depth_bias, v->depth_bias, v->depth_slope, v->depth_clamp);
    if (!old || v->stencil_reference != old->stencil_reference)
        g->set_u32(encoder, g->set_stencil_reference, v->stencil_reference);
    if (draw->indirect_arguments && draw->index_buffer)
        g->draw_indexed_indirect(encoder, g->draw_indexed_primitives_indirect, v->topology, v->index_type,
            draw->index_buffer, draw->index_offset, draw->indirect_arguments, draw->indirect_offset);
    else if (draw->indirect_arguments)
        g->draw_indirect(encoder, g->draw_primitives_indirect, v->topology, draw->indirect_arguments, draw->indirect_offset);
    else if (draw->index_buffer == NULL)
        g->draw(encoder, g->draw_primitives, v->topology, v->first_vertex, v->count, v->instance_count, v->first_instance);
    else g->draw_indexed(encoder, g->draw_indexed_primitives, v->topology, v->count, v->index_type,
        draw->index_buffer, draw->index_offset, v->instance_count, v->base_vertex, v->first_instance);
    state->previous = draw;
    return true;
}
