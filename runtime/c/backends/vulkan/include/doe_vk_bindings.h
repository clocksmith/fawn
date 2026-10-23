#ifndef DOE_VK_BINDINGS_H
#define DOE_VK_BINDINGS_H
#include "doe_vk_texture.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Explicit tags must match the corresponding immutable pipeline layout entry.
 * Shader/layout type, access, sampled type/filterability and view-dimension
 * compatibility are compiler/native validation obligations. No implicit casts. */
typedef struct doe_vk_binding_resource {
    doe_vk_binding_kind kind;
    union {
        doe_vk_buffer_binding buffer;
        doe_vk_texture_view *view;
        doe_vk_sampler *sampler;
    } value;
} doe_vk_binding_resource;
/* Every referenced image subresource must already be initialized, including by
 * an earlier full upload recorded in the same command owner. Recording retains
 * exact image views/samplers through native terminal completion and retirement. */
doe_vk_status doe_vk_commands_dispatch_resources(doe_vk_commands *commands,
    doe_vk_pipeline *pipeline, const doe_vk_binding_resource *bindings,
    size_t binding_count, uint32_t x, uint32_t y, uint32_t z);
#ifdef __cplusplus
}
#endif
#endif
