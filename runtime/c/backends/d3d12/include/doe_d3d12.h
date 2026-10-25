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

typedef enum doe_dx_adapter_selection {
    DOE_DX_SELECT_ORDINAL = 0, DOE_DX_SELECT_WARP = 1, DOE_DX_SELECT_LUID = 2, DOE_DX_SELECT_HARDWARE = 3
} doe_dx_adapter_selection;
#define DOE_DX_NO_ADAPTER_ORDINAL UINT32_MAX
typedef struct doe_dx_adapter_identity {
    uint32_t luid_low;
    int32_t luid_high;
    bool software_adapter;
} doe_dx_adapter_identity;
typedef struct doe_dx_options {
    uint32_t adapter_ordinal; /* Exact DXGI ordinal, no adapter fallback. */
    bool allow_software_adapter;
    /* Zero preserves exact ordinal selection. WARP explicitly selects the CPU
     * renderer and requires allow_software_adapter=true and adapter_ordinal=0.
     * No unavailable selection falls back to another adapter. */
    /* HARDWARE scans past software entries, then selects the first hardware
     * identity exactly. Requires ordinal0; device/open failure never retries. */
    doe_dx_adapter_selection adapter_selection;
    /* LUID requires ordinal0 and matches both words and software identity.
     * Other selectors require this record zeroed; software still needs opt-in. */
    doe_dx_adapter_identity adapter_identity;
} doe_dx_options;

/* Raw SDK query facts plus the backend's admitted implementation bounds.
 * No field establishes WebGPU feature/core admission, current free memory,
 * allocation success, or physical qualification. */
typedef struct doe_dx_limits {
    uint32_t max_descriptors, max_static_samplers;
    uint32_t max_cbvs, max_srvs, max_uavs; /* Descriptor-table bounds; UAVs combine all stages. */
    uint32_t max_compute_threads, max_compute_size[3], max_dispatch_groups[3];
    uint32_t max_compute_shared_bytes, uniform_offset_alignment, max_uniform_bytes;
    uint64_t max_buffer_size, max_buffer_allocation_bytes, buffer_allocation_alignment;
} doe_dx_limits;
typedef struct doe_dx_caps {
    uint32_t adapter_ordinal, vendor_id, device_id; /* WARP/LUID use NO_ADAPTER_ORDINAL. */
    uint32_t luid_low;
    int32_t luid_high;
    bool software_adapter;
    char name[256]; /* UTF-8, NUL terminated. */
    doe_dx_adapter_selection adapter_selection;
    bool unified_memory; /* Actual node0 ARCHITECTURE.UMA query. */
    uint32_t feature_level; /* Highest queried profile among FL11_0..FL12_1. */
    uint32_t resource_binding_tier, resource_heap_tier;
    uint32_t gpu_va_bits_per_resource, gpu_va_bits_per_process;
    uint32_t shader_model; /* Queried up to SM6_2; 0 means query unavailable. */
    bool wave_ops, native_16bit_shader_ops;
    bool texture_compression_bc, depth32float_stencil8; /* Complete queried format contracts. */
    uint32_t wave_lane_count_min, wave_lane_count_max; /* Actual OPTIONS1 facts. */
    uint64_t timestamp_frequency; /* Actual direct-queue ticks per second, never a default. */
    doe_dx_limits limits;
} doe_dx_caps;

doe_dx_status doe_dx_context_create(const doe_dx_options *options,
    doe_dx_context **out, doe_dx_error *error);
doe_dx_status doe_dx_context_caps(const doe_dx_context *context, doe_dx_caps *out);
/* Nonmutating; no polling, error consumption or completion settlement. */
doe_dx_status doe_dx_context_status(const doe_dx_context *context);
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

enum { DOE_DX_MAX_BINDINGS = 64, DOE_DX_MAX_STATIC_SAMPLERS = 16 };
typedef enum doe_dx_binding_access { DOE_DX_READ_ONLY, DOE_DX_READ_WRITE } doe_dx_binding_access;
typedef enum doe_dx_buffer_view { DOE_DX_STRUCTURED, DOE_DX_UNIFORM, DOE_DX_RAW } doe_dx_buffer_view;
typedef struct doe_dx_binding_layout {
    uint32_t shader_register;
    uint32_t register_space;
    uint32_t stride; /* STRUCTURED: multiple of 4, 4..2048. UNIFORM/RAW: exactly 0. */
    doe_dx_binding_access access;
    doe_dx_buffer_view view; /* UNIFORM is read-only and uses HLSL b-registers. */
} doe_dx_binding_layout;
typedef struct doe_dx_pipeline_desc {
    const void *bytecode; /* Compiler-validated DXIL/DXBC, copied by native creation. */
    size_t bytecode_size;
    const doe_dx_binding_layout *bindings;
    size_t binding_count;
} doe_dx_pipeline_desc;
/* Uniform bindings require 256-byte aligned offsets and nonzero sizes, a size
 * multiple of 256 up to 65536, and initialized DEFAULT memory including padding.
 * The frontend owns logical uniform lengths and padding; no implicit widening.
 * Compiler/native frontend owns shader reflection, binding identity, workgroup
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
/* Native CopyBufferRegion byte granularity for occupied texture block rows.
 * Public WebGPU buffer-copy alignment remains in its canonical admission owner. */
doe_dx_status doe_dx_commands_copy_bytes(doe_dx_commands *,doe_dx_buffer *,uint64_t,
    doe_dx_buffer *,uint64_t,uint64_t,doe_dx_error *);
doe_dx_status doe_dx_commands_copy(doe_dx_commands *commands,
    doe_dx_buffer *source, uint64_t source_offset, doe_dx_buffer *destination,
    uint64_t destination_offset, uint64_t size, doe_dx_error *error);
/* Binding order is exactly pipeline descriptor order. Repeated read bindings
 * may overlap; writable views of one buffer must be disjoint and cannot mix
 * with read bindings. One command lease/state transition covers each buffer.
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
