/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_D3D12_DRAW_GUARD_H
#define DOE_NATIVE_D3D12_DRAW_GUARD_H
#include "d3d12_graphics_internal.h"
typedef struct native_d3d12_kernel_code native_d3d12_kernel_code;
/* Pure compiler-owned HLSL reflection admission; output unchanged on failure. */
bool native_d3d12_draw_layout(const doe_shader_view *,bool,doe_dx_binding_layout [3]);
/* Compile outside owner locks; consume the shared guard's exact versioned text. */
native_failure native_d3d12_draw_compile(native_device *,bool scan,native_d3d12_kernel_code **);
native_failure native_d3d12_storage_compile(native_device *,const char *,size_t,uint32_t,size_t,native_d3d12_kernel_code **);
void native_d3d12_kernel_code_release(native_d3d12_kernel_code *);
native_failure native_d3d12_kernel_pipeline(native_device *,const native_d3d12_kernel_code *,doe_dx_pipeline **);
/* Caller discards the unsubmitted native command on any recording failure. */
native_failure native_d3d12_draw_prepare(native_command_handle *,const doe_resource_render_packet *,
    doe_dx_render_draw *,native_d3d12_graphics_snapshot *);
#endif
