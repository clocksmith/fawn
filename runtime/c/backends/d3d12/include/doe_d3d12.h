/* SPDX-License-Identifier: MIT */
#ifndef DOE_D3D12_H
#define DOE_D3D12_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Private backend ABI, not WebGPU validation or a provider-selection policy.
 * Serialize every call for a context and its objects. No callbacks are invoked.
 * Inputs are borrowed for the call. Constructors leave *out unchanged on failure.
 * Context destruction is explicit and refuses outstanding objects/submissions. */
typedef struct doe_dx_context doe_dx_context;
typedef struct doe_dx_buffer doe_dx_buffer;
typedef struct doe_dx_pipeline doe_dx_pipeline;
typedef struct doe_dx_commands doe_dx_commands;

typedef enum doe_dx_status {
    DOE_DX_OK,
    DOE_DX_PENDING,
    DOE_DX_COMPLETION_UNKNOWN,
    DOE_DX_INVALID_ARGUMENT,
    DOE_DX_INVALID_STATE,
    DOE_DX_RANGE,
    DOE_DX_ALIGNMENT,
    DOE_DX_OUT_OF_MEMORY,
    DOE_DX_UNAVAILABLE,
    DOE_DX_UNSUPPORTED,
    DOE_DX_NATIVE_FAILURE,
    DOE_DX_DEVICE_LOST,
    DOE_DX_DEVICE_MISMATCH,
    DOE_DX_BUSY,
    DOE_DX_UNINITIALIZED
} doe_dx_status;

typedef struct doe_dx_error {
    doe_dx_status status;
    int32_t native_code; /* HRESULT or Win32 error, identified by operation. */
    const char *operation; /* Static string. */
} doe_dx_error;

typedef struct doe_dx_options {
    uint32_t adapter_ordinal; /* Exact DXGI ordinal, no adapter fallback. */
    bool allow_software_adapter;
} doe_dx_options;

/* Adapter identity only, not public WebGPU capability admission. */
typedef struct doe_dx_caps {
    uint32_t adapter_ordinal, vendor_id, device_id;
    uint32_t luid_low;
    int32_t luid_high;
    bool software_adapter;
    char name[256]; /* UTF-8, NUL terminated. */
} doe_dx_caps;

doe_dx_status doe_dx_context_create(const doe_dx_options *options,
    doe_dx_context **out, doe_dx_error *error);
doe_dx_status doe_dx_context_caps(const doe_dx_context *context, doe_dx_caps *out);
/* Refuses destruction with BUSY until all caller objects have been released and
 * all submissions retired. On BUSY the context remains usable for polling. */
doe_dx_status doe_dx_context_destroy(doe_dx_context *context);
/* A finite native wait; zero only polls. Timeout is PENDING, not completion.
 * Re-signals a failed post-execute signal to establish an ordered retirement
 * point. Unknown completion retains all objects; device loss settles failure. */
doe_dx_status doe_dx_context_poll(doe_dx_context *context, uint32_t wait_ms,
    doe_dx_error *error);

typedef enum doe_dx_memory {
    DOE_DX_DEFAULT, DOE_DX_UPLOAD, DOE_DX_READBACK
} doe_dx_memory;
/* Upload memory is zero initialized. Default/readback memory requires a full
 * copy before it may be read; a compute shader is not assumed to write all bytes.
 * Default buffers support structured SRV/UAVs; upload/readback are transfer-only. */
doe_dx_status doe_dx_buffer_create(doe_dx_context *context, uint64_t size,
    doe_dx_memory memory, doe_dx_buffer **out, doe_dx_error *error);
doe_dx_status doe_dx_buffer_retain(doe_dx_buffer *buffer);
void doe_dx_buffer_release(doe_dx_buffer *buffer);
uint64_t doe_dx_buffer_size(const doe_dx_buffer *buffer);
/* Host accesses reject recorded or unresolved GPU use. Reads require completed
 * initialization. Outputs are unchanged on failure. No persistent map escapes. */
doe_dx_status doe_dx_buffer_write(doe_dx_buffer *buffer, uint64_t offset,
    const void *bytes, size_t size, doe_dx_error *error);
doe_dx_status doe_dx_buffer_read(doe_dx_buffer *buffer, uint64_t offset,
    void *bytes, size_t size, doe_dx_error *error);

enum { DOE_DX_MAX_BINDINGS = 16, DOE_DX_MAX_COMMAND_RESOURCES = 64 };
typedef enum doe_dx_binding_access { DOE_DX_READ_ONLY, DOE_DX_READ_WRITE } doe_dx_binding_access;
typedef struct doe_dx_binding_layout {
    uint32_t shader_register;
    uint32_t register_space;
    uint32_t stride; /* Structured element size: multiple of 4, 4..2048. */
    doe_dx_binding_access access;
} doe_dx_binding_layout;
typedef struct doe_dx_pipeline_desc {
    const void *bytecode; /* Compiler-validated DXIL/DXBC, copied by native creation. */
    size_t bytecode_size;
    const doe_dx_binding_layout *bindings;
    size_t binding_count;
} doe_dx_pipeline_desc;
/* Compiler/native frontend owns shader reflection, binding identity, workgroup
 * limits and bounds-proof obligations. This owner checks native ranges only. */
doe_dx_status doe_dx_pipeline_create(doe_dx_context *context,
    const doe_dx_pipeline_desc *desc, doe_dx_pipeline **out, doe_dx_error *error);
doe_dx_status doe_dx_pipeline_retain(doe_dx_pipeline *pipeline);
void doe_dx_pipeline_release(doe_dx_pipeline *pipeline);

typedef struct doe_dx_buffer_binding {
    doe_dx_buffer *buffer;
    uint64_t offset, size;
} doe_dx_buffer_binding;

doe_dx_status doe_dx_commands_create(doe_dx_context *context,
    doe_dx_commands **out, doe_dx_error *error);
/* Single caller ownership. Release discards unsubmitted commands. Submitted
 * commands remain owned by the context even after this caller release. */
void doe_dx_commands_release(doe_dx_commands *commands);
doe_dx_status doe_dx_commands_copy(doe_dx_commands *commands,
    doe_dx_buffer *source, uint64_t source_offset, doe_dx_buffer *destination,
    uint64_t destination_offset, uint64_t size, doe_dx_error *error);
/* Binding order is exactly pipeline descriptor order; aliasing is rejected.
 * All buffers must be initialized. Dimensions are 1..65535 independently. */
doe_dx_status doe_dx_commands_dispatch(doe_dx_commands *commands,
    doe_dx_pipeline *pipeline, const doe_dx_buffer_binding *bindings,
    size_t binding_count, uint32_t x, uint32_t y, uint32_t z, doe_dx_error *error);
/* Submit once. A post-execute signal failure returns COMPLETION_UNKNOWN and
 * still transfers GPU ownership to the context. Never resubmit that command. */
doe_dx_status doe_dx_commands_submit(doe_dx_commands *commands, doe_dx_error *error);
/* Does not poll; context_poll advances completion. */
doe_dx_status doe_dx_commands_result(const doe_dx_commands *commands, doe_dx_error *error);

#ifdef __cplusplus
}
#endif
#endif
