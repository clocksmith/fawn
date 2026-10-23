/* SPDX-License-Identifier: MIT */
#include "render_internal.h"

/* Attachment initialization uses the same render pass and speculative image
 * state as explicit clears. This owner never publishes CPU-generated contents. */
doe_vk_status doe_vk_commands_initialize_attachment(doe_vk_commands *commands,
    doe_vk_texture_view *view) {
    doe_vk_status status = doe_vk_recording(commands);
    if (status != DOE_VK_OK) return status;
    if (view == NULL || view->texture->context != commands->context ||
        view->texture->desc.dimension != DOE_VK_TEXTURE_2D ||
        !(view->texture->desc.usage & (DOE_VK_TEXTURE_COLOR_ATTACHMENT | DOE_VK_TEXTURE_DEPTH_STENCIL_ATTACHMENT)))
        return doe_vk_poison(commands, DOE_VK_INVALID_ARGUMENT);
    if (!(commands->context->caps.queue_flags & VK_QUEUE_GRAPHICS_BIT))
        return doe_vk_poison(commands, DOE_VK_UNSUPPORTED);
    doe_vk_texture_use *use = NULL;
    status = doe_vk_use_texture(commands, view->texture, &use);
    if (status != DOE_VK_OK) return doe_vk_poison(commands, status);
    uint32_t aspects = doe_vk_format_aspects(view->texture->desc.format);
    uint32_t layers = view->texture->desc.depth_or_layers;
    const doe_vk_clear_color zero = {0};
    for (uint32_t mip = 0; mip < view->desc.mip_count; ++mip) {
        for (uint32_t layer = 0; layer < view->desc.layer_count; ++layer) {
            uint32_t selected_mip = view->desc.base_mip + mip;
            uint32_t selected_layer = view->desc.base_layer + layer;
            size_t index = (size_t)selected_mip * layers + selected_layer;
            uint32_t missing = aspects & ~use->initialized[index];
            if (missing == 0) continue;
            doe_vk_texture_view *target = view;
            bool temporary = view->desc.dimension != DOE_VK_VIEW_2D ||
                view->desc.mip_count != 1 || view->desc.layer_count != 1;
            if (temporary) {
                const doe_vk_texture_view_desc single = {DOE_VK_VIEW_2D, selected_mip, 1, selected_layer, 1};
                status = doe_vk_texture_view_create(view->texture, &single, &target, NULL);
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
