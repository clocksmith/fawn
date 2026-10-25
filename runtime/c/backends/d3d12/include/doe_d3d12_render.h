/* SPDX-License-Identifier: MIT */
#ifndef DOE_D3D12_RENDER_H
#define DOE_D3D12_RENDER_H
#include "doe_d3d12_dynamic.h"
#include "doe_d3d12_mesh.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Additive whole-pass graphics contract. Existing static/compute layouts keep
 * their representation. Each immutable stage owns at most 64 resource rows and
 * 16 runtime sampler rows; graphics packs 128/32 into one heap of each kind.
 * Stage0 is VS, stage1 PS. Register identity remains compiler/caller-owned;
 * equal native registers in disjoint stages never collide or get remapped. */
enum { DOE_DX_RENDER_STAGES = 2, DOE_DX_RENDER_DESCRIPTORS = 128, DOE_DX_RENDER_SAMPLERS = 32 };
typedef enum doe_dx_topology {
    DOE_DX_TRIANGLES, DOE_DX_TRIANGLE_STRIP, DOE_DX_LINES, DOE_DX_LINE_STRIP, DOE_DX_POINTS
} doe_dx_topology;
typedef enum doe_dx_cull { DOE_DX_CULL_NONE, DOE_DX_CULL_FRONT, DOE_DX_CULL_BACK } doe_dx_cull;
/* Portable blend codes: zero/one=0/1, source/inverse=2/3, destination/
 * inverse=4/5, source-alpha/inverse=6/7, destination-alpha/inverse=8/9,
 * constant/inverse=10/11, source-alpha-saturated=14. Operations add,
 * subtract, reverse-subtract, minimum, maximum are 0..4. */
typedef struct doe_dx_blend_component { uint32_t source, destination, operation; } doe_dx_blend_component;
typedef struct doe_dx_render_pipeline_desc {
    doe_dx_dynamic_pipeline_desc stages[DOE_DX_RENDER_STAGES];
    doe_dx_format color_format; /* One RGBA8/BGRA8 UNORM target, sample 1. */
    bool front_counter_clockwise;
    doe_dx_cull cull;
    doe_dx_topology topology;
    bool depth_enabled, depth_write; /* Optional D32_FLOAT attachment. */
    doe_dx_compare depth_compare;
    int32_t depth_bias;
    float depth_bias_slope, depth_bias_clamp;
    bool blend_enabled;
    doe_dx_blend_component color_blend, alpha_blend;
    /* Explicit presence preserves legacy zero-initialized descriptors while
     * admitting zero masks (which must not be replaced by default all bits). */
    bool masks_explicit;
    uint32_t sample_mask, color_write_mask; /* Color RGBA bits 0..3. */
    /* Public slot order, including holes/empty layouts. Stride0 fetches the
     * same attribute for every vertex/instance; unused slots may be NULL. */
    const doe_dx_vertex_layout *vertex_buffers;
    size_t vertex_buffer_count;
    const doe_dx_vertex_attribute *attributes;
    size_t attribute_count;
} doe_dx_render_pipeline_desc;
typedef struct doe_dx_render_draw {
    doe_dx_pipeline *pipeline;
    doe_dx_dynamic_resources stages[DOE_DX_RENDER_STAGES];
    uint32_t vertex_count, instance_count, first_vertex, first_instance;
    float blend_constant[4];
    float viewport[6]; /* x/y/w/h/minDepth/maxDepth, finite, within target. */
    uint32_t scissor[4]; /* x/y/w/h, within target; empty is permitted. */
    const doe_dx_buffer_binding *vertices; /* Exactly pipeline slot order. */
    size_t vertex_buffer_count;
} doe_dx_render_draw;
typedef struct doe_dx_render_pass {
    doe_dx_texture *color;
    doe_dx_texture *depth;
    bool clear_depth, discard_depth;
    float depth_clear;
    bool clear; /* LOAD otherwise; an uninitialized LOAD really clears zero. */
    float clear_color[4]; /* Finite components; native UNORM conversion clamps. */
    bool discard; /* Records a real zero clear at end, not a fake init bit. */
    const doe_dx_render_draw *draws;
    size_t draw_count; /* Zero draws still execute attachment operations. */
} doe_dx_render_pass;
/* Constructors copy typed declarations. No borrowed shader/DXC data survives.
 * No index buffers, stencil, MSAA or indirect work yet.
 * These are explicit initial composition gaps, not the final migration target. */
doe_dx_status doe_dx_pipeline_create_render(doe_dx_context *context,
    const doe_dx_render_pipeline_desc *desc,doe_dx_pipeline **out,doe_dx_error *error);
/* All fallible preparation precedes native command mutation. Existing pipeline,
 * resource and descriptor-use leases survive unknown completion. Failure leaves
 * prior command records intact, output/initialization/leases unpublished. */
doe_dx_status doe_dx_commands_render(doe_dx_commands *commands,
    const doe_dx_render_pass *pass,doe_dx_error *error);
#ifdef __cplusplus
}
#endif
#endif
