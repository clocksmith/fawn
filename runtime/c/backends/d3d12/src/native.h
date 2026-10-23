/* SPDX-License-Identifier: MIT */
#ifndef DOE_DX_NATIVE_H
#define DOE_DX_NATIVE_H
#include "../include/doe_d3d12.h"
#if defined(DOE_DX_REQUIRE_WINDOWS) && !defined(_WIN32)
#error "The native D3D12 build requires a Windows target and actual Windows SDK."
#endif

/* Private typed platform calls. Windows implements these using SDK C COM
 * declarations, not reconstructed vtables. Host tests replace these symbols at
 * link time and only prove ownership/ordering, never SDK or GPU behavior. */
typedef struct doe_dx_native_context doe_dx_native_context;
typedef struct doe_dx_native_buffer doe_dx_native_buffer;
typedef struct doe_dx_native_pipeline doe_dx_native_pipeline;
typedef struct doe_dx_native_commands doe_dx_native_commands;
typedef struct doe_dx_native_binding {
    doe_dx_native_buffer *buffer;
    uint64_t offset, size;
} doe_dx_native_binding;

doe_dx_status doe_dx_fail(doe_dx_status status, int32_t native_code,
    const char *operation, doe_dx_error *error);
doe_dx_status doe_dx_native_open(const doe_dx_options *options,
    doe_dx_native_context **out, doe_dx_caps *caps, doe_dx_error *error);
void doe_dx_native_close(doe_dx_native_context *context);
doe_dx_status doe_dx_native_buffer_create(doe_dx_native_context *context,
    uint64_t size, doe_dx_memory memory, doe_dx_native_buffer **out, doe_dx_error *error);
void doe_dx_native_buffer_destroy(doe_dx_native_buffer *buffer);
doe_dx_status doe_dx_native_write(doe_dx_native_buffer *buffer, uint64_t offset,
    const void *bytes, size_t size, doe_dx_error *error);
doe_dx_status doe_dx_native_read(doe_dx_native_buffer *buffer, uint64_t offset,
    void *bytes, size_t size, doe_dx_error *error);
doe_dx_status doe_dx_native_pipeline_create(doe_dx_native_context *context,
    const doe_dx_pipeline_desc *desc, doe_dx_native_pipeline **out, doe_dx_error *error);
void doe_dx_native_pipeline_destroy(doe_dx_native_pipeline *pipeline);
doe_dx_status doe_dx_native_commands_create(doe_dx_native_context *context,
    doe_dx_native_commands **out, doe_dx_error *error);
void doe_dx_native_commands_destroy(doe_dx_native_commands *commands);
void doe_dx_native_copy(doe_dx_native_commands *commands, doe_dx_native_buffer *source,
    uint64_t source_offset, doe_dx_native_buffer *destination,
    uint64_t destination_offset, uint64_t size);
/* Allocation failure is before recording; only successful calls append work. */
doe_dx_status doe_dx_native_dispatch(doe_dx_native_commands *commands,
    doe_dx_native_pipeline *pipeline, const doe_dx_native_binding *bindings,
    size_t count, uint32_t x, uint32_t y, uint32_t z, doe_dx_error *error);
doe_dx_status doe_dx_native_finish(doe_dx_native_commands *commands, doe_dx_error *error);
void doe_dx_native_execute(doe_dx_native_context *context, doe_dx_native_commands *commands);
doe_dx_status doe_dx_native_signal(doe_dx_native_context *context, uint64_t value,
    doe_dx_error *error);
/* DEVICE_LOST must mean terminal device removal, not an arbitrary HRESULT. */
doe_dx_status doe_dx_native_completed(doe_dx_native_context *context, uint64_t *out,
    doe_dx_error *error);
doe_dx_status doe_dx_native_wait(doe_dx_native_context *context, uint64_t value,
    uint32_t wait_ms, doe_dx_error *error);
#endif
