/* SPDX-License-Identifier: MIT */
#ifndef DOE_D3D12_MESH_H
#define DOE_D3D12_MESH_H
#include "doe_d3d12_graphics.h"
#ifdef __cplusplus
extern "C" {
#endif

enum { DOE_DX_MAX_VERTEX_BUFFERS = 8, DOE_DX_MAX_VERTEX_ATTRIBUTES = 16 };
typedef enum doe_dx_vertex_format {
    DOE_DX_FLOAT32, DOE_DX_FLOAT32X2, DOE_DX_FLOAT32X3, DOE_DX_FLOAT32X4,
    DOE_DX_UINT32, DOE_DX_UINT32X2, DOE_DX_UINT32X3, DOE_DX_UINT32X4,
    DOE_DX_SINT32, DOE_DX_SINT32X2, DOE_DX_SINT32X3, DOE_DX_SINT32X4
} doe_dx_vertex_format;
typedef enum doe_dx_vertex_step { DOE_DX_PER_VERTEX, DOE_DX_PER_INSTANCE } doe_dx_vertex_step;
typedef struct doe_dx_vertex_layout {
    uint32_t stride; /* Multiple of 4, 4..2048. */
    doe_dx_vertex_step step; /* Instance step rate is exactly 1. */
} doe_dx_vertex_layout;
typedef struct doe_dx_vertex_attribute {
    uint32_t shader_location, buffer_slot, offset;
    doe_dx_vertex_format format; /* HLSL TEXCOORD<shader_location>. */
} doe_dx_vertex_attribute;
typedef enum doe_dx_compare {
    DOE_DX_NEVER, DOE_DX_LESS, DOE_DX_EQUAL, DOE_DX_LESS_EQUAL,
    DOE_DX_GREATER, DOE_DX_NOT_EQUAL, DOE_DX_GREATER_EQUAL, DOE_DX_ALWAYS
} doe_dx_compare;
typedef struct doe_dx_mesh_pipeline_desc {
    doe_dx_graphics_pipeline_desc graphics;
    const doe_dx_vertex_layout *vertex_buffers;
    size_t vertex_buffer_count;
    const doe_dx_vertex_attribute *attributes;
    size_t attribute_count;
    bool depth_enabled, depth_write;
    doe_dx_compare depth_compare; /* Optional D32_FLOAT, no stencil. */
} doe_dx_mesh_pipeline_desc;
doe_dx_status doe_dx_pipeline_create_mesh(doe_dx_context *context,
    const doe_dx_mesh_pipeline_desc *desc, doe_dx_pipeline **out, doe_dx_error *error);

typedef enum doe_dx_index_format { DOE_DX_INDEX_UINT16, DOE_DX_INDEX_UINT32 } doe_dx_index_format;
typedef struct doe_dx_index_binding {
    doe_dx_buffer_binding binding;
    doe_dx_index_format format;
    uint32_t first, count;
} doe_dx_index_binding;
typedef struct doe_dx_mesh_draw_desc {
    doe_dx_draw_desc draw;
    const doe_dx_buffer_binding *vertices; /* Pipeline slot order, count must match. */
    size_t vertex_count;
    const doe_dx_index_binding *indices; /* Optional. Base vertex is always zero. */
    doe_dx_texture *depth; /* Required iff pipeline enables depth; matches color extent. */
    bool clear_depth;
    float depth_clear; /* Finite [0,1]; ignored unless clear_depth. */
} doe_dx_mesh_draw_desc;
/* Draw.vertex_count/first_vertex apply only without indices and must be zero
 * with indices. All buffer views are allocation-bounded and initialized. Direct
 * vertex/instance fetch spans are checked. Indexed vertex values are GPU data;
 * native view bounds apply, not a claimed CPU scan/proof of index contents.
 * No alias between shader resources, vertex slots or index binding is admitted.
 * No depth sampling/storage, stencil, MSAA, indexed base vertex or primitive restart. */
doe_dx_status doe_dx_commands_draw_mesh(doe_dx_commands *commands, doe_dx_pipeline *pipeline,
    const doe_dx_compute_resources *resources, const doe_dx_mesh_draw_desc *draw, doe_dx_error *error);
#ifdef __cplusplus
}
#endif
#endif
