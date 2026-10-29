/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_D3D12_INDIRECT_H
#define DOE_NATIVE_D3D12_INDIRECT_H
#include "d3d12_frontend.h"
#include "../../backends/d3d12/include/doe_d3d12_texture.h"
#include "d3d12_indirect_layout.h"

typedef struct native_d3d12_guard_code native_d3d12_guard_code;
/* Compile/release outside every owner lock; the initializing call retains the
 * existing device lease until serialized publication or rejection completes. */
native_failure native_d3d12_guard_compile(native_device *device, native_d3d12_guard_code **out);
void native_d3d12_guard_code_release(native_d3d12_guard_code *code);
/* Context serialized; native creation copies compiled bytes and the layout. */
native_failure native_d3d12_guard_pipeline(native_device *device,
    const native_d3d12_guard_code *code, doe_dx_pipeline **out);
/* Prepare once on the first admitted indirect command, never a direct pipeline.
 * Concurrent callers may duplicate immutable compilation, never native PSOs. */
native_failure native_d3d12_guard_initialize(native_device *device);
void native_d3d12_guard_reset(native_device *device);
/* On any recording failure the caller discards the entire unsubmitted native
 * command through the existing QueueSubmit/HostReplay failure path. */
native_failure native_d3d12_dispatch_indirect(native_command_handle *commands,
    const doe_resource_command *record);

/* Borrowed direct/indirect binding conversion on the existing pipeline owner. */
#endif
