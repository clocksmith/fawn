#include "render_internal.h"
#include <math.h>

static uint32_t load_operation(bool present, bool clear) {
    if (!present) return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    return clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
}
static uint32_t store_operation(bool present, bool discard) {
    return present && !discard ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

doe_vk_status doe_vk_render_pass_create(doe_vk_context *context,
    doe_vk_texture_format color, doe_vk_texture_format depth_stencil,
    const doe_vk_render_pass_desc *desc, VkRenderPass *out, doe_vk_error *error) {
    const doe_vk_render_pass_desc defaults = {0};
    if (desc == NULL) desc = &defaults;
    uint32_t aspects = depth_stencil != 0 ? doe_vk_format_aspects(depth_stencil) : 0;
    VkAttachmentDescription attachments[2] = {
        {.format = (uint32_t)color, .samples = 1,
            .loadOp = load_operation(true, desc->clear_color != NULL),
            .storeOp = store_operation(true, desc->discard_color),
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        {.format = (uint32_t)depth_stencil, .samples = 1,
            .loadOp = load_operation((aspects & VK_IMAGE_ASPECT_DEPTH_BIT) != 0, desc->clear_depth),
            .storeOp = store_operation((aspects & VK_IMAGE_ASPECT_DEPTH_BIT) != 0, desc->discard_depth),
            .stencilLoadOp = load_operation((aspects & VK_IMAGE_ASPECT_STENCIL_BIT) != 0, desc->clear_stencil),
            .stencilStoreOp = store_operation((aspects & VK_IMAGE_ASPECT_STENCIL_BIT) != 0, desc->discard_stencil),
            .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}
    };
    const VkAttachmentReference color_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkAttachmentReference depth_ref = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    const VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1, .pColorAttachments = &color_ref,
        .pDepthStencilAttachment = depth_stencil != 0 ? &depth_ref : NULL};
    const VkRenderPassCreateInfo create = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = depth_stencil != 0 ? 2 : 1, .pAttachments = attachments,
        .subpassCount = 1, .pSubpasses = &subpass};
    return doe_vk_result(context, context->vk.vkCreateRenderPass(context->device, &create, NULL, out),
        "vkCreateRenderPass", error);
}

static doe_vk_status validate_view(doe_vk_commands *commands, doe_vk_texture_view *view,
    uint32_t usage, uint32_t *width, uint32_t *height) {
    if (view == NULL || view->texture->context != commands->context ||
        view->desc.dimension != DOE_VK_VIEW_2D || view->desc.mip_count != 1 || view->desc.layer_count != 1 ||
        !(view->texture->desc.usage & usage)) return DOE_VK_INVALID_ARGUMENT;
    const doe_vk_texture *texture = view->texture;
    uint32_t w = texture->desc.width >> view->desc.base_mip;
    uint32_t h = texture->desc.height >> view->desc.base_mip;
    *width = w != 0 ? w : 1;
    *height = h != 0 ? h : 1;
    const VkPhysicalDeviceLimits *limits = &commands->context->properties.limits;
    if (*width > limits->maxFramebufferWidth || *height > limits->maxFramebufferHeight)
        return DOE_VK_UNSUPPORTED;
    return texture->content_status;
}

doe_vk_status doe_vk_render_attachments_validate(doe_vk_commands *commands,
    const doe_vk_render_pass_desc *desc, uint32_t *width, uint32_t *height) {
    if (desc == NULL) return DOE_VK_INVALID_ARGUMENT;
    if (!(commands->context->caps.queue_flags & VK_QUEUE_GRAPHICS_BIT)) return DOE_VK_UNSUPPORTED;
    doe_vk_status status = validate_view(commands, desc->color, DOE_VK_TEXTURE_COLOR_ATTACHMENT, width, height);
    if (status != DOE_VK_OK) return status;
    uint32_t aspects = 0;
    if (desc->depth_stencil != NULL) {
        uint32_t depth_width, depth_height;
        status = validate_view(commands, desc->depth_stencil, DOE_VK_TEXTURE_DEPTH_STENCIL_ATTACHMENT,
            &depth_width, &depth_height);
        if (status != DOE_VK_OK) return status;
        if (*width != depth_width || *height != depth_height) return DOE_VK_INVALID_ARGUMENT;
        aspects = doe_vk_format_aspects(desc->depth_stencil->texture->desc.format);
        if (aspects == VK_IMAGE_ASPECT_COLOR_BIT) return DOE_VK_INVALID_ARGUMENT;
    }
    if (!(aspects & VK_IMAGE_ASPECT_DEPTH_BIT) &&
        (desc->clear_depth || desc->discard_depth || desc->depth_read_only)) return DOE_VK_INVALID_ARGUMENT;
    if (!(aspects & VK_IMAGE_ASPECT_STENCIL_BIT) &&
        (desc->clear_stencil || desc->discard_stencil || desc->stencil_read_only)) return DOE_VK_INVALID_ARGUMENT;
    if (desc->depth_read_only && (desc->clear_depth || desc->discard_depth)) return DOE_VK_INVALID_ARGUMENT;
    if (desc->stencil_read_only && (desc->clear_stencil || desc->discard_stencil)) return DOE_VK_INVALID_ARGUMENT;
    if (desc->clear_depth && (!isfinite(desc->depth_clear_value) ||
        desc->depth_clear_value < 0 || desc->depth_clear_value > 1)) return DOE_VK_INVALID_ARGUMENT;
    return DOE_VK_OK;
}

static size_t subresource_index(const doe_vk_texture_view *view) {
    return (size_t)view->desc.base_mip * view->texture->desc.depth_or_layers + view->desc.base_layer;
}

doe_vk_status doe_vk_render_attachments_prepare(doe_vk_commands *commands,
    const doe_vk_render_pass_desc *desc, uint32_t width, uint32_t height, doe_vk_render_use **out) {
    doe_vk_texture_use *color = NULL, *depth = NULL;
    doe_vk_status status = doe_vk_use_texture(commands, desc->color->texture, &color);
    if (status != DOE_VK_OK) return status;
    size_t color_index = subresource_index(desc->color);
    if (desc->clear_color == NULL && !(color->initialized[color_index] & VK_IMAGE_ASPECT_COLOR_BIT))
        return DOE_VK_INVALID_STATE;
    uint32_t aspects = 0, cleared = 0;
    size_t depth_index = 0;
    if (desc->depth_stencil != NULL) {
        status = doe_vk_use_texture(commands, desc->depth_stencil->texture, &depth);
        if (status != DOE_VK_OK) return status;
        depth_index = subresource_index(desc->depth_stencil);
        aspects = doe_vk_format_aspects(depth->texture->desc.format);
        cleared = (desc->clear_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : 0) |
            (desc->clear_stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
        uint32_t loaded = aspects & ~cleared;
        if ((depth->initialized[depth_index] & loaded) != loaded) return DOE_VK_INVALID_STATE;
    }
    doe_vk_render_use *use = calloc(1, sizeof(*use));
    if (use == NULL) return DOE_VK_OUT_OF_MEMORY;
    status = doe_vk_texture_view_retain(desc->color);
    if (status != DOE_VK_OK) {
        free(use);
        return status;
    }
    use->target = desc->color;
    use->next = commands->renders;
    commands->renders = use;
    if (desc->depth_stencil != NULL) {
        status = doe_vk_texture_view_retain(desc->depth_stencil);
        if (status != DOE_VK_OK) return status;
        use->depth_stencil = desc->depth_stencil;
    }
    doe_vk_context *context = commands->context;
    status = doe_vk_render_pass_create(context, desc->color->texture->desc.format,
        depth != NULL ? depth->texture->desc.format : 0, desc, &use->pass, NULL);
    if (status != DOE_VK_OK) return status;
    const VkImageView views[2] = {desc->color->view, use->depth_stencil != NULL ? use->depth_stencil->view : 0};
    const VkFramebufferCreateInfo framebuffer = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = use->pass, .attachmentCount = depth != NULL ? 2 : 1, .pAttachments = views,
        .width = width, .height = height, .layers = 1};
    status = doe_vk_result(context, context->vk.vkCreateFramebuffer(context->device,
        &framebuffer, NULL, &use->framebuffer), "vkCreateFramebuffer", NULL);
    if (status != DOE_VK_OK) return status;
    doe_vk_barrier(commands);
    doe_vk_texture_transition(commands, color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    if (depth != NULL) doe_vk_texture_transition(commands, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    /* These are speculative final aspect states, published only on successful
     * command completion. Discard cannot become initialized through a draw. */
    color->initialized[color_index] = desc->discard_color ? 0 : VK_IMAGE_ASPECT_COLOR_BIT;
    if (depth != NULL) {
        uint32_t discarded = (desc->discard_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : 0) |
            (desc->discard_stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
        depth->initialized[depth_index] = (uint8_t)(aspects & ~discarded);
    }
    *out = use;
    return DOE_VK_OK;
}

void doe_vk_render_attachments_begin(doe_vk_commands *commands, const doe_vk_render_use *use,
    const doe_vk_render_pass_desc *desc, uint32_t width, uint32_t height) {
    VkClearValue values[2] = {0};
    _Static_assert(sizeof(values[0].color) == sizeof(*desc->clear_color), "clear color union ABI");
    if (desc->clear_color != NULL) memcpy(&values[0].color, desc->clear_color, sizeof(*desc->clear_color));
    values[1].depthStencil = (VkClearDepthStencilValue){desc->depth_clear_value, desc->stencil_clear_value};
    uint32_t count = desc->clear_depth || desc->clear_stencil ? 2 : (desc->clear_color != NULL ? 1 : 0);
    const VkRenderPassBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = use->pass, .framebuffer = use->framebuffer,
        .renderArea = {.extent = {width, height}}, .clearValueCount = count,
        .pClearValues = count != 0 ? values : NULL};
    commands->context->vk.vkCmdBeginRenderPass(commands->command, &begin, VK_SUBPASS_CONTENTS_INLINE);
}
