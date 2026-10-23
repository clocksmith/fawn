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
};
struct doe_vk_texture_view {
    doe_vk_texture *texture;
    uint32_t references;
    doe_vk_texture_view_desc desc;
    uint32_t aspects; /* Resolved native aspect bits of this view. */
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
doe_vk_status doe_vk_image_format_properties(doe_vk_context *context,
    const doe_vk_texture_desc *desc, VkImageFormatProperties *out);
doe_vk_status doe_vk_attachment_samples(doe_vk_context *context,
    doe_vk_texture_format format, uint32_t *out);
/* Initialization bytes are per-aspect masks (color1, depth2, stencil4). */
uint32_t doe_vk_format_aspects(doe_vk_texture_format format);
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
