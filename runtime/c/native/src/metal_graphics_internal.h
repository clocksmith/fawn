/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_METAL_GRAPHICS_INTERNAL_H
#define DOE_NATIVE_METAL_GRAPHICS_INTERNAL_H
#include "metal_binding_internal.h"
#include "render_internal.h"
typedef struct native_metal_graphics {
    native_metal_stage_map stages[2];
    uint32_t vertex_slots[DOE_RESOURCE_MAX_VERTEX_BUFFERS];
    uint32_t topology, winding, cull;
    float depth_bias, depth_slope, depth_clamp;
    bool unclipped_depth;
} native_metal_graphics;
native_failure native_metal_render(doe_mtl_commands *commands,
    const doe_resource_command *records, size_t count, size_t *consumed);
#endif
