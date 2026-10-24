/* SPDX-License-Identifier: MIT */
#ifndef DOE_D3D12_RENDER_H
#define DOE_D3D12_RENDER_H
#include "doe_d3d12_dynamic.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Additive whole-pass graphics contract. Existing static/compute layouts keep
 * their representation. Each immutable stage owns at most 64 resource rows and
 * 16 runtime sampler rows; graphics packs 128/32 into one heap of each kind.
 * Stage0 is VS, stage1 PS. Register identity remains compiler/caller-owned;
 * equal native registers in disjoint stages never collide or get remapped. */
enum { DOE_DX_RENDER_STAGES = 2, DOE_DX_RENDER_DESCRIPTORS = 128, DOE_DX_RENDER_SAMPLERS = 32 };
typedef struct doe_dx_render_pipeline_desc {
    doe_dx_dynamic_pipeline_desc stages[DOE_DX_RENDER_STAGES];
    doe_dx_format color_format; /* One RGBA8/BGRA8 UNORM target, sample 1. */
    bool front_counter_clockwise;
} doe_dx_render_pipeline_desc;
typedef struct doe_dx_render_draw {
    doe_dx_pipeline *pipeline;
    doe_dx_dynamic_resources stages[DOE_DX_RENDER_STAGES];
    uint32_t vertex_count, instance_count, first_vertex, first_instance;
    float viewport[6]; /* x/y/w/h/minDepth/maxDepth, finite, within target. */
    uint32_t scissor[4]; /* x/y/w/h, within target; empty is permitted. */
} doe_dx_render_draw;
typedef struct doe_dx_render_pass {
    doe_dx_texture *color;
    bool clear; /* LOAD otherwise; an uninitialized LOAD really clears zero. */
    float clear_color[4]; /* Finite components; native UNORM conversion clamps. */
    bool discard; /* Records a real zero clear at end, not a fake init bit. */
    const doe_dx_render_draw *draws;
    size_t draw_count; /* Zero draws still execute attachment operations. */
} doe_dx_render_pass;
/* Constructors copy typed declarations. No borrowed shader/DXC data survives.
 * No vertex/index buffers, depth, blending, culling, MSAA or indirect work yet.
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
