/* SPDX-License-Identifier: MIT */
#ifndef DOE_VK_H
#define DOE_VK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Backend-private C ABI, not a replacement for the public WebGPU ABI.
 * All calls for a context and its objects require caller serialization.
 * Output objects are published only on success. No callbacks are invoked.
 * Pointer arguments are borrowed for the duration of a call unless noted. */
typedef struct doe_vk_context doe_vk_context;
typedef struct doe_vk_buffer doe_vk_buffer;
typedef struct doe_vk_pipeline doe_vk_pipeline;
typedef struct doe_vk_commands doe_vk_commands;

typedef enum doe_vk_status {
    DOE_VK_OK,
    DOE_VK_PENDING,
    DOE_VK_TIMEOUT,
    DOE_VK_INVALID_ARGUMENT,
    DOE_VK_OUT_OF_MEMORY,
    DOE_VK_LOADER_UNAVAILABLE,
    DOE_VK_UNSUPPORTED,
    DOE_VK_NATIVE_FAILURE,
    DOE_VK_DEVICE_LOST,
    DOE_VK_BUSY,
    DOE_VK_INVALID_STATE,
    DOE_VK_SURFACE_OUTDATED,
    DOE_VK_SURFACE_LOST
} doe_vk_status;

typedef struct doe_vk_error {
    doe_vk_status status;
    int32_t native_result;
    const char *operation; /* Static string, never shader-location attribution. */
} doe_vk_error;

enum { DOE_VK_SURFACE_WAYLAND = 1, DOE_VK_SURFACE_XCB = 2, DOE_VK_SURFACE_XLIB = 4 };

enum { DOE_VK_UUID_SIZE = 16 };
/* How context creation picks a physical device. None falls back to another. */
typedef enum doe_vk_adapter_selection {
    DOE_VK_SELECT_ORDINAL = 0, /* Exactly adapter_ordinal (default). */
    DOE_VK_SELECT_CLASS,       /* First enumerated CPU device when allow_cpu_adapter,
                                * otherwise the first non-CPU device. */
    DOE_VK_SELECT_IDENTITY     /* The device whose deviceUUID equals device_uuid;
                                * changed enumeration never substitutes hardware. */
} doe_vk_adapter_selection;

typedef struct doe_vk_options {
    uint32_t adapter_ordinal; /* Exact enumeration ordinal, no silent fallback. */
    bool allow_cpu_adapter; /* False by default; software is never GPU evidence. */
    bool require_graphics_queue; /* Select a compute+graphics family or reject. */
    bool enable_shader_demote_if_supported; /* Explicit opt-in; unavailable remains disabled. */
    uint32_t surface_platforms; /* Zero disables WSI; exact required platform bits. */
    doe_vk_adapter_selection selection;
    uint8_t device_uuid[DOE_VK_UUID_SIZE]; /* Required identity for SELECT_IDENTITY. */
} doe_vk_options;

typedef struct doe_vk_caps {
    uint32_t api_version;
    uint32_t driver_version;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t device_type;
    uint32_t adapter_ordinal; /* Actual enumeration index of the selected device. */
    uint8_t device_uuid[DOE_VK_UUID_SIZE]; /* VkPhysicalDeviceIDProperties.deviceUUID. */
    uint32_t queue_family;
    uint32_t queue_flags;
    uint32_t timestamp_valid_bits;
    char device_name[256];
    uint32_t max_workgroup_count[3];
    uint32_t max_workgroup_size[3];
    uint32_t max_workgroup_invocations;
    uint32_t max_storage_buffer_range;
    uint32_t max_uniform_buffer_range;
    uint64_t storage_offset_alignment;
    uint64_t uniform_offset_alignment;
    bool robust_buffer_access;
    bool software_adapter;
    uint32_t max_workgroup_storage_size;
    uint32_t max_bind_groups;
    uint32_t max_bindings_per_group;
    uint32_t max_storage_buffers_per_stage;
    uint32_t max_uniform_buffers_per_stage;
    uint32_t max_sampled_textures_per_stage;
    uint32_t max_storage_textures_per_stage;
    uint32_t max_samplers_per_stage;
    uint32_t max_texture_dimension_1d;
    uint32_t max_texture_dimension_2d;
    uint32_t max_texture_dimension_3d;
    uint32_t max_texture_array_layers;
    float timestamp_period_ns;
    uint32_t max_vertex_input_bindings;
    uint32_t max_vertex_input_attributes;
    uint32_t max_vertex_input_binding_stride;
    uint32_t max_vertex_input_attribute_offset;
    uint32_t max_vertex_output_components;
    uint32_t max_fragment_input_components;
    bool shader_demote_to_helper_invocation; /* Enabled, not merely advertised. */
    uint32_t surface_platforms; /* Enabled with swapchain-maintenance1, not discovery only. */
    /* Enabled fullDrawIndexUint32, not merely advertised. Without it, uint32
     * index draws reject because larger index values exceed the native limit. */
    bool full_draw_index_uint32;
} doe_vk_caps;

/* One owned reference on success. Objects retain their creating context. */
doe_vk_status doe_vk_context_create(const doe_vk_options *options,
                                    doe_vk_context **out, doe_vk_error *error);
doe_vk_status doe_vk_context_retain(doe_vk_context *context);
void doe_vk_context_release(doe_vk_context *context);
bool doe_vk_context_alive(const doe_vk_context *context);
doe_vk_status doe_vk_context_caps(const doe_vk_context *context, doe_vk_caps *out);

/* Usage bits intentionally match the retained Vulkan buffer contract. */
enum {
    DOE_VK_BUFFER_COPY_SRC = 1,
    DOE_VK_BUFFER_COPY_DST = 2,
    DOE_VK_BUFFER_UNIFORM = 16,
    DOE_VK_BUFFER_INDEX = 64,
    DOE_VK_BUFFER_STORAGE = 32,
    DOE_VK_BUFFER_VERTEX = 128,
    DOE_VK_BUFFER_INDIRECT = 256
};
typedef enum doe_vk_memory_kind {
    DOE_VK_MEMORY_HOST,
    DOE_VK_MEMORY_DEVICE
} doe_vk_memory_kind;
doe_vk_status doe_vk_buffer_create(doe_vk_context *context, uint64_t size,
    uint32_t usage, doe_vk_memory_kind memory, doe_vk_buffer **out, doe_vk_error *error);
doe_vk_status doe_vk_buffer_retain(doe_vk_buffer *buffer);
void doe_vk_buffer_release(doe_vk_buffer *buffer);
uint64_t doe_vk_buffer_size(const doe_vk_buffer *buffer);
/* Host memory is required coherent and initialized to zero on creation.
 * Device memory requires explicit fill/copy before reading, as enforced upstream.
 * Host accesses refuse buffers retained by recorded or unresolved GPU commands.
 * Terminal failed submissions poison participating buffer contents. Read/record
 * rejects them until a full host write initializes the allocation; device-local
 * buffers must be replaced. Partial writes cannot make failed output successful. */
doe_vk_status doe_vk_buffer_write(doe_vk_buffer *buffer, uint64_t offset,
    const void *bytes, size_t size);
doe_vk_status doe_vk_buffer_read(doe_vk_buffer *buffer, uint64_t offset,
    void *bytes, size_t size);

typedef enum doe_vk_binding_kind {
    DOE_VK_BINDING_UNIFORM,
    DOE_VK_BINDING_STORAGE,
    DOE_VK_BINDING_SAMPLED_TEXTURE,
    DOE_VK_BINDING_STORAGE_TEXTURE,
    DOE_VK_BINDING_SAMPLER
} doe_vk_binding_kind;
typedef struct doe_vk_binding_layout {
    uint32_t group;
    uint32_t binding;
    doe_vk_binding_kind kind;
} doe_vk_binding_layout;
typedef struct doe_vk_buffer_binding {
    doe_vk_buffer *buffer;
    uint64_t offset;
    uint64_t size;
} doe_vk_buffer_binding;
/* Compiler-validated SPIR-V only; runtime does not infer shader meaning.
 * Layout order defines binding order at dispatch. Groups and slots may be sparse.
 * SPIR-V and entrypoint are consumed synchronously, not borrowed by pipeline. */
doe_vk_status doe_vk_pipeline_create(doe_vk_context *context,
    const uint32_t *spirv, size_t word_count, const char *entrypoint,
    const doe_vk_binding_layout *layout, size_t binding_count,
    doe_vk_pipeline **out, doe_vk_error *error);
doe_vk_status doe_vk_pipeline_retain(doe_vk_pipeline *pipeline);
void doe_vk_pipeline_release(doe_vk_pipeline *pipeline);

/* Commands are one-shot. Recording retains buffers/pipelines immediately.
 * A command failure poisons the recording: discard it, never partially submit.
 * Submit preserves ownership even when completion becomes uncertain.
 * Release returns BUSY for pending/unknown execution and leaves pointer live.
 * Timeout stops waiting only. Wait/poll again, then release terminal commands.
 * Every caller must settle and release commands before releasing its final handle. */
doe_vk_status doe_vk_commands_create(doe_vk_context *context,
    doe_vk_commands **out, doe_vk_error *error);
doe_vk_status doe_vk_commands_copy(doe_vk_commands *commands,
    doe_vk_buffer *source, uint64_t source_offset, doe_vk_buffer *destination,
    uint64_t destination_offset, uint64_t size);
doe_vk_status doe_vk_commands_fill(doe_vk_commands *commands,
    doe_vk_buffer *destination, uint64_t offset, uint64_t size, uint32_t value);
doe_vk_status doe_vk_commands_dispatch(doe_vk_commands *commands,
    doe_vk_pipeline *pipeline, const doe_vk_buffer_binding *bindings,
    size_t binding_count, uint32_t x, uint32_t y, uint32_t z);
doe_vk_status doe_vk_commands_submit(doe_vk_commands *commands, doe_vk_error *error);
doe_vk_status doe_vk_commands_wait(doe_vk_commands *commands,
    uint64_t timeout_ns, doe_vk_error *error);
doe_vk_status doe_vk_commands_poll(doe_vk_commands *commands, doe_vk_error *error);
/* Establish queue idleness after an uncertain submission failure. This has no
 * timeout and may block. The original submission failure remains a failure. */
doe_vk_status doe_vk_commands_recover(doe_vk_commands *commands, doe_vk_error *error);
doe_vk_status doe_vk_commands_release(doe_vk_commands *commands);
const char *doe_vk_status_name(doe_vk_status status);
#ifdef __cplusplus
}
#endif
#endif
