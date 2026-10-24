/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_D3D12_INDIRECT_H
#define DOE_NATIVE_D3D12_INDIRECT_H
#include "d3d12_frontend.h"
#include "../../backends/d3d12/include/doe_d3d12_texture.h"
#include "d3d12_indirect_layout.h"

typedef struct native_d3d12_guard_code native_d3d12_guard_code;
/* Compile/release outside every owner lock. The existing pipeline call lease
 * protects the device and selected provider throughout this preparation. */
native_failure native_d3d12_guard_compile(native_device *device, native_d3d12_guard_code **out);
void native_d3d12_guard_code_release(native_d3d12_guard_code *code);
/* Context serialized; native creation copies compiled bytes and the layout. */
native_failure native_d3d12_guard_pipeline(native_device *device,
    const native_d3d12_guard_code *code, doe_dx_pipeline **out);
/* On any recording failure the caller discards the entire unsubmitted native
 * command through the existing QueueSubmit/HostReplay failure path. */
native_failure native_d3d12_dispatch_indirect(native_command_handle *commands,
    const doe_resource_command *record);

/* Borrowed direct/indirect binding conversion on the existing pipeline owner. */
native_failure native_d3d12_resolve_bindings(const doe_resource_command *record,
    doe_dx_buffer_binding bindings[DOE_DX_MAX_BINDINGS], size_t *count,
    uint32_t *metadata_index, doe_dx_pipeline **guard);
#endif
