/* SPDX-License-Identifier: MIT */
#include "render_internal.h"
#include <stdlib.h>
#include <string.h>

/* Named public index formats to native VkIndexType names; never a numeric cast.
 * The resource owner already admitted the format at SetIndexBuffer. */
static bool index_format(WGPUIndexFormat format, doe_vk_index_format *out)
{
    switch (format) {
    case WGPUIndexFormat_Uint16:
        *out = DOE_VK_INDEX_UINT16;
        return true;
    case WGPUIndexFormat_Uint32:
        *out = DOE_VK_INDEX_UINT32;
        return true;
    case WGPUIndexFormat_Undefined:
    case WGPUIndexFormat_Force32:
        return false;
    }
    return false;
}

/* A temporary typed conversion of one immutable resource pass span. There is no
 * new recording state or lease registry: the resource owner already finalized
 * ordering, and one backend call consumes this entire borrowed pass. */
static doe_vk_status prepare_draw(doe_vk_commands *commands, const doe_resource_render_packet *packet,
    doe_vk_render_pass_draw *draw, doe_vk_binding_resource *bindings, doe_vk_buffer_binding *vertices,
    doe_vk_render_indirect *indirect, doe_vk_render_indices *indices)
{
    native_pipeline *pipeline = doe_resource_render_pipeline_native(packet->pipeline);
    doe_vk_status status = native_resolve_bindings(commands, pipeline,
        doe_resource_render_pipeline_get_layout(packet->pipeline), packet->groups, &packet->offsets, bindings);
    if (status != DOE_VK_OK) return status;
    for (size_t i = 0; i < pipeline->vertex_count; ++i) {
        native_buffer *buffer = doe_resource_buffer_native(packet->vertices[i].buffer);
        vertices[i] = (doe_vk_buffer_binding){buffer ? buffer->handle.vulkan : NULL,
            packet->vertices[i].offset,packet->vertices[i].size};
    }
    *draw = (doe_vk_render_pass_draw){.pipeline=pipeline->handle.vulkan,
        .bindings=bindings,.binding_count=pipeline->binding_count,
        .vertices=vertices,.vertex_buffer_count=pipeline->vertex_count,
        .draw={.vertex_count=packet->count,.instance_count=packet->instances,.first_vertex=packet->first,
            .first_instance=packet->first_instance,.viewport_convention=DOE_VK_VIEWPORT_WEBGPU,
            .override_blend_constant=true,.stencil_reference=packet->state.stencil_reference}};
    if (packet->indirect) {
        status = native_draw_guard_initialize(pipeline->device);
        if (status != DOE_VK_OK) return status;
        native_buffer *arguments = doe_resource_buffer_native(packet->indirect);
        *indirect = (doe_vk_render_indirect){pipeline->device->draw_validator,
            arguments->handle.vulkan,packet->indirect_offset};
        draw->indirect = indirect;
    }
    if (packet->indexed) {
        /* Indexed counts move from the shared packet fields into the native
         * index record; direct vertex fields stay zero for the backend. */
        native_buffer *buffer = doe_resource_buffer_native(packet->index.buffer);
        doe_vk_index_format format;
        if (!buffer || !index_format(packet->index_format, &format)) return DOE_VK_INVALID_ARGUMENT;
        *indices = (doe_vk_render_indices){buffer->handle.vulkan, packet->index.offset, packet->index.size, format,
            packet->indirect ? 0 : packet->count, packet->indirect ? 0 : packet->first,
            packet->indirect ? 0 : packet->base_vertex};
        draw->draw.vertex_count = 0;
        draw->draw.first_vertex = 0;
        draw->indices = indices;
    }
    memcpy(draw->draw.viewport, packet->state.viewport, sizeof(draw->draw.viewport));
    memcpy(draw->draw.scissor, packet->state.scissor, sizeof(draw->draw.scissor));
    doe_vk_clear_color blend;
    if (!native_render_clear(WGPUTextureFormat_RGBA32Float, packet->state.blend, &blend)) return DOE_VK_UNSUPPORTED;
    memcpy(draw->draw.blend_constant, blend.float32, sizeof(draw->draw.blend_constant));
    return DOE_VK_OK;
}

static bool add_size(size_t *total, size_t count)
{
    if (*total > SIZE_MAX - count) return false;
    *total += count;
    return true;
}

static bool same_attachments(const doe_resource_render_targets *a, const doe_resource_render_targets *b)
{
    if (a->color_count != b->color_count || a->depth_stencil != b->depth_stencil) return false;
    for (uint32_t i = 0; i < a->color_count; ++i)
        if (a->colors[i].view != b->colors[i].view || a->colors[i].resolve_target != b->colors[i].resolve_target)
            return false;
    return true;
}

/* Converts every canonical slot; unused slots stay NULL with no operations. */
static doe_vk_status lower_colors(const doe_resource_render_targets *source, doe_vk_render_pass_desc *pass,
    native_view *views[DOE_RESOURCE_MAX_COLOR_ATTACHMENTS])
{
    pass->color_attachment_count = source->color_count;
    for (uint32_t i = 0; i < source->color_count; ++i) {
        const doe_resource_color_attachment *color = &source->colors[i];
        views[i] = doe_resource_view_native(color->view);
        if (!views[i]) continue;
        doe_vk_color_attachment *slot = &pass->color_attachments[i];
        native_view *resolve = doe_resource_view_native(color->resolve_target);
        *slot = (doe_vk_color_attachment){.view=views[i]->view,.discard=color->store == WGPUStoreOp_Discard,
            .resolve_target=resolve ? resolve->view : NULL};
        if (color->load == WGPULoadOp_Clear) {
            const doe_resource_view_desc *view = doe_resource_view_description(color->view);
            if (!view || !native_render_clear(view->format, color->clear, &slot->clear_value)) return DOE_VK_INVALID_ARGUMENT;
            slot->clear = true;
        } else if (color->load != WGPULoadOp_Load) return DOE_VK_INVALID_ARGUMENT;
    }
    return DOE_VK_OK;
}

doe_vk_status native_record_render(doe_vk_commands *commands,
    const doe_resource_command *records, size_t count, size_t *consumed)
{
    if (!count || records[0].kind != DOE_COMMAND_RENDER) return DOE_VK_INVALID_ARGUMENT;
    const doe_resource_render_packet *begin = doe_resource_render_record_borrow(records[0].value.render);
    if (!begin || !begin->boundary_only || !begin->first_in_pass || begin->last_in_pass)
        return DOE_VK_INVALID_ARGUMENT;
    size_t end = 0, binding_count = 0, vertex_count = 0, indirect_count = 0, indexed_count = 0;
    for (size_t i = 1; i < count; ++i) {
        if (records[i].kind == DOE_COMMAND_BEGIN_OCCLUSION || records[i].kind == DOE_COMMAND_END_OCCLUSION) {
            if (records[i].value.timestamp.set != begin->targets.occlusion_queries) return DOE_VK_INVALID_ARGUMENT;
            continue;
        }
        if (records[i].kind != DOE_COMMAND_RENDER) return DOE_VK_UNSUPPORTED;
        const doe_resource_render_packet *packet = doe_resource_render_record_borrow(records[i].value.render);
        if (!packet || packet->first_in_pass || !same_attachments(&packet->targets, &begin->targets) ||
            packet->targets.occlusion_queries != begin->targets.occlusion_queries) return DOE_VK_INVALID_ARGUMENT;
        if (packet->boundary_only) {
            if (!packet->last_in_pass) return DOE_VK_INVALID_ARGUMENT;
            end = i;
            break;
        }
        if (packet->last_in_pass) return DOE_VK_UNSUPPORTED;
        if (packet->indirect && !add_size(&indirect_count,1)) return DOE_VK_OUT_OF_MEMORY;
        if (packet->indexed && !add_size(&indexed_count,1)) return DOE_VK_OUT_OF_MEMORY;
        native_pipeline *pipeline = doe_resource_render_pipeline_native(packet->pipeline);
        if (!pipeline || !add_size(&binding_count, pipeline->binding_count) ||
            !add_size(&vertex_count, pipeline->vertex_count)) return DOE_VK_OUT_OF_MEMORY;
    }
    if (!end) return DOE_VK_INVALID_ARGUMENT;
    size_t op_count = end - 1;
    if (op_count > SIZE_MAX / sizeof(doe_vk_render_pass_op) ||
        indirect_count > SIZE_MAX / sizeof(doe_vk_render_indirect) ||
        indexed_count > SIZE_MAX / sizeof(doe_vk_render_indices) || binding_count > SIZE_MAX / sizeof(doe_vk_binding_resource) ||
        vertex_count > SIZE_MAX / sizeof(doe_vk_buffer_binding)) return DOE_VK_OUT_OF_MEMORY;
    doe_vk_render_pass_op *ops = op_count ? calloc(op_count, sizeof(*ops)) : NULL;
    doe_vk_render_indirect *indirect = indirect_count ? calloc(indirect_count,sizeof(*indirect)) : NULL;
    doe_vk_render_indices *indices = indexed_count ? calloc(indexed_count,sizeof(*indices)) : NULL;
    doe_vk_binding_resource *bindings = binding_count ? calloc(binding_count, sizeof(*bindings)) : NULL;
    doe_vk_buffer_binding *vertices = vertex_count ? calloc(vertex_count, sizeof(*vertices)) : NULL;
    doe_vk_status status = (op_count && !ops) || (indirect_count && !indirect) || (indexed_count && !indices) ||
        (binding_count && !bindings) || (vertex_count && !vertices) ?
        DOE_VK_OUT_OF_MEMORY : DOE_VK_OK;
    size_t binding_at = 0, vertex_at = 0, indirect_at = 0, indexed_at = 0;
    for (size_t i = 1; status == DOE_VK_OK && i < end; ++i) {
        doe_vk_render_pass_op *op = &ops[i - 1];
        if (records[i].kind == DOE_COMMAND_BEGIN_OCCLUSION) {
            op->kind = DOE_VK_RENDER_BEGIN_OCCLUSION;
            op->value.query_index = records[i].value.timestamp.index;
        } else if (records[i].kind == DOE_COMMAND_END_OCCLUSION) op->kind = DOE_VK_RENDER_END_OCCLUSION;
        else {
            op->kind = DOE_VK_RENDER_DRAW;
            const doe_resource_render_packet *packet = doe_resource_render_record_borrow(records[i].value.render);
            status = prepare_draw(commands, packet, &op->value.draw, bindings ? bindings + binding_at : NULL,
                vertices ? vertices + vertex_at : NULL, packet->indirect ? &indirect[indirect_at++] : NULL,
                packet->indexed ? &indices[indexed_at++] : NULL);
            if (status == DOE_VK_OK) {
                binding_at += op->value.draw.binding_count;
                vertex_at += op->value.draw.vertex_buffer_count;
            }
        }
    }
    doe_vk_render_pass_desc pass = {0};
    native_view *colors[DOE_RESOURCE_MAX_COLOR_ATTACHMENTS] = {0};
    if (status == DOE_VK_OK) status = lower_colors(&begin->targets, &pass, colors);
    if (status == DOE_VK_OK) {
        native_queries *query = doe_resource_query_set_native(begin->targets.occlusion_queries);
        native_view *depth = doe_resource_view_native(begin->targets.depth_stencil);
        const doe_resource_render_targets *source = &begin->targets;
        pass.depth_stencil = depth ? depth->view : NULL;
        pass.clear_depth = source->depth_load == WGPULoadOp_Clear;
        pass.clear_stencil = source->stencil_load == WGPULoadOp_Clear;
        pass.discard_depth = source->depth_store == WGPUStoreOp_Discard;
        pass.discard_stencil = source->stencil_store == WGPUStoreOp_Discard;
        pass.depth_read_only = source->depth_read_only;
        pass.stencil_read_only = source->stencil_read_only;
        pass.depth_clear_value = source->clear_depth;
        pass.stencil_clear_value = source->clear_stencil;
        /* Canonical speculative aspect state decides whether a real zero clear
         * is needed after Discard. No native shadow initialization bit exists. */
        for (uint32_t i = 0; status == DOE_VK_OK && i < source->color_count; ++i)
            if (colors[i]) status = doe_vk_commands_initialize_attachment(commands, colors[i]->view);
        if (status == DOE_VK_OK && depth) status = doe_vk_commands_initialize_attachment(commands, depth->view);
        if (status == DOE_VK_OK) status = doe_vk_commands_render_pass_full(commands, &pass,
            query ? query->pool : NULL, ops, op_count);
    }
    free(indirect);
    free(indices);
    free(vertices);
    free(bindings);
    free(ops);
    if (status == DOE_VK_OK) *consumed = end + 1;
    return status;
}
