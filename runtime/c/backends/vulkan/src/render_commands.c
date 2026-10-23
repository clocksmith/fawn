#include "render_internal.h"
#include "query_internal.h"
#include <math.h>

doe_vk_status doe_vk_render_pass_create(doe_vk_context *context,
    doe_vk_texture_format format, bool clear, VkRenderPass *out, doe_vk_error *error) {
    const VkAttachmentDescription attachment = {
        .format = (uint32_t)format, .samples = 1,
        .loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };
    const VkAttachmentReference reference = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &reference
    };
    const VkRenderPassCreateInfo create = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1, .pAttachments = &attachment,
        .subpassCount = 1, .pSubpasses = &subpass
    };
    return doe_vk_result(context, context->vk.vkCreateRenderPass(context->device, &create, NULL, out), "vkCreateRenderPass", error);
}

static doe_vk_status validate_target(doe_vk_commands *commands, doe_vk_texture_view *target,
    uint32_t *width, uint32_t *height) {
    if (target == NULL || target->texture->context != commands->context ||
        target->desc.dimension != DOE_VK_VIEW_2D || target->desc.mip_count != 1 || target->desc.layer_count != 1 ||
        !(target->texture->desc.usage & DOE_VK_TEXTURE_COLOR_ATTACHMENT)) return DOE_VK_INVALID_ARGUMENT;
    if (!(commands->context->caps.queue_flags & VK_QUEUE_GRAPHICS_BIT)) return DOE_VK_UNSUPPORTED;
    const doe_vk_texture *texture = target->texture;
    uint32_t w = texture->desc.width >> target->desc.base_mip;
    uint32_t h = texture->desc.height >> target->desc.base_mip;
    *width = w != 0 ? w : 1;
    *height = h != 0 ? h : 1;
    const VkPhysicalDeviceLimits *limits = &commands->context->properties.limits;
    if (*width > limits->maxFramebufferWidth || *height > limits->maxFramebufferHeight)
        return DOE_VK_UNSUPPORTED;
    return texture->content_status;
}

static doe_vk_status prepare_pass(doe_vk_commands *commands, doe_vk_texture_view *target,
    bool clear, uint32_t width, uint32_t height, doe_vk_render_use **out) {
    doe_vk_texture_use *texture_use = NULL;
    doe_vk_status status = doe_vk_use_texture(commands, target->texture, &texture_use);
    if (status != DOE_VK_OK) return status;
    size_t index = (size_t)target->desc.base_mip * target->texture->desc.depth_or_layers + target->desc.base_layer;
    if (!clear && !texture_use->initialized[index]) return DOE_VK_INVALID_STATE;
    doe_vk_render_use *use = calloc(1, sizeof(*use));
    if (use == NULL) return DOE_VK_OUT_OF_MEMORY;
    status = doe_vk_texture_view_retain(target);
    if (status != DOE_VK_OK) {
        free(use);
        return status;
    }
    use->target = target;
    use->next = commands->renders;
    commands->renders = use;
    doe_vk_context *context = commands->context;
    status = doe_vk_render_pass_create(context, target->texture->desc.format, clear, &use->pass, NULL);
    if (status != DOE_VK_OK) return status;
    const VkFramebufferCreateInfo framebuffer = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = use->pass,
        .attachmentCount = 1, .pAttachments = &target->view, .width = width, .height = height, .layers = 1
    };
    status = doe_vk_result(context, context->vk.vkCreateFramebuffer(context->device,
        &framebuffer, NULL, &use->framebuffer), "vkCreateFramebuffer", NULL);
    if (status != DOE_VK_OK) return status;
    doe_vk_barrier(commands);
    doe_vk_texture_transition(commands, texture_use, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    if (clear) texture_use->initialized[index] = 1;
    *out = use;
    return DOE_VK_OK;
}

static void begin_pass(doe_vk_commands *commands, const doe_vk_render_use *use,
    uint32_t width, uint32_t height, const doe_vk_clear_color *clear) {
    VkClearValue value = {0};
    _Static_assert(sizeof(value.color) == sizeof(*clear), "clear color union ABI");
    if (clear != NULL) memcpy(&value.color, clear, sizeof(*clear));
    const VkRenderPassBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = use->pass, .framebuffer = use->framebuffer,
        .renderArea = { .extent = {width, height} }, .clearValueCount = clear != NULL ? 1 : 0,
        .pClearValues = clear != NULL ? &value : NULL
    };
    commands->context->vk.vkCmdBeginRenderPass(commands->command, &begin, VK_SUBPASS_CONTENTS_INLINE);
}

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

static doe_vk_status validate_pass_draw(doe_vk_commands *commands, doe_vk_texture_view *target,
    const doe_vk_render_pass_draw *draw, uint32_t width, uint32_t height, prepared_draw *prepared) {
    doe_vk_pipeline *pipeline = draw->pipeline;
    if (pipeline == NULL || !pipeline->graphics || pipeline->context != commands->context ||
        draw->binding_count != pipeline->binding_count ||
        (draw->binding_count != 0 && draw->bindings == NULL) ||
        target->texture->desc.format != pipeline->color_format) return DOE_VK_INVALID_ARGUMENT;
    doe_vk_status status = doe_vk_render_viewport(commands->context, &draw->draw, width, height, &prepared->viewport);
    if (status != DOE_VK_OK) return status;
    status = validate_draw(commands, pipeline, draw->vertices, draw->vertex_buffer_count, &draw->draw);
    if (status != DOE_VK_OK) return status;
    for (size_t i = 0; i < draw->binding_count; ++i) {
        if ((draw->bindings[i].kind == DOE_VK_BINDING_SAMPLED_TEXTURE ||
            draw->bindings[i].kind == DOE_VK_BINDING_STORAGE_TEXTURE) &&
            draw->bindings[i].value.view != NULL && draw->bindings[i].value.view->texture == target->texture)
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
    context->vk.vkCmdSetViewport(commands->command, 0, 1, &prepared->viewport);
    context->vk.vkCmdSetScissor(commands->command, 0, 1, &prepared->scissor);
    if (draw->vertex_buffer_count != 0) context->vk.vkCmdBindVertexBuffers(commands->command, 0,
        (uint32_t)draw->vertex_buffer_count, prepared->buffers, prepared->offsets);
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

doe_vk_status doe_vk_commands_render_pass_ops(doe_vk_commands *commands,
    doe_vk_texture_view *target, const doe_vk_clear_color *clear,
    doe_vk_query_pool *occlusion, const doe_vk_render_pass_op *ops, size_t count) {
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return status;
    if ((count != 0 && ops == NULL) || count > SIZE_MAX / sizeof(prepared_draw))
        return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    uint32_t width = 0, height = 0;
    status = validate_target(commands, target, &width, &height);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    status = validate_query_ops(commands, occlusion, ops, count);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    prepared_draw *prepared = count != 0 ? calloc(count, sizeof(*prepared)) : NULL;
    if (count != 0 && prepared == NULL) return doe_vk_poison(commands, DOE_VK_OUT_OF_MEMORY);
    for (size_t i = 0; i < count; ++i) {
        if (ops[i].kind != DOE_VK_RENDER_DRAW) continue;
        status = validate_pass_draw(commands, target, &ops[i].value.draw, width, height, &prepared[i]);
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
    status = prepare_pass(commands, target, clear != NULL, width, height, &use);
    if (status != DOE_VK_OK) goto failed;
    begin_pass(commands, use, width, height, clear);
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
        doe_vk_texture_view_release(use->target);
        free(use);
    }
}
