/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_D3D12_INDIRECT_LAYOUT_H
#define DOE_NATIVE_D3D12_INDIRECT_LAYOUT_H
#include "../../shader/include/doe_shader.h"
#include "../../backends/d3d12/include/doe_d3d12.h"
/* Exact compiler-owned guard closure and register metadata. Out is untouched
 * on rejection; no device policy, shader parser or SDK invocation lives here.
 * size_rows receives the size-table row count the entry reads: 0, or 1 for its
 * one canonical resource when the compiler reports STORAGE_SIZES. */
bool native_d3d12_guard_layout(const doe_shader_view *view, doe_dx_binding_layout *out, uint32_t *size_rows);
#endif
