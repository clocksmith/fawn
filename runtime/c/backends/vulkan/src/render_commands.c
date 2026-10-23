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
    if (v[0] < 0 || v[1] < 0 || v[2] <= 0 || v[3] <= 0 || v[4] < 0 || v[4] > 1 || v[5] < 0 || v[5] > 1 ||
        (double)v[0] + v[2] > width || (double)v[1] + v[3] > height ||
        v[2] > limits->maxViewportDimensions[0] || v[3] > limits->maxViewportDimensions[1] ||
        v[0] < limits->viewportBoundsRange[0] || v[1] < limits->viewportBoundsRange[0] ||
        (double)v[0] + v[2] > limits->viewportBoundsRange[1] ||
        (double)v[1] + v[3] > limits->viewportBoundsRange[1] ||
        draw->scissor[0] > INT32_MAX || draw->scissor[1] > INT32_MAX ||
        !doe_vk_range(width, draw->scissor[0], draw->scissor[2]) ||
        !doe_vk_range(height, draw->scissor[1], draw->scissor[3])) return DOE_VK_INVALID_ARGUMENT;
    VkViewport viewport = {v[0], v[1], v[2], v[3], v[4], v[5]};
    if (draw->viewport_convention == DOE_VK_VIEWPORT_WEBGPU) {
        viewport.y = v[1] + v[3];
        viewport.height = -v[3];
        if (!isfinite(viewport.y) || viewport.y > limits->viewportBoundsRange[1])
            return DOE_VK_INVALID_ARGUMENT;
    }
    *out = viewport;
    return DOE_VK_OK;
}

static doe_vk_status validate_draw(doe_vk_commands *commands, doe_vk_pipeline *pipeline,
    const doe_vk_buffer_binding *vertices, size_t vertex_count, const doe_vk_render_draw *draw) {
    if (vertex_count != pipeline->vertex_count || (vertex_count != 0 && vertices == NULL))
        return DOE_VK_INVALID_ARGUMENT;
    if ((uint64_t)draw->first_vertex + draw->vertex_count > UINT32_MAX ||
        (uint64_t)draw->first_instance + draw->instance_count > UINT32_MAX) return DOE_VK_INVALID_ARGUMENT;
    for (size_t i = 0; i < vertex_count; ++i) {
        const doe_vk_buffer_binding *binding = &vertices[i];
        if (binding->buffer == NULL && pipeline->vertex_attribute_end[i] == 0 &&
            binding->offset == 0 && binding->size == 0) continue;
        if (binding->buffer == NULL || binding->buffer->context != commands->context ||
            !(binding->buffer->usage & DOE_VK_BUFFER_VERTEX) || binding->offset % 4 != 0 ||
            !doe_vk_range(binding->buffer->size, binding->offset, binding->size)) return DOE_VK_INVALID_ARGUMENT;
        if (binding->buffer->content_status != DOE_VK_OK) return binding->buffer->content_status;
        if (draw->vertex_count == 0 || draw->instance_count == 0 || pipeline->vertex_attribute_end[i] == 0) continue;
        uint64_t last = pipeline->vertices[i].per_instance ?
            (uint64_t)draw->first_instance + draw->instance_count - 1 : (uint64_t)draw->first_vertex + draw->vertex_count - 1;
        uint64_t offset = last * pipeline->vertices[i].stride;
        if (!doe_vk_range(binding->size, offset, pipeline->vertex_attribute_end[i])) return DOE_VK_INVALID_ARGUMENT;
    }
    return DOE_VK_OK;
}

typedef struct prepared_draw {
    doe_vk_dispatch_use *bindings;
    VkViewport viewport;
    VkRect2D scissor;
    VkBuffer buffers[DOE_VK_MAX_VERTEX_BUFFERS];
    VkDeviceSize offsets[DOE_VK_MAX_VERTEX_BUFFERS];
} prepared_draw;

static doe_vk_status validate_pass_draw(doe_vk_commands *commands, const doe_vk_render_pass_desc *desc,
    const doe_vk_render_pass_draw *draw, uint32_t width, uint32_t height, prepared_draw *prepared) {
    doe_vk_pipeline *pipeline = draw->pipeline;
    doe_vk_texture_view *target = desc->color;
    if (pipeline == NULL || !pipeline->graphics || pipeline->context != commands->context ||
        draw->binding_count != pipeline->binding_count ||
        (draw->binding_count != 0 && draw->bindings == NULL) ||
        target->texture->desc.format != pipeline->color_format ||
        (desc->depth_stencil != NULL ? desc->depth_stencil->texture->desc.format : 0) != pipeline->depth_stencil_format ||
        (desc->depth_read_only && pipeline->depth_write) ||
        (desc->stencil_read_only && pipeline->stencil_write)) return DOE_VK_INVALID_ARGUMENT;
    if (draw->draw.override_blend_constant) {
        for (unsigned i = 0; i < 4; ++i)
            if (!isfinite(draw->draw.blend_constant[i])) return DOE_VK_INVALID_ARGUMENT;
    }
    doe_vk_status status = doe_vk_render_viewport(commands->context, &draw->draw, width, height, &prepared->viewport);
    if (status != DOE_VK_OK) return status;
    status = validate_draw(commands, pipeline, draw->vertices, draw->vertex_buffer_count, &draw->draw);
    if (status != DOE_VK_OK) return status;
    for (size_t i = 0; i < draw->binding_count; ++i) {
        if ((draw->bindings[i].kind == DOE_VK_BINDING_SAMPLED_TEXTURE ||
            draw->bindings[i].kind == DOE_VK_BINDING_STORAGE_TEXTURE) &&
            draw->bindings[i].value.view != NULL && (draw->bindings[i].value.view->texture == target->texture ||
                (desc->depth_stencil != NULL && draw->bindings[i].value.view->texture == desc->depth_stencil->texture)))
            return DOE_VK_UNSUPPORTED;
    }
    prepared->scissor = (VkRect2D){
        .offset = {(int32_t)draw->draw.scissor[0], (int32_t)draw->draw.scissor[1]},
        .extent = {draw->draw.scissor[2], draw->draw.scissor[3]}
    };
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
    context->vk.vkCmdDraw(commands->command, draw->draw.vertex_count, draw->draw.instance_count,
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
    uint32_t width = 0, height = 0;
    status = doe_vk_render_attachments_validate(commands, desc, &width, &height);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    status = validate_query_ops(commands, occlusion, ops, count);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    prepared_draw *prepared = count != 0 ? calloc(count, sizeof(*prepared)) : NULL;
    if (count != 0 && prepared == NULL) return doe_vk_poison(commands, DOE_VK_OUT_OF_MEMORY);
    for (size_t i = 0; i < count; ++i) {
        if (ops[i].kind != DOE_VK_RENDER_DRAW) continue;
        status = validate_pass_draw(commands, desc, &ops[i].value.draw, width, height, &prepared[i]);
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
    doe_vk_render_use *use = NULL;
    status = doe_vk_render_attachments_prepare(commands, desc, width, height, &use);
    if (status != DOE_VK_OK) goto failed;
    doe_vk_render_attachments_begin(commands, use, desc, width, height);
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
        .value.draw = {pipeline, bindings, binding_count, vertices, vertex_count, *draw}};
    return doe_vk_commands_render_pass_ops(commands, target, NULL, NULL, &op, 1);
}

void doe_vk_render_release(doe_vk_commands *commands) {
    doe_vk_context *context = commands->context;
    while (commands->renders != NULL) {
        doe_vk_render_use *use = commands->renders;
        commands->renders = use->next;
        if (use->framebuffer != 0) context->vk.vkDestroyFramebuffer(context->device, use->framebuffer, NULL);
        if (use->pass != 0) context->vk.vkDestroyRenderPass(context->device, use->pass, NULL);
        doe_vk_texture_view_release(use->depth_stencil);
        doe_vk_texture_view_release(use->target);
        free(use);
    }
}
