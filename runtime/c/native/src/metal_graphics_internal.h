/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_METAL_GRAPHICS_INTERNAL_H
#define DOE_NATIVE_METAL_GRAPHICS_INTERNAL_H
#include "metal_frontend.h"
#include "render_internal.h"
typedef struct native_metal_graphics {
    uint32_t tables[2][DOE_RESOURCE_MAX_GROUPS * DOE_RESOURCE_MAX_BINDINGS];
    uint32_t slots[2][DOE_RESOURCE_MAX_GROUPS * DOE_RESOURCE_MAX_BINDINGS];
    uint32_t vertex_slots[DOE_RESOURCE_MAX_VERTEX_BUFFERS];
    uint32_t topology, winding, cull;
} native_metal_graphics;
bool native_metal_stage(native_pipeline *pipeline, const native_stage *stage,
    size_t index, doe_mtl_shader_stage *out);
native_failure native_metal_render(doe_mtl_commands *commands,
    const doe_resource_command *records, size_t count, size_t *consumed);
#endif
