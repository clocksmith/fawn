/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <string.h>

/* The objects a device that is no longer alive returns (WebGPU invalid objects
 * after loss or destroy()). They own no native payload and keep only the
 * descriptor values their getters report. Device liveness never comes back, so
 * every resource operation on them already rejects as UNAVAILABLE. NULL for an
 * invalid configuration, a device that is still alive, or allocation failure. */
static void *lost_object(const doe_resource_config *config, size_t size, size_t alignment)
{
    if (!resource_config_valid(config) || config->device.alive(config->device.identity)) return NULL;
    void *object = resource_allocate(config, size, alignment);
    if (object) memset(object, 0, size);
    return object;
}
#define LOST(type, config) \
    doe_resource_##type *object = lost_object(config, sizeof(doe_resource_##type), _Alignof(doe_resource_##type)); \
    if (!object) return NULL; \
    resource_init(&object->base, config, (doe_resource_native){0})

/* Explicit invalid resources on a live device use the same storage/lifetime
 * representation; destroyed is irreversible and admits no native use. */
doe_resource_buffer *doe_resource_buffer_error(const doe_resource_config *config,
    const doe_resource_buffer_desc *desc, doe_resource_native native)
{
    if (!resource_config_valid(config)) return NULL;
    doe_resource_buffer *object = resource_allocate(config, sizeof(*object), _Alignof(doe_resource_buffer));
    if (!object) return NULL;
    memset(object, 0, sizeof(*object));
    resource_init(&object->base, config, native);
    if (desc) object->desc = *desc;
    object->desc.mapped_at_creation = false;
    object->destroyed = true;
    return object;
}
doe_resource_texture *doe_resource_texture_error(const doe_resource_config *config,
    const doe_resource_texture_desc *desc)
{
    if (!resource_config_valid(config)) return NULL;
    doe_resource_texture *object = resource_allocate(config, sizeof(*object), _Alignof(doe_resource_texture));
    if (!object) return NULL;
    memset(object, 0, sizeof(*object));
    resource_init(&object->base, config, (doe_resource_native){0});
    if (desc) object->desc = *desc;
    object->desc.view_format_count = 0;
    object->desc.view_formats = object->view_formats;
    object->destroyed = true;
    object->error_object = true;
    return object;
}

doe_resource_shader *doe_resource_shader_lost(const doe_resource_config *config)
{
    LOST(shader, config);
    return object;
}
doe_resource_buffer *doe_resource_buffer_lost(const doe_resource_config *config, const doe_resource_buffer_desc *desc,
    doe_resource_native native)
{
    LOST(buffer, config);
    object->base.native = native; /* Optional frontend bookkeeping; never a device allocation. */
    if (desc) object->desc = *desc;
    object->desc.mapped_at_creation = false;
    return object;
}
doe_resource_texture *doe_resource_texture_lost(const doe_resource_config *config, const doe_resource_texture_desc *desc)
{
    LOST(texture, config);
    if (desc) object->desc = *desc;
    object->desc.view_format_count = 0;
    object->desc.view_formats = object->view_formats;
    return object;
}
doe_resource_view *doe_resource_view_lost(doe_resource_texture *texture, const doe_resource_view_desc *desc)
{
    if (!texture) return NULL;
    LOST(view, &texture->base.config);
    doe_resource_texture_retain(texture);
    object->texture = texture;
    if (desc) object->desc = *desc;
    return object;
}
doe_resource_sampler *doe_resource_sampler_lost(const doe_resource_config *config)
{
    LOST(sampler, config);
    return object;
}
doe_resource_layout *doe_resource_layout_lost(const doe_resource_config *config)
{
    LOST(layout, config); /* No entries: the flexible array is empty. */
    return object;
}
doe_resource_pipeline_layout *doe_resource_pipeline_layout_lost(const doe_resource_config *config)
{
    LOST(pipeline_layout, config);
    return object;
}
doe_resource_bind_group *doe_resource_bind_group_lost(const doe_resource_config *config)
{
    LOST(bind_group, config); /* No layout, no bindings. */
    return object;
}
doe_resource_pipeline *doe_resource_pipeline_lost(const doe_resource_config *config)
{
    LOST(pipeline, config);
    return object;
}
doe_resource_render_pipeline *doe_resource_render_pipeline_lost(const doe_resource_config *config)
{
    LOST(render_pipeline, config);
    return object;
}
doe_resource_query_set *doe_resource_query_set_lost(const doe_resource_config *config, WGPUQueryType type, uint32_t count)
{
    LOST(query_set, config);
    object->type = type;
    object->count = count;
    return object;
}
doe_resource_encoder *doe_resource_encoder_lost(const doe_resource_config *config)
{
    LOST(encoder, config);
    object->status = DOE_RESOURCE_UNAVAILABLE; /* Every command and Finish reject. */
    return object;
}
