/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_METAL_BINDING_INTERNAL_H
#define DOE_NATIVE_METAL_BINDING_INTERNAL_H
#include "metal_frontend.h"

/* Compiler-selected entry slots, indexed by this pipeline's canonical binding
 * span. A zero table means that the entry does not use the binding. No public
 * group/binding number is reinterpreted as a native slot. */
typedef struct native_metal_stage_map {
    uint32_t tables[NATIVE_MAX_PIPELINE_BINDINGS];
    uint32_t slots[NATIVE_MAX_PIPELINE_BINDINGS];
} native_metal_stage_map;

/* Call-borrowed conversion storage, not a resource/recording/lease owner. The
 * private command owner acquires native leases when it encodes these spans. */
typedef struct native_metal_snapshot {
    doe_mtl_buffer_binding buffers[DOE_MTL_BUFFER_SLOTS];
    doe_mtl_texture_binding textures[DOE_MTL_TEXTURE_SLOTS];
    doe_mtl_sampler_binding samplers[DOE_MTL_SAMPLER_SLOTS];
    doe_mtl_render_bindings bindings;
} native_metal_snapshot;

bool native_metal_stage_map_build(const native_pipeline *pipeline, const native_stage *stage,
    uint32_t expected_stage, native_metal_stage_map *map, doe_mtl_shader_stage *out);
native_failure native_metal_binding_snapshot(doe_mtl_commands *commands,
    const native_pipeline *pipeline, const native_metal_stage_map *map,
    const doe_resource_binding *resolved, native_metal_snapshot *out);
#endif
