/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_D3D12_BINDING_INTERNAL_H
#define DOE_NATIVE_D3D12_BINDING_INTERNAL_H
#include "d3d12_frontend.h"
#include "../../backends/d3d12/include/doe_d3d12_dynamic.h"

typedef struct native_d3d12_compute {
    size_t buffer_indices[DOE_DX_MAX_BINDINGS], texture_indices[DOE_DX_MAX_BINDINGS];
    size_t sampler_indices[DOE_DX_MAX_DYNAMIC_SAMPLERS];
    bool uniform[DOE_DX_MAX_BINDINGS];
    size_t buffer_count, texture_count, sampler_count;
    uint32_t dispatch_index;
    doe_dx_pipeline *guard;
} native_d3d12_compute;
typedef struct native_d3d12_layout {
    doe_dx_binding_layout buffers[DOE_DX_MAX_BINDINGS];
    doe_dx_texture_binding_layout textures[DOE_DX_MAX_BINDINGS];
    doe_dx_texture_contract contracts[DOE_DX_MAX_BINDINGS];
    doe_dx_sampler_layout samplers[DOE_DX_MAX_DYNAMIC_SAMPLERS];
} native_d3d12_layout;
typedef struct native_d3d12_snapshot {
    doe_dx_buffer_binding buffers[DOE_DX_MAX_BINDINGS];
    doe_dx_texture *textures[DOE_DX_MAX_BINDINGS];
    doe_dx_sampler_desc samplers[DOE_DX_MAX_DYNAMIC_SAMPLERS];
    doe_dx_dynamic_resources values; /* Borrows the arrays in this record. */
    uint32_t metadata_index;
    doe_dx_pipeline *guard; /* Borrowed existing pipeline-owned guard. */
} native_d3d12_snapshot;
/* Typed selected-entry conversion; outputs untouched on failed row/span checks.
 * No shader parsing, source interpretation, native calls or ownership changes. */
bool native_d3d12_stage_bindings(const native_pipeline *pipeline,const native_stage *stage,
    native_d3d12_layout *out,native_d3d12_compute *map);
bool native_d3d12_stage_bindings_for(const native_pipeline *,const native_stage *,uint32_t,
    native_d3d12_layout *,native_d3d12_compute *);
native_failure native_d3d12_binding_values(const native_pipeline *,const native_d3d12_compute *,
    const doe_resource_binding *,native_d3d12_snapshot *);
/* Requires the canonical resource owner's normalized sampler descriptor. The
 * private backend separately admits native descriptor values when recording. */
bool native_d3d12_sampler_descriptor(const WGPUSamplerDescriptor *normalized,doe_dx_sampler_desc *out);
/* Known internal storage kernels use this same reflected register/type owner. */
bool native_d3d12_storage_kernel_layout(const doe_shader_view *,uint32_t,size_t,doe_dx_binding_layout [3]);
/* Resolve canonical command-owned snapshots under existing device serialization.
 * No resource or sampler reference count is created here. */
native_failure native_d3d12_binding_snapshot(const doe_resource_command *record,native_d3d12_snapshot *out);
#endif
