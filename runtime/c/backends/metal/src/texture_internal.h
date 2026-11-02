/* SPDX-License-Identifier: MIT */
#ifndef DOE_MTL_TEXTURE_INTERNAL_H
#define DOE_MTL_TEXTURE_INTERNAL_H
#include "internal.h"

doe_mtl_status doe_mtl_initialize_texture_region(doe_mtl_commands *, doe_mtl_texture *,
    doe_mtl_texture_region, uint8_t, doe_mtl_error *);
typedef struct doe_mtl_texture_use doe_mtl_texture_use;
struct doe_mtl_texture {
    doe_mtl_context *context;
    uint32_t references;
    void *native;
    doe_mtl_texture_desc desc;
    doe_mtl_format_info format;
    size_t subresource_count;
    uint8_t *initialized;
    bool poisoned;
    bool queue_ordered; /* Actual native resource reports tracked hazards. */
    uint64_t initialization_generation;
    uint64_t last_submission; /* Last texture use's queue point, including retired uses. */
    bool evict_requested; /* Public destroy(): free native storage once unused. */
    bool evicted;         /* Native texture and every native view released. */
    doe_mtl_commands *owner;
    doe_mtl_texture_use *uses;
    doe_mtl_surface *surface;
    void *drawable;
    bool presentation_registered;
    struct doe_mtl_texture_view *views; /* Live views; Metal views share storage. */
};
struct doe_mtl_texture_view {
    doe_mtl_texture *texture;
    uint32_t references;
    void *native;
    doe_mtl_texture_view_desc desc;
    struct doe_mtl_texture_view *prev, *next; /* Parent's live-view list. */
};
struct doe_mtl_sampler {
    doe_mtl_context *context;
    uint32_t references;
    void *native;
};
struct doe_mtl_texture_use {
    doe_mtl_texture *texture;
    doe_mtl_commands *commands;
    uint8_t *initialized;
    uint64_t initialization_generation;
    struct doe_mtl_texture_use *next;
    struct doe_mtl_texture_use *older, *newer;
};
typedef struct doe_mtl_view_use {
    doe_mtl_texture_view *view;
    struct doe_mtl_view_use *next;
} doe_mtl_view_use;
typedef struct doe_mtl_sampler_use {
    doe_mtl_sampler *sampler;
    struct doe_mtl_sampler_use *next;
} doe_mtl_sampler_use;

enum { DOE_MTL_ASPECT_COLOR = 1, DOE_MTL_ASPECT_DEPTH = 2, DOE_MTL_ASPECT_STENCIL = 4 };
/* Adopts native +1 only on success; external producer contents are initialized. */
doe_mtl_status doe_mtl_texture_adopt_external(doe_mtl_context *context,
    const doe_mtl_texture_desc *desc, void *native, doe_mtl_texture **out, doe_mtl_error *error);
uint8_t doe_mtl_format_aspects(doe_mtl_format format);
/* Releases the native texture and every native view once no recording owns it. */
void doe_mtl_texture_evict_now(doe_mtl_texture *texture);
bool doe_mtl_samples_supported(const doe_mtl_caps *caps, uint32_t samples);
bool doe_mtl_texture_aspect_initialized(const doe_mtl_commands *commands,
    const doe_mtl_texture_view *view, uint8_t aspects);
const uint8_t *doe_mtl_texture_initialization(const doe_mtl_commands *, const doe_mtl_texture *);
void doe_mtl_texture_aspect_publish(doe_mtl_commands *commands,
    const doe_mtl_texture_view *view, uint8_t aspects, bool initialized);
bool doe_mtl_format_compatible(doe_mtl_format a, doe_mtl_format b);
uint32_t doe_mtl_mip_extent(uint32_t dimension, uint32_t mip);
uint32_t doe_mtl_texture_layers(const doe_mtl_texture_desc *desc);
doe_mtl_status doe_mtl_texture_preflight(doe_mtl_commands *commands,
    const doe_mtl_texture *texture, uint32_t usage);
doe_mtl_status doe_mtl_texture_region_check(const doe_mtl_texture *texture,
    doe_mtl_texture_region region, bool *full);
bool doe_mtl_texture_region_aspects_initialized(const doe_mtl_commands *commands,
    const doe_mtl_texture *texture, doe_mtl_texture_region region, uint8_t aspects);
void doe_mtl_texture_region_aspects_initialize(doe_mtl_texture_use *use,
    doe_mtl_texture_region region, uint8_t aspects);
bool doe_mtl_texture_region_initialized(const doe_mtl_commands *commands,
    const doe_mtl_texture *texture, doe_mtl_texture_region region);
doe_mtl_status doe_mtl_use_texture(doe_mtl_commands *commands,
    doe_mtl_texture *texture, doe_mtl_texture_use **out);
void doe_mtl_texture_region_initialize(doe_mtl_texture_use *use, doe_mtl_texture_region region);
doe_mtl_status doe_mtl_immediates_preflight(doe_mtl_immediate_binding binding,
    uint32_t sizes_slot, doe_mtl_native_resources *native);
doe_mtl_status doe_mtl_image_bindings_preflight(doe_mtl_commands *commands,
    const doe_mtl_texture_binding *textures, size_t texture_count,
    const doe_mtl_sampler_binding *samplers, size_t sampler_count,
    doe_mtl_native_resources *native);
doe_mtl_status doe_mtl_image_bindings_retain(doe_mtl_commands *commands,
    const doe_mtl_texture_binding *textures, size_t texture_count,
    const doe_mtl_sampler_binding *samplers, size_t sampler_count);
doe_mtl_status doe_mtl_use_view(doe_mtl_commands *commands, doe_mtl_texture_view *view);
void doe_mtl_images_retire(doe_mtl_commands *commands, bool completed, bool success);
#endif
