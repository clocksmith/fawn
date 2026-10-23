/* SPDX-License-Identifier: MIT */
#ifndef DOE_MTL_TEXTURE_INTERNAL_H
#define DOE_MTL_TEXTURE_INTERNAL_H
#include "internal.h"

struct doe_mtl_texture {
    doe_mtl_context *context;
    uint32_t references;
    void *native;
    doe_mtl_texture_desc desc;
    doe_mtl_format_info format;
    size_t subresource_count;
    bool *initialized;
    bool poisoned;
    doe_mtl_commands *owner;
};
struct doe_mtl_texture_view {
    doe_mtl_texture *texture;
    uint32_t references;
    void *native;
    doe_mtl_texture_view_desc desc;
};
struct doe_mtl_sampler {
    doe_mtl_context *context;
    uint32_t references;
    void *native;
};
typedef struct doe_mtl_texture_use {
    doe_mtl_texture *texture;
    bool *initialized;
    struct doe_mtl_texture_use *next;
} doe_mtl_texture_use;
typedef struct doe_mtl_view_use {
    doe_mtl_texture_view *view;
    struct doe_mtl_view_use *next;
} doe_mtl_view_use;
typedef struct doe_mtl_sampler_use {
    doe_mtl_sampler *sampler;
    struct doe_mtl_sampler_use *next;
} doe_mtl_sampler_use;

bool doe_mtl_format_compatible(doe_mtl_format a, doe_mtl_format b);
uint32_t doe_mtl_mip_extent(uint32_t dimension, uint32_t mip);
uint32_t doe_mtl_texture_layers(const doe_mtl_texture_desc *desc);
doe_mtl_status doe_mtl_texture_preflight(const doe_mtl_commands *commands,
    const doe_mtl_texture *texture, uint32_t usage);
doe_mtl_status doe_mtl_texture_region_check(const doe_mtl_texture *texture,
    doe_mtl_texture_region region, bool *full);
bool doe_mtl_texture_region_initialized(const doe_mtl_commands *commands,
    const doe_mtl_texture *texture, doe_mtl_texture_region region);
doe_mtl_status doe_mtl_use_texture(doe_mtl_commands *commands,
    doe_mtl_texture *texture, doe_mtl_texture_use **out);
void doe_mtl_texture_region_initialize(doe_mtl_texture_use *use, doe_mtl_texture_region region);
doe_mtl_status doe_mtl_image_bindings_preflight(doe_mtl_commands *commands,
    const doe_mtl_texture_binding *textures, size_t texture_count,
    const doe_mtl_sampler_binding *samplers, size_t sampler_count,
    doe_mtl_native_resources *native);
doe_mtl_status doe_mtl_image_bindings_retain(doe_mtl_commands *commands,
    const doe_mtl_texture_binding *textures, size_t texture_count,
    const doe_mtl_sampler_binding *samplers, size_t sampler_count);
void doe_mtl_images_retire(doe_mtl_commands *commands, bool completed, bool success);
#endif
