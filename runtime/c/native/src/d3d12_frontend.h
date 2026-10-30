/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_D3D12_FRONTEND_H
#define DOE_NATIVE_D3D12_FRONTEND_H
#include "internal.h"
#include "../../backends/d3d12/include/doe_d3d12_shader.h"

/* Backend allocations retain their existing private leases; this frontend
 * record owns no reference count, queue, fence or submission state. */
typedef struct native_d3d12_buffer {
    doe_dx_buffer *allocation;
    uint64_t allocation_size;
    doe_dx_memory memory;
    bool zero_deferred; /* Linked on the backend's deferred_zero list. */
    struct native_d3d12_buffer *next_deferred;
} native_d3d12_buffer;
/* Caller holds the device mutex. Records the complete zero fill of every
 * deferred DEFAULT buffer into one command and submits it, without waiting,
 * before any recording, transfer or CPU read can observe those bytes. */
native_failure native_d3d12_materialize_zero(native_device *device);

native_result native_d3d12_open(bool fallback, native_backend *out);
native_result native_d3d12_open_luid(uint32_t low, int32_t high, native_backend *out);
native_result native_d3d12_reopen(const native_backend *adapter, native_backend *out);
void native_d3d12_close(native_backend *backend);
native_result native_d3d12_status(const native_backend *backend);
WGPUStatus native_d3d12_limits(const doe_dx_caps *caps, WGPULimits *out);
native_failure native_d3d12_command_create(const native_backend *backend, native_command_handle *out);
native_failure native_d3d12_command_progress(native_command_handle *commands, uint32_t wait_ms);
native_failure native_d3d12_command_submit(native_command_handle *commands);
bool native_d3d12_command_in_flight(const native_command_handle *commands);
native_failure native_d3d12_wait_owner(native_device *, doe_dx_commands *, const struct timespec *);
native_failure native_d3d12_record_batch(native_device *, native_command_handle *, doe_resource_commands **, size_t);
native_result native_d3d12_command_release(native_command_handle *commands);
native_failure native_d3d12_buffer_create(native_buffer *buffer, uint64_t size, uint32_t usage,
    bool direct_readback);
void native_d3d12_buffer_close(native_buffer *buffer);
native_failure native_d3d12_buffer_read(native_buffer *buffer, uint64_t offset, void *bytes, size_t size);
native_failure native_d3d12_buffer_write(native_buffer *buffer, uint64_t offset, const void *bytes, size_t size);
native_failure native_d3d12_record_commands(native_command_handle *commands, doe_resource_commands *resources);
native_failure native_d3d12_dispatch(native_command_handle *commands, const doe_resource_command *record);
/* backend is the device's own backend; it retains each selected compiler module. */
native_failure native_d3d12_compile_stage(native_backend *backend,const native_stage *stage,doe_dx_shader **out);
native_failure native_d3d12_compile_vertex(native_backend *backend,const native_stage *,uint32_t interstage_mask,doe_dx_shader **);
native_failure native_d3d12_compile_compute(native_backend *backend,const native_stage *stage, doe_dx_shader **out);
void native_d3d12_compiler_release(doe_dx_shader *shader);
native_failure native_d3d12_pipeline_create(native_pipeline *pipeline, const native_stage *stage,
    const doe_dx_shader *shader);
void native_d3d12_pipeline_close(native_pipeline *pipeline);
#endif
