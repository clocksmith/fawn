/* SPDX-License-Identifier: MIT */
#ifndef DOE_D3D12_GRAPHICS_H
#define DOE_D3D12_GRAPHICS_H
#include "doe_d3d12_texture.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Offscreen triangle-list graphics: procedural vertices (SV_VertexID), no
 * vertex/index attributes, depth, MSAA, blending or culling. One RGBA8_UNORM
 * attachment; full-target viewport/scissor. Shader validation stays upstream.
 * Fragment bytecode and all-stage immutable resource layout live in fragment. */
typedef struct doe_dx_graphics_pipeline_desc {
    const void *vertex_bytecode;
    size_t vertex_bytecode_size;
    doe_dx_resource_pipeline_desc fragment;
    doe_dx_format color_format;
} doe_dx_graphics_pipeline_desc;
doe_dx_status doe_dx_pipeline_create_graphics(doe_dx_context *context,
    const doe_dx_graphics_pipeline_desc *desc, doe_dx_pipeline **out, doe_dx_error *error);

typedef struct doe_dx_draw_desc {
    doe_dx_texture *color;
    bool clear; /* Otherwise load initialized texels. Store is unconditional. */
    float clear_color[4]; /* Clear components must be finite, in [0,1]. */
    uint32_t vertex_count, instance_count, first_vertex, first_instance;
} doe_dx_draw_desc;
/* One atomic recording operation: failure precedes command mutation. A clear
 * initializes the whole attachment only at completion; draw coverage alone does
 * not. The attachment cannot alias any bound sampled/storage texture. */
doe_dx_status doe_dx_commands_draw(doe_dx_commands *commands, doe_dx_pipeline *pipeline,
    const doe_dx_compute_resources *resources, const doe_dx_draw_desc *draw, doe_dx_error *error);

/* Timestamp heaps have 1..64 slots and the same exclusive command ownership as
 * buffers/textures. Writes become initialized only on successful completion.
 * No callbacks, implicit submission, frequency fallback or integer conversion. */
typedef struct doe_dx_timestamps doe_dx_timestamps;
doe_dx_status doe_dx_timestamps_create(doe_dx_context *context, uint32_t count,
    doe_dx_timestamps **out, doe_dx_error *error);
doe_dx_status doe_dx_timestamps_retain(doe_dx_timestamps *timestamps);
void doe_dx_timestamps_release(doe_dx_timestamps *timestamps);
doe_dx_status doe_dx_timestamp_frequency(doe_dx_context *context, uint64_t *out, doe_dx_error *error);
doe_dx_status doe_dx_commands_timestamp(doe_dx_commands *commands,
    doe_dx_timestamps *timestamps, uint32_t index, doe_dx_error *error);
/* Resolves initialized slots to READBACK bytes. Offset must be 8-byte aligned;
 * partial writes require an already initialized destination. Raw uint64 ticks
 * may be read through buffer_read after observed successful completion. */
doe_dx_status doe_dx_commands_resolve_timestamps(doe_dx_commands *commands,
    doe_dx_timestamps *timestamps, uint32_t first, uint32_t count,
    doe_dx_buffer *destination, uint64_t offset, doe_dx_error *error);
#ifdef __cplusplus
}
#endif
#endif
