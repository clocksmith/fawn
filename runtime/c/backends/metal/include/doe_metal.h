/* SPDX-License-Identifier: MIT */
#ifndef DOE_METAL_H
#define DOE_METAL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Backend-private native boundary, not the WebGPU object/reference authority.
 * Calls for a context and its objects require caller serialization. A live owned
 * reference covers every call. Inputs are borrowed for the call; constructors
 * leave *out unchanged on failure. No foreign callbacks or implicit GPU waits. */
typedef struct doe_mtl_context doe_mtl_context;
typedef struct doe_mtl_buffer doe_mtl_buffer;
typedef struct doe_mtl_pipeline doe_mtl_pipeline;
typedef struct doe_mtl_commands doe_mtl_commands;

typedef enum doe_mtl_status {
    DOE_MTL_OK,
    DOE_MTL_PENDING,
    DOE_MTL_COMPLETION_UNKNOWN,
    DOE_MTL_INVALID_ARGUMENT,
    DOE_MTL_OUT_OF_MEMORY,
    DOE_MTL_LOADER_UNAVAILABLE,
    DOE_MTL_UNSUPPORTED,
    DOE_MTL_NATIVE_FAILURE,
    DOE_MTL_INVALID_STATE,
    DOE_MTL_BUSY,
    DOE_MTL_RANGE,
    DOE_MTL_ALIGNMENT,
    DOE_MTL_DEVICE_MISMATCH,
    DOE_MTL_UNINITIALIZED,
    DOE_MTL_EXECUTION_FAILED
} doe_mtl_status;

typedef struct doe_mtl_error {
    doe_mtl_status status;
    int64_t native_code;
    const char *operation; /* Static name; no invented shader attribution. */
    char message[256];
} doe_mtl_error;

typedef struct doe_mtl_dimensions { uint32_t x, y, z; } doe_mtl_dimensions;
typedef struct doe_mtl_caps {
    uint64_t max_buffer_length;
    uint64_t max_threads_per_dimension[3];
    char device_name[256];
    uint32_t max_texture_1d, max_texture_2d, max_texture_3d, max_texture_layers;
    bool bc_texture_compression;
    uint32_t sample_count_mask; /* Bit log2(count), native queried counts 1..16. */
    bool depth24_stencil8, separate_depth_stencil;
    bool depth_resolve, stencil_resolve, store_and_resolve, multisample_array;
    uint32_t max_visibility_offset;
    bool counting_visibility;
} doe_mtl_caps;

doe_mtl_status doe_mtl_context_create(doe_mtl_context **out, doe_mtl_error *error);
doe_mtl_status doe_mtl_context_retain(doe_mtl_context *context);
void doe_mtl_context_release(doe_mtl_context *context);
doe_mtl_status doe_mtl_context_caps(const doe_mtl_context *context, doe_mtl_caps *out);
/* Native handles below are borrowed only. Do not release them, change their
 * state, or submit work using them behind this owner's lifetime bookkeeping. */
void *doe_mtl_context_device(const doe_mtl_context *context);

typedef enum doe_mtl_memory { DOE_MTL_SHARED, DOE_MTL_PRIVATE } doe_mtl_memory;
/* Shared allocations are zero-initialized. Private allocations need a full
 * recorded fill/copy before use. The WebGPU owner still owns usage/map policy. */
doe_mtl_status doe_mtl_buffer_create(doe_mtl_context *context, uint64_t size,
    doe_mtl_memory memory, doe_mtl_buffer **out, doe_mtl_error *error);
doe_mtl_status doe_mtl_buffer_retain(doe_mtl_buffer *buffer);
void doe_mtl_buffer_release(doe_mtl_buffer *buffer);
uint64_t doe_mtl_buffer_size(const doe_mtl_buffer *buffer);
void *doe_mtl_buffer_native(const doe_mtl_buffer *buffer);
/* Host accesses reject every recorded or unresolved GPU use. Failed GPU work
 * poisons contents; only a full host rewrite (shared) or replacement (private)
 * recovers them. Read outputs are untouched on any failure. */
doe_mtl_status doe_mtl_buffer_write(doe_mtl_buffer *buffer, uint64_t offset,
    const void *bytes, size_t size);
doe_mtl_status doe_mtl_buffer_read(doe_mtl_buffer *buffer, uint64_t offset,
    void *bytes, size_t size);

enum { DOE_MTL_BUFFER_SLOTS = 31 };
/* Compiler/native validation owns shader meaning, layout, usage and robust
 * access. This primitive consumes validated MSL, never WGSL or source patterns.
 * workgroup is the compiler-resolved size, with no guessed fallback.
 * sizes_slot is UINT32_MAX if unused; otherwise the compiler-selected slot
 * receives uint32_t bound lengths as in the retained checked Metal bridge. */
typedef struct doe_mtl_pipeline_desc {
    const char *msl;
    size_t msl_length;
    const char *entrypoint;
    doe_mtl_dimensions workgroup;
    uint32_t sizes_slot;
} doe_mtl_pipeline_desc;
doe_mtl_status doe_mtl_pipeline_create(doe_mtl_context *context,
    const doe_mtl_pipeline_desc *desc, doe_mtl_pipeline **out, doe_mtl_error *error);
doe_mtl_status doe_mtl_pipeline_retain(doe_mtl_pipeline *pipeline);
void doe_mtl_pipeline_release(doe_mtl_pipeline *pipeline);
void *doe_mtl_pipeline_native(const doe_mtl_pipeline *pipeline);

typedef struct doe_mtl_buffer_binding {
    uint32_t slot;
    doe_mtl_buffer *buffer;
    uint64_t offset;
    uint64_t size;
} doe_mtl_buffer_binding;

/* Commands are one-shot. Native command buffers use unretained references;
 * this backend retains the actual buffers/pipelines through terminal status.
 * Buffers have one unresolved recording owner, including unsubmitted commands.
 * A recording failure poisons the entire command: release, never partial submit.
 * Terminal errors authorize retirement, NOT successful readback. Pending/unknown
 * release returns BUSY and does not consume the command pointer. No cancellation
 * API pretends to establish GPU completion. Settle and release every command. */
doe_mtl_status doe_mtl_commands_create(doe_mtl_context *context,
    doe_mtl_commands **out, doe_mtl_error *error);
doe_mtl_status doe_mtl_commands_copy(doe_mtl_commands *commands,
    doe_mtl_buffer *source, uint64_t source_offset, doe_mtl_buffer *destination,
    uint64_t destination_offset, uint64_t size, doe_mtl_error *error);
doe_mtl_status doe_mtl_commands_fill(doe_mtl_commands *commands,
    doe_mtl_buffer *destination, uint64_t offset, uint64_t size, uint8_t value,
    doe_mtl_error *error);
doe_mtl_status doe_mtl_commands_dispatch(doe_mtl_commands *commands,
    doe_mtl_pipeline *pipeline, const doe_mtl_buffer_binding *bindings,
    size_t count, doe_mtl_dimensions groups, uint32_t repeat, doe_mtl_error *error);
/* Indirect GPU counts need a separate native admission/validation contract.
 * This entrypoint explicitly rejects rather than dispatching unchecked counts. */
doe_mtl_status doe_mtl_commands_dispatch_indirect(doe_mtl_commands *commands,
    doe_mtl_error *error);
doe_mtl_status doe_mtl_commands_submit(doe_mtl_commands *commands, doe_mtl_error *error);
doe_mtl_status doe_mtl_commands_poll(doe_mtl_commands *commands, doe_mtl_error *error);
/* Blocking native wait, not a bounded operational timeout. Unknown retains all
 * obligations. Poll is the nonblocking primitive for the shared future owner. */
doe_mtl_status doe_mtl_commands_wait(doe_mtl_commands *commands, doe_mtl_error *error);
doe_mtl_status doe_mtl_commands_release(doe_mtl_commands *commands);
const char *doe_mtl_status_name(doe_mtl_status status);
#ifdef __cplusplus
}
#endif
#endif
