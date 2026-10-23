/* SPDX-License-Identifier: MIT */
#include "render_internal.h"
#include <math.h>

static uint32_t load_operation(bool present, bool clear) {
    if (!present) return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    return clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
}
static uint32_t store_operation(bool present, bool discard) {
    return present && !discard ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

doe_vk_status doe_vk_render_colors_normalize(const doe_vk_render_pass_desc *desc, doe_vk_render_colors *out) {
    if (desc == NULL) return DOE_VK_INVALID_ARGUMENT;
    doe_vk_render_colors colors = {0};
    const bool legacy = desc->color != NULL || desc->clear_color != NULL || desc->discard_color ||
        desc->resolve_target != NULL;
    if (desc->color_attachment_count != 0) {
        if (legacy || desc->color_attachment_count > DOE_VK_MAX_COLOR_ATTACHMENTS) return DOE_VK_INVALID_ARGUMENT;
        colors.count = desc->color_attachment_count;
        memcpy(colors.slots, desc->color_attachments, colors.count * sizeof(*colors.slots));
    } else if (legacy) {
        if (desc->color == NULL) return DOE_VK_INVALID_ARGUMENT;
        colors.count = 1;
        colors.slots[0] = (doe_vk_color_attachment){.view = desc->color, .clear = desc->clear_color != NULL,
            .discard = desc->discard_color, .resolve_target = desc->resolve_target};
        if (desc->clear_color != NULL) colors.slots[0].clear_value = *desc->clear_color;
    }
    for (uint32_t i = 0; i < colors.count; ++i) {
        const doe_vk_color_attachment *slot = &colors.slots[i];
        if (slot->view == NULL && (slot->clear || slot->discard || slot->resolve_target != NULL))
            return DOE_VK_INVALID_ARGUMENT;
    }
    *out = colors;
    return DOE_VK_OK;
}

/* Attachment order: used colors by slot, depth/stencil, then resolves by slot. */
doe_vk_status doe_vk_render_pass_create(doe_vk_context *context, const doe_vk_render_layout *layout,
    const doe_vk_render_colors *colors, const doe_vk_render_pass_desc *desc, VkRenderPass *out, doe_vk_error *error) {
    const doe_vk_render_pass_desc defaults = {0};
    if (desc == NULL) desc = &defaults;
    if (layout->color_count > DOE_VK_MAX_COLOR_ATTACHMENTS)
        return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "render pass color slots", error);
    VkAttachmentDescription attachments[2 * DOE_VK_MAX_COLOR_ATTACHMENTS + 1];
    VkAttachmentReference color_refs[DOE_VK_MAX_COLOR_ATTACHMENTS], resolve_refs[DOE_VK_MAX_COLOR_ATTACHMENTS];
    uint32_t count = 0;
    bool resolving = false;
    for (uint32_t i = 0; i < layout->color_count; ++i) {
        color_refs[i] = (VkAttachmentReference){VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED};
        if (layout->colors[i] == 0) continue;
        const doe_vk_color_attachment *slot = colors != NULL ? &colors->slots[i] : NULL;
        attachments[count] = (VkAttachmentDescription){.format = (uint32_t)layout->colors[i],
            .samples = layout->samples, .loadOp = load_operation(true, slot != NULL && slot->clear),
            .storeOp = store_operation(true, slot != NULL && slot->discard),
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        color_refs[i] = (VkAttachmentReference){count++, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    }
    VkAttachmentReference depth_ref = {VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED};
    if (layout->depth_stencil != 0) {
        uint32_t aspects = doe_vk_format_aspects(layout->depth_stencil);
        attachments[count] = (VkAttachmentDescription){.format = (uint32_t)layout->depth_stencil,
            .samples = layout->samples,
            .loadOp = load_operation((aspects & VK_IMAGE_ASPECT_DEPTH_BIT) != 0, desc->clear_depth),
            .storeOp = store_operation((aspects & VK_IMAGE_ASPECT_DEPTH_BIT) != 0, desc->discard_depth),
            .stencilLoadOp = load_operation((aspects & VK_IMAGE_ASPECT_STENCIL_BIT) != 0, desc->clear_stencil),
            .stencilStoreOp = store_operation((aspects & VK_IMAGE_ASPECT_STENCIL_BIT) != 0, desc->discard_stencil),
            .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        depth_ref = (VkAttachmentReference){count++, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    }
    for (uint32_t i = 0; i < layout->color_count; ++i) {
        resolve_refs[i] = (VkAttachmentReference){VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED};
        if (colors == NULL || colors->slots[i].resolve_target == NULL) continue;
        attachments[count] = (VkAttachmentDescription){.format = (uint32_t)layout->colors[i], .samples = 1,
            .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        resolve_refs[i] = (VkAttachmentReference){count++, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        resolving = true;
    }
    const VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = layout->color_count, .pColorAttachments = layout->color_count != 0 ? color_refs : NULL,
        .pDepthStencilAttachment = layout->depth_stencil != 0 ? &depth_ref : NULL,
        .pResolveAttachments = resolving ? resolve_refs : NULL};
    const VkRenderPassCreateInfo create = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = count, .pAttachments = count != 0 ? attachments : NULL,
        .subpassCount = 1, .pSubpasses = &subpass};
    return doe_vk_result(context, context->vk.vkCreateRenderPass(context->device, &create, NULL, out),
        "vkCreateRenderPass", error);
}

static doe_vk_status validate_view(doe_vk_commands *commands, doe_vk_texture_view *view,
    uint32_t usage, uint32_t *width, uint32_t *height) {
    if (view == NULL || view->texture->context != commands->context ||
        view->desc.dimension != DOE_VK_VIEW_2D || view->desc.mip_count != 1 || view->desc.layer_count != 1 ||
        view->aspects != doe_vk_format_aspects(view->texture->desc.format) ||
        doe_vk_texture_view_swizzled(view) || !(view->texture->desc.usage & usage)) return DOE_VK_INVALID_ARGUMENT;
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

static bool same_subresource(const doe_vk_texture_view *a, const doe_vk_texture_view *b) {
    return a != NULL && b != NULL && a->texture == b->texture && a->desc.base_mip == b->desc.base_mip &&
        a->desc.base_layer == b->desc.base_layer;
}

doe_vk_status doe_vk_render_attachments_validate(doe_vk_commands *commands,
    const doe_vk_render_pass_desc *desc, const doe_vk_render_colors *colors, uint32_t *width, uint32_t *height) {
    if (!(commands->context->caps.queue_flags & VK_QUEUE_GRAPHICS_BIT)) return DOE_VK_UNSUPPORTED;
    if (colors->count > commands->context->caps.max_color_attachments) return DOE_VK_UNSUPPORTED;
    doe_vk_status status = DOE_VK_OK;
    bool sized = false;
    uint32_t samples = 0;
    for (uint32_t i = 0; i < colors->count; ++i) {
        const doe_vk_color_attachment *slot = &colors->slots[i];
        if (slot->view == NULL) continue;
        uint32_t slot_width, slot_height;
        status = validate_view(commands, slot->view, DOE_VK_TEXTURE_COLOR_ATTACHMENT, &slot_width, &slot_height);
        if (status != DOE_VK_OK) return status;
        uint32_t slot_samples = doe_vk_samples(slot->view->texture->desc.sample_count);
        if (sized && (slot_width != *width || slot_height != *height || slot_samples != samples))
            return DOE_VK_INVALID_ARGUMENT;
        *width = slot_width;
        *height = slot_height;
        samples = slot_samples;
        sized = true;
        for (uint32_t j = 0; j < i; ++j) {
            if (same_subresource(colors->slots[j].view, slot->view) ||
                same_subresource(colors->slots[j].resolve_target, slot->view)) return DOE_VK_INVALID_ARGUMENT;
        }
        if (slot->resolve_target != NULL) {
            const doe_vk_texture *resolve = slot->resolve_target->texture;
            if (samples == 1 || doe_vk_samples(resolve->desc.sample_count) != 1 ||
                resolve == slot->view->texture || slot->resolve_target->format != slot->view->format)
                return DOE_VK_INVALID_ARGUMENT;
            for (uint32_t j = 0; j < colors->count; ++j) {
                if (j != i && (same_subresource(colors->slots[j].view, slot->resolve_target) ||
                    same_subresource(colors->slots[j].resolve_target, slot->resolve_target)))
                    return DOE_VK_INVALID_ARGUMENT;
            }
            uint32_t resolve_width, resolve_height;
            status = validate_view(commands, slot->resolve_target, DOE_VK_TEXTURE_COLOR_ATTACHMENT,
                &resolve_width, &resolve_height);
            if (status != DOE_VK_OK) return status;
            if (*width != resolve_width || *height != resolve_height) return DOE_VK_INVALID_ARGUMENT;
        }
    }
    if (!sized && desc->depth_stencil == NULL) return DOE_VK_INVALID_ARGUMENT;
    uint32_t aspects = 0;
    if (desc->depth_stencil != NULL) {
        uint32_t depth_width, depth_height;
        status = validate_view(commands, desc->depth_stencil, DOE_VK_TEXTURE_DEPTH_STENCIL_ATTACHMENT,
            &depth_width, &depth_height);
        if (status != DOE_VK_OK) return status;
        if (sized && (*width != depth_width || *height != depth_height ||
            samples != doe_vk_samples(desc->depth_stencil->texture->desc.sample_count))) return DOE_VK_INVALID_ARGUMENT;
        *width = depth_width;
        *height = depth_height;
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

doe_vk_status doe_vk_render_attachments_prepare(doe_vk_commands *commands, const doe_vk_render_pass_desc *desc,
    const doe_vk_render_colors *colors, uint32_t width, uint32_t height, doe_vk_render_use **out) {
    doe_vk_texture_use *color[DOE_VK_MAX_COLOR_ATTACHMENTS] = {0}, *resolve[DOE_VK_MAX_COLOR_ATTACHMENTS] = {0};
    doe_vk_texture_use *depth = NULL;
    doe_vk_status status = DOE_VK_OK;
    doe_vk_render_layout layout = {.color_count = colors->count};
    for (uint32_t i = 0; i < colors->count; ++i) {
        const doe_vk_color_attachment *slot = &colors->slots[i];
        if (slot->view == NULL) continue;
        status = doe_vk_use_texture(commands, slot->view->texture, &color[i]);
        if (status != DOE_VK_OK) return status;
        if (!slot->clear && !(color[i]->initialized[subresource_index(slot->view)] & VK_IMAGE_ASPECT_COLOR_BIT))
            return DOE_VK_INVALID_STATE;
        layout.colors[i] = slot->view->format; /* The view format, possibly a reinterpreted pair. */
        layout.samples = doe_vk_samples(slot->view->texture->desc.sample_count);
        if (slot->resolve_target != NULL) {
            status = doe_vk_use_texture(commands, slot->resolve_target->texture, &resolve[i]);
            if (status != DOE_VK_OK) return status;
        }
    }
    uint32_t aspects = 0;
    size_t depth_index = 0;
    if (desc->depth_stencil != NULL) {
        status = doe_vk_use_texture(commands, desc->depth_stencil->texture, &depth);
        if (status != DOE_VK_OK) return status;
        depth_index = subresource_index(desc->depth_stencil);
        aspects = doe_vk_format_aspects(depth->texture->desc.format);
        uint32_t cleared = (desc->clear_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : 0) |
            (desc->clear_stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
        uint32_t loaded = aspects & ~cleared;
        if ((depth->initialized[depth_index] & loaded) != loaded) return DOE_VK_INVALID_STATE;
        layout.depth_stencil = depth->texture->desc.format;
        layout.samples = doe_vk_samples(depth->texture->desc.sample_count);
    }
    doe_vk_render_use *use = calloc(1, sizeof(*use));
    if (use == NULL) return DOE_VK_OUT_OF_MEMORY;
    use->next = commands->renders;
    commands->renders = use;
    use->color_count = colors->count;
    /* Each retained view is recorded immediately, so release on failure is exact. */
    for (uint32_t i = 0; i < colors->count; ++i) {
        if (colors->slots[i].view != NULL) {
            status = doe_vk_texture_view_retain(colors->slots[i].view);
            if (status != DOE_VK_OK) return status;
            use->colors[i] = colors->slots[i].view;
        }
        if (colors->slots[i].resolve_target != NULL) {
            status = doe_vk_texture_view_retain(colors->slots[i].resolve_target);
            if (status != DOE_VK_OK) return status;
            use->resolves[i] = colors->slots[i].resolve_target;
        }
    }
    if (desc->depth_stencil != NULL) {
        status = doe_vk_texture_view_retain(desc->depth_stencil);
        if (status != DOE_VK_OK) return status;
        use->depth_stencil = desc->depth_stencil;
    }
    doe_vk_context *context = commands->context;
    status = doe_vk_render_pass_create(context, &layout, colors, desc, &use->pass, NULL);
    if (status != DOE_VK_OK) return status;
    VkImageView views[2 * DOE_VK_MAX_COLOR_ATTACHMENTS + 1] = {0};
    uint32_t view_count = 0;
    for (uint32_t i = 0; i < colors->count; ++i) if (use->colors[i] != NULL) views[view_count++] = use->colors[i]->view;
    if (desc->depth_stencil != NULL) views[view_count++] = desc->depth_stencil->view;
    for (uint32_t i = 0; i < colors->count; ++i) if (use->resolves[i] != NULL) views[view_count++] = use->resolves[i]->view;
    const VkFramebufferCreateInfo framebuffer = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = use->pass, .attachmentCount = view_count, .pAttachments = views,
        .width = width, .height = height, .layers = 1};
    status = doe_vk_result(context, context->vk.vkCreateFramebuffer(context->device,
        &framebuffer, NULL, &use->framebuffer), "vkCreateFramebuffer", NULL);
    if (status != DOE_VK_OK) return status;
    doe_vk_barrier(commands);
    for (uint32_t i = 0; i < colors->count; ++i) {
        if (color[i] != NULL) doe_vk_texture_transition(commands, color[i], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        if (resolve[i] != NULL) doe_vk_texture_transition(commands, resolve[i], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
    if (depth != NULL) doe_vk_texture_transition(commands, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    /* These are speculative final aspect states, published only on successful
     * command completion. Discard cannot become initialized through a draw. */
    for (uint32_t i = 0; i < colors->count; ++i) {
        const doe_vk_color_attachment *slot = &colors->slots[i];
        if (color[i] != NULL)
            color[i]->initialized[subresource_index(slot->view)] = slot->discard ? 0 : VK_IMAGE_ASPECT_COLOR_BIT;
        if (resolve[i] != NULL) resolve[i]->initialized[subresource_index(slot->resolve_target)] = VK_IMAGE_ASPECT_COLOR_BIT;
    }
    if (depth != NULL) {
        uint32_t discarded = (desc->discard_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : 0) |
            (desc->discard_stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
        depth->initialized[depth_index] = (uint8_t)(aspects & ~discarded);
    }
    *out = use;
    return DOE_VK_OK;
}

void doe_vk_render_attachments_begin(doe_vk_commands *commands, const doe_vk_render_use *use,
    const doe_vk_render_pass_desc *desc, const doe_vk_render_colors *colors, uint32_t width, uint32_t height) {
    VkClearValue values[2 * DOE_VK_MAX_COLOR_ATTACHMENTS + 1];
    memset(values, 0, sizeof(values));
    _Static_assert(sizeof(values[0].color) == sizeof(colors->slots[0].clear_value), "clear color union ABI");
    uint32_t count = 0;
    for (uint32_t i = 0; i < colors->count; ++i) {
        if (colors->slots[i].view == NULL) continue;
        if (colors->slots[i].clear) memcpy(&values[count].color, &colors->slots[i].clear_value, sizeof(values[count].color));
        ++count;
    }
    if (desc->depth_stencil != NULL)
        values[count++].depthStencil = (VkClearDepthStencilValue){desc->depth_clear_value, desc->stencil_clear_value};
    /* Resolve attachments never clear; values past the last attachment are unused. */
    const VkRenderPassBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = use->pass, .framebuffer = use->framebuffer,
        .renderArea = {.extent = {width, height}}, .clearValueCount = count,
        .pClearValues = count != 0 ? values : NULL};
    commands->context->vk.vkCmdBeginRenderPass(commands->command, &begin, VK_SUBPASS_CONTENTS_INLINE);
}
