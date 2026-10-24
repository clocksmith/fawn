/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_D3D12_GRAPHICS_INTERNAL_H
#define DOE_NATIVE_D3D12_GRAPHICS_INTERNAL_H
#include "d3d12_binding_internal.h"
#include "../../backends/d3d12/include/doe_d3d12_render.h"
#include "render_internal.h"

typedef struct native_d3d12_graphics {
    native_d3d12_compute stages[DOE_DX_RENDER_STAGES]; /* Copied slot maps, no guard objects. */
} native_d3d12_graphics;
typedef struct native_d3d12_graphics_snapshot {
    native_d3d12_snapshot stages[DOE_DX_RENDER_STAGES];
    doe_dx_buffer_binding vertices[DOE_DX_MAX_VERTEX_BUFFERS];
} native_d3d12_graphics_snapshot;
/* Maps independently compiled stage identities into one canonical public binding
 * list. Module-local resource ordinals never become cross-stage identities. */
bool native_d3d12_graphics_maps(const native_pipeline *,const native_stage [2],
    native_d3d12_layout [2],native_d3d12_graphics *);
/* Called after shared stage-interface validation; source reflection is unchanged. */
bool native_d3d12_interstage_mask(const native_stage *,uint32_t *);
bool native_d3d12_graphics_vertices(const WGPUVertexState *,const native_device *,doe_dx_render_pipeline_desc *,
    doe_resource_render_pipeline_desc *,doe_dx_vertex_layout [DOE_DX_MAX_VERTEX_BUFFERS],
    doe_dx_vertex_attribute [DOE_DX_MAX_VERTEX_ATTRIBUTES]);
WGPURenderPipeline native_d3d12_create_render_pipeline(native_device *,
    const WGPURenderPipelineDescriptor *,const native_stage *,size_t,native_pipeline_error *);
native_failure native_d3d12_render(native_command_handle *,const doe_resource_command *,size_t,size_t *);
#endif
