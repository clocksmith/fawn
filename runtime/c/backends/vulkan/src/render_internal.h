#ifndef DOE_VK_RENDER_INTERNAL_H
#define DOE_VK_RENDER_INTERNAL_H
#include "texture_internal.h"
#include "../include/doe_vk_render.h"
typedef struct doe_vk_render_use {
    VkRenderPass pass;
    VkFramebuffer framebuffer;
    doe_vk_texture_view *target;
    doe_vk_texture_view *depth_stencil;
    struct doe_vk_render_use *next;
} doe_vk_render_use;
doe_vk_status doe_vk_render_pass_create(doe_vk_context *context,
    doe_vk_texture_format color, doe_vk_texture_format depth_stencil,
    const doe_vk_render_pass_desc *desc, VkRenderPass *out, doe_vk_error *error);
doe_vk_status doe_vk_render_attachments_validate(doe_vk_commands *commands,
    const doe_vk_render_pass_desc *desc, uint32_t *width, uint32_t *height);
doe_vk_status doe_vk_render_attachments_prepare(doe_vk_commands *commands,
    const doe_vk_render_pass_desc *desc, uint32_t width, uint32_t height, doe_vk_render_use **out);
void doe_vk_render_attachments_begin(doe_vk_commands *commands, const doe_vk_render_use *use,
    const doe_vk_render_pass_desc *desc, uint32_t width, uint32_t height);
bool doe_vk_render_stencil_writes(const doe_vk_render_pipeline_desc *desc);
void doe_vk_render_release(doe_vk_commands *commands);
doe_vk_status doe_vk_render_viewport(const doe_vk_context *context, const doe_vk_render_draw *draw,
    uint32_t width, uint32_t height, VkViewport *out);
#endif
