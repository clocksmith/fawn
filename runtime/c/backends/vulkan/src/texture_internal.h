/* SPDX-License-Identifier: MIT */
#ifndef DOE_VK_TEXTURE_INTERNAL_H
#define DOE_VK_TEXTURE_INTERNAL_H
#include "internal.h"
#include "../include/doe_vk_texture.h"
struct doe_vk_texture {
    doe_vk_context *context;
    uint32_t references;
    doe_vk_texture_desc desc;
    uint32_t texel_bytes;
    uint32_t layout;
    size_t subresource_count;
    uint8_t *initialized;
    doe_vk_status content_status;
    doe_vk_commands *recording_owner;
    bool borrowed_image, external_alive;
    doe_vk_commands *external_owner; /* Prepared presentation seals this frame. */
    VkImage image;
    VkDeviceMemory memory;
    /* Mutable-format list (image format first); zero count when immutable. */
    doe_vk_texture_format view_formats[2];
    uint32_t view_format_count;
};
struct doe_vk_texture_view {
    doe_vk_texture *texture;
    uint32_t references;
    doe_vk_texture_view_desc desc;
    uint32_t aspects; /* Resolved native aspect bits of this view. */
    doe_vk_texture_format format; /* Resolved view format (image or its pair). */
    VkImageView view;
};
struct doe_vk_sampler {
    doe_vk_context *context;
    uint32_t references;
    VkSampler sampler;
};
typedef struct doe_vk_texture_use {
    doe_vk_texture *texture;
    uint32_t layout;
    uint8_t *initialized;
    struct doe_vk_texture_use *next;
} doe_vk_texture_use;
static inline uint32_t doe_vk_samples(uint32_t requested) {
    return requested != 0 ? requested : 1;
}
/* The srgb/unorm reinterpretation pair of a format, or zero. */
doe_vk_texture_format doe_vk_srgb_pair(doe_vk_texture_format format);
doe_vk_status doe_vk_image_format_properties(doe_vk_context *context,
    const doe_vk_texture_desc *desc, VkImageFormatProperties *out);
doe_vk_status doe_vk_attachment_samples(doe_vk_context *context,
    doe_vk_texture_format format, uint32_t *out);
/* Initialization bytes are per-aspect masks (color1, depth2, stencil4). */
uint32_t doe_vk_format_aspects(doe_vk_texture_format format);
/* Texel width/height of one block: 4 for BC formats, else 1. */
uint32_t doe_vk_format_block_extent(doe_vk_texture_format format);
/* Any non-identity channel: such a view is sampled-only. */
bool doe_vk_texture_view_swizzled(const doe_vk_texture_view *view);
bool doe_vk_texture_view_swizzled_desc(const doe_vk_texture_view_desc *desc);
/* 3D color-attachment images are 2D_ARRAY_COMPATIBLE; a 2D view of one of
 * their mips' depth slices is a render attachment. */
bool doe_vk_texture_slice_renderable(const doe_vk_texture_desc *desc);
bool doe_vk_texture_view_is_slice(const doe_vk_texture_view *view);
/* One aspect-selection owner for views and transfers: ALL resolves to every
 * format aspect; DEPTH/STENCIL must be present in the format. */
doe_vk_status doe_vk_texture_aspects(const doe_vk_texture *texture, doe_vk_texture_aspect aspect, uint32_t *out);
doe_vk_status doe_vk_use_texture(doe_vk_commands *commands, doe_vk_texture *texture,
    doe_vk_texture_use **out);
void doe_vk_texture_transition(doe_vk_commands *commands, doe_vk_texture_use *use,
    uint32_t layout);
void doe_vk_textures_complete(doe_vk_commands *commands, doe_vk_status status);
void doe_vk_textures_release(doe_vk_commands *commands);
doe_vk_status doe_vk_texture_transfer_region(const doe_vk_context *context,
    const doe_vk_texture *texture, doe_vk_texture_region region);
doe_vk_status doe_vk_texture_copy_region(const doe_vk_texture *texture,
    doe_vk_texture_region region, uint64_t buffer_size,
    doe_vk_texture_buffer_layout layout, VkBufferImageCopy *out);
#endif
