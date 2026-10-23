/* SPDX-License-Identifier: MIT */
#ifndef DOE_VK_RENDER_INTERNAL_H
#define DOE_VK_RENDER_INTERNAL_H
#include "texture_internal.h"
#include "../include/doe_vk_render.h"
typedef struct doe_vk_render_use {
    VkRenderPass pass;
    VkFramebuffer framebuffer;
    uint32_t color_count;
    doe_vk_texture_view *colors[DOE_VK_MAX_COLOR_ATTACHMENTS];
    doe_vk_texture_view *resolves[DOE_VK_MAX_COLOR_ATTACHMENTS];
    doe_vk_texture_view *depth_stencil;
    struct doe_vk_render_use *next;
} doe_vk_render_use;
/* The one normalized color-slot view of a pass descriptor: legacy single-color
 * fields become slot 0; validation, preparation and begin consume only this. */
typedef struct doe_vk_render_colors {
    uint32_t count;
    doe_vk_color_attachment slots[DOE_VK_MAX_COLOR_ATTACHMENTS];
} doe_vk_render_colors;
doe_vk_status doe_vk_render_colors_normalize(const doe_vk_render_pass_desc *desc, doe_vk_render_colors *out);
/* Formats per slot (zero is unused), shared by pipeline and pass creation so
 * both sides of Vulkan render-pass compatibility come from one constructor. */
typedef struct doe_vk_render_layout {
    uint32_t color_count;
    doe_vk_texture_format colors[DOE_VK_MAX_COLOR_ATTACHMENTS];
    doe_vk_texture_format depth_stencil;
    uint32_t samples;
} doe_vk_render_layout;
/* NULL colors/desc builds the pipeline-compatible pass (LOAD/STORE, no resolve). */
doe_vk_status doe_vk_render_pass_create(doe_vk_context *context, const doe_vk_render_layout *layout,
    const doe_vk_render_colors *colors, const doe_vk_render_pass_desc *desc, VkRenderPass *out, doe_vk_error *error);
doe_vk_status doe_vk_render_attachments_validate(doe_vk_commands *commands,
    const doe_vk_render_pass_desc *desc, const doe_vk_render_colors *colors, uint32_t *width, uint32_t *height);
doe_vk_status doe_vk_render_attachments_prepare(doe_vk_commands *commands, const doe_vk_render_pass_desc *desc,
    const doe_vk_render_colors *colors, uint32_t width, uint32_t height, doe_vk_render_use **out);
void doe_vk_render_attachments_begin(doe_vk_commands *commands, const doe_vk_render_use *use,
    const doe_vk_render_pass_desc *desc, const doe_vk_render_colors *colors, uint32_t width, uint32_t height);
bool doe_vk_render_stencil_writes(const doe_vk_render_pipeline_desc *desc);
void doe_vk_render_release(doe_vk_commands *commands);
doe_vk_status doe_vk_render_viewport(const doe_vk_context *context, const doe_vk_render_draw *draw,
    uint32_t width, uint32_t height, VkViewport *out);
doe_vk_status doe_vk_render_vertex_limits(doe_vk_commands *commands, doe_vk_pipeline *pipeline,
    const doe_vk_buffer_binding *vertices, size_t count, uint32_t out_limits[2]);
/* Exact native index capacity in elements, clamped to UINT32_MAX because native
 * firstIndex/indexCount are uint32 and the guard compares without wrapping. */
doe_vk_status doe_vk_render_index_limit(doe_vk_commands *commands,
    const doe_vk_render_indices *indices, uint32_t *out_limit);
doe_vk_status doe_vk_draw_indirect_validate(doe_vk_commands *commands,
    const doe_vk_render_indirect *indirect, bool indexed);
/* limits[0] is the vertex capacity, or the index capacity when indexed. */
doe_vk_status doe_vk_draw_indirect_prepare(doe_vk_commands *commands,
    const doe_vk_render_indirect *indirect, const uint32_t limits[2], bool indexed, VkBuffer *out);
#endif
