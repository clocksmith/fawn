/* SPDX-License-Identifier: MIT */
#include "render_internal.h"

/* Attachment initialization uses the same render pass and speculative image
 * state as explicit clears. This owner never publishes CPU-generated contents. */
/* Clears every undefined aspect of the selected 2D mips/layers to zero with real
 * attachment clears. `view` (optional) is reused when it already names exactly
 * one 2D subresource; otherwise single-subresource views are made per clear. */
static doe_vk_status initialize_span(doe_vk_commands *commands, doe_vk_texture *texture,
    doe_vk_texture_view *view, uint32_t base_mip, uint32_t mip_count, uint32_t base_layer, uint32_t layer_count) {
    if (!(commands->context->caps.queue_flags & VK_QUEUE_GRAPHICS_BIT))
        return doe_vk_poison(commands, DOE_VK_UNSUPPORTED);
    doe_vk_texture_use *use = NULL;
    doe_vk_status status = doe_vk_use_texture(commands, texture, &use);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    uint32_t aspects = doe_vk_format_aspects(texture->desc.format);
    uint32_t layers = texture->desc.depth_or_layers;
    const doe_vk_clear_color zero = {0};
    for (uint32_t mip = 0; mip < mip_count; ++mip) {
        for (uint32_t layer = 0; layer < layer_count; ++layer) {
            uint32_t selected_mip = base_mip + mip;
            uint32_t selected_layer = base_layer + layer;
            size_t index = (size_t)selected_mip * layers + selected_layer;
            uint32_t missing = aspects & ~use->initialized[index];
            if (missing == 0) continue;
            doe_vk_texture_view *target = view;
            bool temporary = view == NULL || view->desc.dimension != DOE_VK_VIEW_2D ||
                view->desc.mip_count != 1 || view->desc.layer_count != 1 || view->aspects != aspects ||
                view->format != texture->desc.format;
            if (temporary) {
                const doe_vk_texture_view_desc single = {DOE_VK_VIEW_2D, selected_mip, 1, selected_layer, 1, DOE_VK_ASPECT_ALL, 0, {0}};
                status = doe_vk_texture_view_create(texture, &single, &target, NULL);
                if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
            }
            doe_vk_render_pass_desc desc = {0};
            if (aspects == VK_IMAGE_ASPECT_COLOR_BIT) {
                desc.color = target;
                desc.clear_color = &zero;
            } else {
                desc.depth_stencil = target;
                desc.clear_depth = (missing & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
                desc.clear_stencil = (missing & VK_IMAGE_ASPECT_STENCIL_BIT) != 0;
            }
            status = doe_vk_commands_render_pass_full(commands, &desc, NULL, NULL, 0);
            if (temporary) doe_vk_texture_view_release(target);
            if (status != DOE_VK_OK) return status;
        }
    }
    return DOE_VK_OK;
}

static bool attachment_capable(const doe_vk_texture *texture) {
    return texture->desc.dimension == DOE_VK_TEXTURE_2D &&
        (texture->desc.usage & (DOE_VK_TEXTURE_COLOR_ATTACHMENT | DOE_VK_TEXTURE_DEPTH_STENCIL_ATTACHMENT)) != 0;
}

doe_vk_status doe_vk_commands_initialize_attachment(doe_vk_commands *commands,
    doe_vk_texture_view *view) {
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return status;
    if (view == NULL || view->texture->context != commands->context || !attachment_capable(view->texture))
        return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    return initialize_span(commands, view->texture, view, view->desc.base_mip, view->desc.mip_count,
        view->desc.base_layer, view->desc.layer_count);
}

doe_vk_status doe_vk_commands_initialize_texture(doe_vk_commands *commands, doe_vk_texture *texture) {
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return status;
    if (texture == NULL || texture->context != commands->context) return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    /* Only attachment-capable 2D images can lose contents (attachment discard);
     * every other image is fully initialized from creation onward. */
    if (!attachment_capable(texture)) return DOE_VK_OK;
    return initialize_span(commands, texture, NULL, 0, texture->desc.mip_levels, 0, texture->desc.depth_or_layers);
}
