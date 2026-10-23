#include "render_internal.h"
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

doe_vk_status doe_vk_commands_clear_color(doe_vk_commands *commands,
    doe_vk_texture_view *target, doe_vk_clear_color clear) {
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return status;
    uint32_t width = 0, height = 0;
    status = validate_target(commands, target, &width, &height);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    doe_vk_render_use *use = NULL;
    status = prepare_pass(commands, target, true, width, height, &use);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    begin_pass(commands, use, width, height, &clear);
    commands->context->vk.vkCmdEndRenderPass(commands->command);
    return DOE_VK_OK;
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

doe_vk_status doe_vk_commands_draw(doe_vk_commands *commands,
    doe_vk_pipeline *pipeline, doe_vk_texture_view *target,
    const doe_vk_binding_resource *bindings, size_t binding_count,
    const doe_vk_buffer_binding *vertices, size_t vertex_count, const doe_vk_render_draw *draw) {
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return status;
    if (pipeline == NULL || !pipeline->graphics || pipeline->context != commands->context ||
        binding_count != pipeline->binding_count || (binding_count != 0 && bindings == NULL))
        return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    uint32_t width = 0, height = 0;
    status = validate_target(commands, target, &width, &height);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    if (target->texture->desc.format != pipeline->color_format) return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    VkViewport viewport;
    status = doe_vk_render_viewport(commands->context, draw, width, height, &viewport);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    status = validate_draw(commands, pipeline, vertices, vertex_count, draw);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    for (size_t i = 0; i < binding_count; ++i) {
        if ((bindings[i].kind == DOE_VK_BINDING_SAMPLED_TEXTURE || bindings[i].kind == DOE_VK_BINDING_STORAGE_TEXTURE) &&
            bindings[i].value.view != NULL && bindings[i].value.view->texture == target->texture)
            return doe_vk_poison(commands, DOE_VK_UNSUPPORTED);
    }
    status = doe_vk_bind_resources(commands, pipeline, bindings, binding_count);
    if (status != DOE_VK_OK) return status;
    VkBuffer native_buffers[DOE_VK_MAX_VERTEX_BUFFERS];
    VkDeviceSize offsets[DOE_VK_MAX_VERTEX_BUFFERS];
    for (size_t i = 0; i < vertex_count; ++i) {
        status = doe_vk_use_buffer(commands, vertices[i].buffer);
        if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
        native_buffers[i] = vertices[i].buffer->buffer;
        offsets[i] = vertices[i].offset;
    }
    doe_vk_render_use *use = NULL;
    status = prepare_pass(commands, target, false, width, height, &use);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    begin_pass(commands, use, width, height, NULL);
    const VkRect2D scissor = { .offset = {(int32_t)draw->scissor[0], (int32_t)draw->scissor[1]},
        .extent = {draw->scissor[2], draw->scissor[3]} };
    doe_vk_context *context = commands->context;
    context->vk.vkCmdSetViewport(commands->command, 0, 1, &viewport);
    context->vk.vkCmdSetScissor(commands->command, 0, 1, &scissor);
    if (vertex_count != 0) context->vk.vkCmdBindVertexBuffers(commands->command, 0,
        (uint32_t)vertex_count, native_buffers, offsets);
    context->vk.vkCmdDraw(commands->command, draw->vertex_count, draw->instance_count, draw->first_vertex, draw->first_instance);
    context->vk.vkCmdEndRenderPass(commands->command);
    return DOE_VK_OK;
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
