/* SPDX-License-Identifier: MIT */
#include "render_internal.h"
#include "query_internal.h"
#include <math.h>

doe_vk_status doe_vk_render_viewport(const doe_vk_context *context, const doe_vk_render_draw *draw,
    uint32_t width, uint32_t height, VkViewport *out) {
    if (context == NULL || draw == NULL || out == NULL ||
        (draw->viewport_convention != DOE_VK_VIEWPORT_RAW_VULKAN &&
         draw->viewport_convention != DOE_VK_VIEWPORT_WEBGPU)) return DOE_VK_INVALID_ARGUMENT;
    if (draw->viewport_convention == DOE_VK_VIEWPORT_WEBGPU && context->caps.api_version < (1u << 22 | 1u << 12))
        return DOE_VK_UNSUPPORTED;
    const VkPhysicalDeviceLimits *limits = &context->properties.limits;
    for (unsigned i = 0; i < 6; ++i) if (!isfinite(draw->viewport[i])) return DOE_VK_INVALID_ARGUMENT;
    const float *v = draw->viewport;
    if (v[2] < 0 || v[3] < 0 || v[4] < 0 || v[4] > 1 || v[5] < 0 || v[5] > 1 ||
        v[2] > limits->maxViewportDimensions[0] || v[3] > limits->maxViewportDimensions[1] ||
        v[0] < limits->viewportBoundsRange[0] || v[1] < limits->viewportBoundsRange[0] ||
        (double)v[0] + v[2] > limits->viewportBoundsRange[1] ||
        (double)v[1] + v[3] > limits->viewportBoundsRange[1] ||
        draw->scissor[0] > INT32_MAX || draw->scissor[1] > INT32_MAX ||
        !doe_vk_range(width, draw->scissor[0], draw->scissor[2]) ||
        !doe_vk_range(height, draw->scissor[1], draw->scissor[3])) return DOE_VK_INVALID_ARGUMENT;
    VkViewport viewport = {v[0], v[1], v[2], v[3], v[4], v[5]};
    if (v[2] == 0 || v[3] == 0) {
        /* Vulkan width must be positive. The original logical bounds above
         * remain mandatory; an empty scissor below preserves vertex work. */
        if (limits->maxViewportDimensions[0] < 1 || limits->maxViewportDimensions[1] < 1 ||
            limits->viewportBoundsRange[0] > 0 || limits->viewportBoundsRange[1] < 1)
            return DOE_VK_UNSUPPORTED;
        viewport = (VkViewport){0, 0, 1, 1, v[4], v[5]};
    } else if (draw->viewport_convention == DOE_VK_VIEWPORT_WEBGPU) {
        viewport.y = v[1] + v[3];
        viewport.height = -v[3];
        if (!isfinite(viewport.y) || viewport.y > limits->viewportBoundsRange[1] ||
            (double)viewport.y + viewport.height < limits->viewportBoundsRange[0] ||
            (double)viewport.y + viewport.height > limits->viewportBoundsRange[1])
            return DOE_VK_INVALID_ARGUMENT;
    }
    *out = viewport;
    return DOE_VK_OK;
}

typedef struct prepared_draw {
    doe_vk_dispatch_use *bindings;
    VkViewport viewport;
    VkRect2D scissor;
    VkBuffer buffers[DOE_VK_MAX_VERTEX_BUFFERS];
    VkDeviceSize offsets[DOE_VK_MAX_VERTEX_BUFFERS];
    uint32_t vertex_limits[2];
    uint32_t index_limit;
    VkBuffer index_buffer;
    VkBuffer indirect;
    bool samples_depth_stencil; /* Samples the depth/stencil attachment's texture. */
} prepared_draw;

/* Vulkan render-pass compatibility: equal per-slot formats, where missing
 * trailing slots and unused slots are both unused. */
static bool colors_compatible(const doe_vk_render_colors *colors, const doe_vk_pipeline *pipeline) {
    uint32_t count = colors->count > pipeline->color_count ? colors->count : pipeline->color_count;
    for (uint32_t i = 0; i < count; ++i) {
        const doe_vk_texture_view *view = i < colors->count ? colors->slots[i].view : NULL;
        doe_vk_texture_format pass = view != NULL ? view->format : 0;
        if (pass != (i < pipeline->color_count ? pipeline->color_formats[i] : 0)) return false;
    }
    return true;
}

static bool attached(const doe_vk_render_pass_desc *desc, const doe_vk_render_colors *colors,
    const doe_vk_texture *texture) {
    if (desc->depth_stencil != NULL && desc->depth_stencil->texture == texture) return true;
    for (uint32_t i = 0; i < colors->count; ++i) {
        const doe_vk_color_attachment *slot = &colors->slots[i];
        if ((slot->view != NULL && slot->view->texture == texture) ||
            (slot->resolve_target != NULL && slot->resolve_target->texture == texture)) return true;
    }
    return false;
}

static bool attached_color(const doe_vk_render_colors *colors, const doe_vk_texture *texture) {
    for (uint32_t i = 0; i < colors->count; ++i) {
        const doe_vk_color_attachment *slot = &colors->slots[i];
        if ((slot->view != NULL && slot->view->texture == texture) ||
            (slot->resolve_target != NULL && slot->resolve_target->texture == texture)) return true;
    }
    return false;
}

static bool same_subresource(const doe_vk_texture_view *a, const doe_vk_texture_view *b) {
    return a->desc.base_mip < b->desc.base_mip + b->desc.mip_count && b->desc.base_mip < a->desc.base_mip + a->desc.mip_count &&
        a->desc.base_layer < b->desc.base_layer + b->desc.layer_count && b->desc.base_layer < a->desc.base_layer + a->desc.layer_count;
}

/* Aspects the pass never writes: read-only ones and those the format lacks. */
static uint32_t read_only_aspects(const doe_vk_render_pass_desc *desc) {
    uint32_t present = doe_vk_format_aspects(desc->depth_stencil->texture->desc.format);
    uint32_t read_only = (desc->depth_read_only ? VK_IMAGE_ASPECT_DEPTH_BIT : 0) |
        (desc->stencil_read_only ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
    return read_only | ~present;
}

static doe_vk_status validate_pass_draw(doe_vk_commands *commands, const doe_vk_render_pass_desc *desc,
    const doe_vk_render_colors *colors, const doe_vk_render_pass_draw *draw, uint32_t width, uint32_t height,
    prepared_draw *prepared) {
    doe_vk_pipeline *pipeline = draw->pipeline;
    doe_vk_texture_view *target = desc->depth_stencil;
    for (uint32_t i = colors->count; i > 0; --i) if (colors->slots[i - 1].view != NULL) target = colors->slots[i - 1].view;
    if (pipeline == NULL || !pipeline->graphics || pipeline->context != commands->context ||
        draw->binding_count != pipeline->binding_count ||
        (draw->binding_count != 0 && draw->bindings == NULL) ||
        !colors_compatible(colors, pipeline) ||
        doe_vk_samples(target->texture->desc.sample_count) != doe_vk_samples(pipeline->sample_count) ||
        (desc->depth_stencil != NULL ? desc->depth_stencil->texture->desc.format : 0) != pipeline->depth_stencil_format ||
        (desc->depth_read_only && pipeline->depth_write) ||
        (desc->stencil_read_only && pipeline->stencil_write)) return DOE_VK_INVALID_ARGUMENT;
    if (draw->draw.override_blend_constant) {
        for (unsigned i = 0; i < 4; ++i)
            if (!isfinite(draw->draw.blend_constant[i])) return DOE_VK_INVALID_ARGUMENT;
    }
    doe_vk_status status = doe_vk_render_viewport(commands->context, &draw->draw, width, height, &prepared->viewport);
    if (status != DOE_VK_OK) return status;
    status = doe_vk_render_vertex_limits(commands, pipeline, draw->vertices,
        draw->vertex_buffer_count, prepared->vertex_limits);
    if (status != DOE_VK_OK) return status;
    const doe_vk_render_indices *indices = draw->indices;
    if (indices != NULL) {
        if (draw->draw.vertex_count != 0 || draw->draw.first_vertex != 0) return DOE_VK_INVALID_ARGUMENT;
        status = doe_vk_render_index_limit(commands, indices, &prepared->index_limit);
        if (status != DOE_VK_OK) return status;
    }
    if (draw->indirect != NULL) {
        status = doe_vk_draw_indirect_validate(commands, draw->indirect, indices != NULL);
        if (status != DOE_VK_OK) return status;
    } else if (indices != NULL) {
        /* Exact bound index range. Vertex-step capacity is not inferred from
         * GPU index values; robustBufferAccess owns those fetches. */
        const doe_vk_render_draw *counts = &draw->draw;
        uint64_t index_end = (uint64_t)indices->first_index + indices->index_count;
        uint64_t instance_end = (uint64_t)counts->first_instance + counts->instance_count;
        if (index_end > prepared->index_limit || instance_end > UINT32_MAX) return DOE_VK_INVALID_ARGUMENT;
        if (indices->index_count != 0 && counts->instance_count != 0 && instance_end > prepared->vertex_limits[1])
            return DOE_VK_INVALID_ARGUMENT;
    } else {
        const doe_vk_render_draw *counts = &draw->draw;
        uint64_t vertex_end = (uint64_t)counts->first_vertex + counts->vertex_count;
        uint64_t instance_end = (uint64_t)counts->first_instance + counts->instance_count;
        if (vertex_end > UINT32_MAX || instance_end > UINT32_MAX)
            return DOE_VK_INVALID_ARGUMENT;
        if (counts->vertex_count != 0 && counts->instance_count != 0 &&
            (vertex_end > prepared->vertex_limits[0] || instance_end > prepared->vertex_limits[1]))
            return DOE_VK_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < draw->binding_count; ++i) {
        const doe_vk_texture_view *view = draw->bindings[i].value.view;
        if ((draw->bindings[i].kind != DOE_VK_BINDING_SAMPLED_TEXTURE &&
            draw->bindings[i].kind != DOE_VK_BINDING_STORAGE_TEXTURE) ||
            view == NULL || !attached(desc, colors, view->texture)) continue;
        /* Sampling the depth/stencil attachment's texture: another subresource,
         * or aspects the pass only reads (WebGPU read-only depth/stencil). */
        if (draw->bindings[i].kind == DOE_VK_BINDING_SAMPLED_TEXTURE && desc->depth_stencil != NULL &&
            view->texture == desc->depth_stencil->texture && !attached_color(colors, view->texture) &&
            (!same_subresource(view, desc->depth_stencil) || (view->aspects & ~read_only_aspects(desc)) == 0)) {
            prepared->samples_depth_stencil = true;
            continue;
        }
        return DOE_VK_UNSUPPORTED;
    }
    prepared->scissor = (VkRect2D){
        .offset = {(int32_t)draw->draw.scissor[0], (int32_t)draw->draw.scissor[1]},
        .extent = {draw->draw.scissor[2], draw->draw.scissor[3]}
    };
    if (draw->draw.viewport[2] == 0 || draw->draw.viewport[3] == 0)
        prepared->scissor.extent = (VkExtent2D){0, 0};
    return DOE_VK_OK;
}

static doe_vk_status prepare_draw(doe_vk_commands *commands, const doe_vk_render_pass_draw *draw,
    prepared_draw *prepared) {
    doe_vk_status status = doe_vk_prepare_resources(commands, draw->pipeline, draw->bindings,
        draw->binding_count, &prepared->bindings);
    if (status != DOE_VK_OK) return status;
    for (size_t i = 0; i < draw->vertex_buffer_count; ++i) {
        if (draw->vertices[i].buffer == NULL) continue;
        status = doe_vk_use_buffer(commands, draw->vertices[i].buffer);
        if (status != DOE_VK_OK) return status;
        prepared->buffers[i] = draw->vertices[i].buffer->buffer;
        prepared->offsets[i] = draw->vertices[i].offset;
    }
    if (draw->indices != NULL) {
        status = doe_vk_use_buffer(commands, draw->indices->buffer);
        if (status != DOE_VK_OK) return status;
        prepared->index_buffer = draw->indices->buffer->buffer;
    }
    return DOE_VK_OK;
}

static void record_draw(doe_vk_commands *commands, const doe_vk_render_pass_draw *draw,
    const prepared_draw *prepared) {
    doe_vk_context *context = commands->context;
    doe_vk_bind_prepared_resources(commands, prepared->bindings);
    const float *blend = draw->draw.override_blend_constant ?
        draw->draw.blend_constant : draw->pipeline->blend_constant;
    context->vk.vkCmdSetBlendConstants(commands->command, blend);
    if (draw->pipeline->stencil_test)
        context->vk.vkCmdSetStencilReference(commands->command, 3, draw->draw.stencil_reference);
    context->vk.vkCmdSetViewport(commands->command, 0, 1, &prepared->viewport);
    context->vk.vkCmdSetScissor(commands->command, 0, 1, &prepared->scissor);
    uint32_t slot = 0;
    while (slot < draw->vertex_buffer_count) {
        if (prepared->buffers[slot] == 0) {
            ++slot;
            continue;
        }
        uint32_t first = slot++;
        while (slot < draw->vertex_buffer_count && prepared->buffers[slot] != 0) ++slot;
        context->vk.vkCmdBindVertexBuffers(commands->command, first, slot - first,
            prepared->buffers + first, prepared->offsets + first);
    }
    const doe_vk_render_indices *indices = draw->indices;
    if (indices != NULL)
        context->vk.vkCmdBindIndexBuffer(commands->command, prepared->index_buffer, indices->offset,
            (uint32_t)indices->format);
    if (prepared->indirect != 0 && indices != NULL)
        context->vk.vkCmdDrawIndexedIndirect(commands->command, prepared->indirect, 0, 1, 20);
    else if (prepared->indirect != 0)
        context->vk.vkCmdDrawIndirect(commands->command, prepared->indirect, 0, 1, 16);
    else if (indices != NULL)
        context->vk.vkCmdDrawIndexed(commands->command, indices->index_count, draw->draw.instance_count,
            indices->first_index, indices->base_vertex, draw->draw.first_instance);
    else context->vk.vkCmdDraw(commands->command, draw->draw.vertex_count, draw->draw.instance_count,
        draw->draw.first_vertex, draw->draw.first_instance);
}

static doe_vk_status validate_query_ops(doe_vk_commands *commands,
    doe_vk_query_pool *pool, const doe_vk_render_pass_op *ops, size_t count) {
    if (pool != NULL && (pool->context != commands->context || pool->kind != DOE_VK_QUERY_OCCLUSION))
        return DOE_VK_INVALID_ARGUMENT;
    if (pool != NULL && pool->content_status != DOE_VK_OK) return pool->content_status;
    uint8_t *written = pool != NULL ? calloc(pool->count, 1) : NULL;
    if (pool != NULL && written == NULL) return DOE_VK_OUT_OF_MEMORY;
    bool active = false;
    doe_vk_status status = DOE_VK_OK;
    for (size_t i = 0; i < count; ++i) {
        if (ops[i].kind == DOE_VK_RENDER_DRAW) continue;
        if (ops[i].kind == DOE_VK_RENDER_BEGIN_OCCLUSION) {
            uint32_t index = ops[i].value.query_index;
            if (pool == NULL || active || index >= pool->count || written[index]) {
                status = DOE_VK_INVALID_ARGUMENT;
                break;
            }
            written[index] = 1;
            active = true;
        } else if (ops[i].kind == DOE_VK_RENDER_END_OCCLUSION && active) active = false;
        else {
            status = DOE_VK_INVALID_ARGUMENT;
            break;
        }
    }
    if (active) status = DOE_VK_INVALID_ARGUMENT;
    free(written);
    return status;
}

doe_vk_status doe_vk_commands_render_pass_full(doe_vk_commands *commands,
    const doe_vk_render_pass_desc *desc, doe_vk_query_pool *occlusion,
    const doe_vk_render_pass_op *ops, size_t count) {
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return status;
    if ((count != 0 && ops == NULL) || count > SIZE_MAX / sizeof(prepared_draw))
        return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    doe_vk_render_colors colors;
    status = doe_vk_render_colors_normalize(desc, &colors);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    uint32_t width = 0, height = 0;
    status = doe_vk_render_attachments_validate(commands, desc, &colors, &width, &height);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    status = validate_query_ops(commands, occlusion, ops, count);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    prepared_draw *prepared = count != 0 ? calloc(count, sizeof(*prepared)) : NULL;
    if (count != 0 && prepared == NULL) return doe_vk_poison(commands, DOE_VK_OUT_OF_MEMORY);
    for (size_t i = 0; i < count; ++i) {
        if (ops[i].kind != DOE_VK_RENDER_DRAW) continue;
        status = validate_pass_draw(commands, desc, &colors, &ops[i].value.draw, width, height, &prepared[i]);
        if (status != DOE_VK_OK) goto failed;
    }
    doe_vk_query_use *queries = NULL;
    for (size_t i = 0; i < count; ++i) {
        if (ops[i].kind == DOE_VK_RENDER_DRAW) {
            status = prepare_draw(commands, &ops[i].value.draw, &prepared[i]);
        } else if (ops[i].kind == DOE_VK_RENDER_BEGIN_OCCLUSION) {
            status = doe_vk_use_queries(commands, occlusion, &queries);
            if (status == DOE_VK_OK) status = doe_vk_commands_reset_queries(commands,
                occlusion, ops[i].value.query_index, 1);
        }
        if (status != DOE_VK_OK) goto failed;
    }
    /* All target bindings are admitted before guards. Copies observe preceding
     * producer commands, and every draw has a different retained scratch record. */
    for (size_t i = 0; i < count; ++i) {
        if (ops[i].kind != DOE_VK_RENDER_DRAW || ops[i].value.draw.indirect == NULL) continue;
        const bool indexed = ops[i].value.draw.indices != NULL;
        const uint32_t limits[2] = {indexed ? prepared[i].index_limit : prepared[i].vertex_limits[0],
            prepared[i].vertex_limits[1]};
        status = doe_vk_draw_indirect_prepare(commands, ops[i].value.draw.indirect,
            limits, indexed, &prepared[i].indirect);
        if (status != DOE_VK_OK) goto failed;
    }
    bool depth_stencil_sampled = false;
    for (size_t i = 0; i < count; ++i)
        if (ops[i].kind == DOE_VK_RENDER_DRAW && prepared[i].samples_depth_stencil) depth_stencil_sampled = true;
    doe_vk_render_use *use = NULL;
    status = doe_vk_render_attachments_prepare(commands, desc, &colors, width, height, depth_stencil_sampled, &use);
    if (status != DOE_VK_OK) goto failed;
    doe_vk_render_attachments_begin(commands, use, desc, &colors, width, height);
    uint32_t active_index = 0;
    doe_vk_context *context = commands->context;
    for (size_t i = 0; i < count; ++i) {
        if (ops[i].kind == DOE_VK_RENDER_DRAW) record_draw(commands, &ops[i].value.draw, &prepared[i]);
        else if (ops[i].kind == DOE_VK_RENDER_BEGIN_OCCLUSION) {
            active_index = ops[i].value.query_index;
            context->vk.vkCmdBeginQuery(commands->command, occlusion->pool, active_index, 0);
        } else {
            context->vk.vkCmdEndQuery(commands->command, occlusion->pool, active_index);
            queries->states[active_index] = DOE_VK_QUERY_WRITTEN;
        }
    }
    context->vk.vkCmdEndRenderPass(commands->command);
    free(prepared);
    /* WebGPU discard leaves zeros. One slice of a 3D mip cannot be marked
     * undefined on its own, so a discarded slice is cleared to zero now. */
    const doe_vk_clear_color zero = {0};
    for (uint32_t i = 0; i < colors.count; ++i) {
        doe_vk_texture_view *view = colors.slots[i].view;
        if (view == NULL || !colors.slots[i].discard || !doe_vk_texture_view_is_slice(view)) continue;
        const doe_vk_render_pass_desc clear = {.color = view, .clear_color = &zero};
        status = doe_vk_commands_render_pass_full(commands, &clear, NULL, NULL, 0);
        if (status != DOE_VK_OK) return status;
    }
    return DOE_VK_OK;
failed:
    free(prepared);
    return doe_vk_poison(commands, status);
}

doe_vk_status doe_vk_commands_render_pass_ops(doe_vk_commands *commands,
    doe_vk_texture_view *target, const doe_vk_clear_color *clear,
    doe_vk_query_pool *occlusion, const doe_vk_render_pass_op *ops, size_t count) {
    const doe_vk_render_pass_desc desc = {.color = target, .clear_color = clear};
    return doe_vk_commands_render_pass_full(commands, &desc, occlusion, ops, count);
}

doe_vk_status doe_vk_commands_render_pass(doe_vk_commands *commands,
    doe_vk_texture_view *target, const doe_vk_clear_color *clear,
    const doe_vk_render_pass_draw *draws, size_t count) {
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return status;
    if ((count != 0 && draws == NULL) || count > SIZE_MAX / sizeof(doe_vk_render_pass_op))
        return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    doe_vk_render_pass_op *ops = count != 0 ? calloc(count, sizeof(*ops)) : NULL;
    if (count != 0 && ops == NULL) return doe_vk_poison(commands, DOE_VK_OUT_OF_MEMORY);
    for (size_t i = 0; i < count; ++i) {
        ops[i].kind = DOE_VK_RENDER_DRAW;
        ops[i].value.draw = draws[i];
    }
    status = doe_vk_commands_render_pass_ops(commands, target, clear, NULL, ops, count);
    free(ops);
    return status;
}

doe_vk_status doe_vk_commands_clear_color(doe_vk_commands *commands,
    doe_vk_texture_view *target, doe_vk_clear_color clear) {
    return doe_vk_commands_render_pass_ops(commands, target, &clear, NULL, NULL, 0);
}

doe_vk_status doe_vk_commands_draw(doe_vk_commands *commands,
    doe_vk_pipeline *pipeline, doe_vk_texture_view *target,
    const doe_vk_binding_resource *bindings, size_t binding_count,
    const doe_vk_buffer_binding *vertices, size_t vertex_count, const doe_vk_render_draw *draw) {
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return status;
    if (draw == NULL) return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    const doe_vk_render_pass_op op = {.kind = DOE_VK_RENDER_DRAW,
        .value.draw = {.pipeline = pipeline, .bindings = bindings, .binding_count = binding_count,
            .vertices = vertices, .vertex_buffer_count = vertex_count, .draw = *draw}};
    return doe_vk_commands_render_pass_ops(commands, target, NULL, NULL, &op, 1);
}

void doe_vk_render_release(doe_vk_commands *commands) {
    doe_vk_context *context = commands->context;
    while (commands->renders != NULL) {
        doe_vk_render_use *use = commands->renders;
        commands->renders = use->next;
        if (use->framebuffer != 0) context->vk.vkDestroyFramebuffer(context->device, use->framebuffer, NULL);
        if (use->pass != 0) context->vk.vkDestroyRenderPass(context->device, use->pass, NULL);
        for (uint32_t i = 0; i < use->color_count; ++i) {
            doe_vk_texture_view_release(use->resolves[i]);
            doe_vk_texture_view_release(use->colors[i]);
        }
        doe_vk_texture_view_release(use->depth_stencil);
        free(use);
    }
}
