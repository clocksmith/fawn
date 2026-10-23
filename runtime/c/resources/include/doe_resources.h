/* SPDX-License-Identifier: MIT
 * Experimental, authored C resource/recording owner. Not public WebGPU ABI. */
#ifndef DOE_RESOURCES_H
#define DOE_RESOURCES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "doe_instance.h"
#include "doe_labels.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum doe_resource_status {
    DOE_RESOURCE_OK = 0,
    DOE_RESOURCE_INVALID_ARGUMENT,
    DOE_RESOURCE_OUT_OF_MEMORY,
    DOE_RESOURCE_OVERFLOW,
    DOE_RESOURCE_UNSUPPORTED,
    DOE_RESOURCE_DEVICE_MISMATCH,
    DOE_RESOURCE_UNAVAILABLE,
    DOE_RESOURCE_USAGE,
    DOE_RESOURCE_ALIGNMENT,
    DOE_RESOURCE_RANGE,
    DOE_RESOURCE_ALIAS,
    DOE_RESOURCE_LAYOUT,
    DOE_RESOURCE_ASPECT,
    DOE_RESOURCE_STATE,
    DOE_RESOURCE_PENDING,
    DOE_RESOURCE_EXECUTION_FAILED
} doe_resource_status;

/* Logical debug balance belongs to each recording owner. Labels are optional
 * diagnostics handled at the ABI/backend boundary, not retained GPU resources. */
typedef enum doe_resource_debug_operation {
    DOE_RESOURCE_DEBUG_PUSH,
    DOE_RESOURCE_DEBUG_POP,
    DOE_RESOURCE_DEBUG_MARKER
} doe_resource_debug_operation;

/* Copying either lease does not retain it. Constructors acquire the device
 * lease once. Native ownership transfers ONLY on success; otherwise the caller
 * still owns it. Callbacks return normally, may not resurrect finalizing objects,
 * and execute without owner locks. identity denotes the actual creating device. */
typedef struct doe_resource_device {
    void *identity;
    bool (*alive)(void *identity);
    void (*retain)(void *identity);
    void (*release)(void *identity);
} doe_resource_device;

typedef struct doe_resource_native {
    void *handle;
    void *userdata;
    void (*release)(void *userdata, void *handle);
} doe_resource_native;

typedef struct doe_resource_config {
    doe_instance_allocator allocator;
    doe_resource_device device;
    /* Optional existing label owner, borrowed through final resource release.
     * It must remain open. Labels are set through that owner's existing API. */
    doe_label_store *labels;
    /* Requested device limit for viewport admission, copied with this config.
     * Zero selects the WebGPU baseline 8192 for standalone internal callers;
     * public adapters must supply their actual requested device limit. */
    uint32_t max_texture_dimension_2d;
    /* Requested color-attachment limits, copied with this config. Zero selects
     * the WebGPU baselines 8 and 32 for standalone internal callers; public
     * adapters supply their actual requested device limits. Counts above
     * DOE_RESOURCE_MAX_COLOR_ATTACHMENTS are clamped to that storage bound. */
    uint32_t max_color_attachments;
    uint32_t max_color_attachment_bytes_per_sample;
    /* Requested binding-slot limits (WebGPU "exceeds the binding slot limits"):
     * per-stage counts over a layout's entries, and dynamic-offset buffer counts
     * per pipeline layout. Zero selects the WebGPU default for that field. */
    struct {
        uint32_t max_bind_groups;
        uint32_t max_dynamic_uniform_buffers, max_dynamic_storage_buffers;
        uint32_t max_sampled_textures, max_samplers, max_storage_buffers;
        uint32_t max_storage_textures, max_uniform_buffers;
    } slot_limits;
} doe_resource_config;

typedef struct doe_resource_shader doe_resource_shader;
typedef struct doe_resource_buffer doe_resource_buffer;
typedef struct doe_resource_texture doe_resource_texture;
typedef struct doe_resource_view doe_resource_view;
typedef struct doe_resource_sampler doe_resource_sampler;
typedef struct doe_resource_layout doe_resource_layout;
typedef struct doe_resource_pipeline_layout doe_resource_pipeline_layout;
typedef struct doe_resource_bind_group doe_resource_bind_group;
typedef struct doe_resource_pipeline doe_resource_pipeline;
typedef struct doe_resource_encoder doe_resource_encoder;
typedef struct doe_resource_compute_pass doe_resource_compute_pass;
typedef struct doe_resource_commands doe_resource_commands;
typedef struct doe_resource_query_set doe_resource_query_set;
typedef struct doe_resource_external_texture doe_resource_external_texture;
typedef struct doe_resource_render_record doe_resource_render_record;

/* Device-bound identity for an immutable compiler artifact. The adopted payload
 * may have independent sharing, but this is the sole WebGPU shader object lease. */
doe_resource_status doe_resource_shader_create(const doe_resource_config *config,
    doe_resource_native native, doe_resource_shader **out);
void doe_resource_shader_retain(doe_resource_shader *shader);
void doe_resource_shader_release(doe_resource_shader *shader);
void *doe_resource_shader_native(const doe_resource_shader *shader);
void *doe_resource_shader_device(const doe_resource_shader *shader);

/* All constructors leave outputs untouched on failure. Objects require a live
 * authentic pointer, not arbitrary handle probing. NULL release is a no-op.
 * retain/release are atomic; all mutable state and borrowed views require caller
 * serialization. An owned reference must cover every call and borrowed use. */
typedef struct doe_resource_buffer_desc {
    uint64_t size;
    WGPUBufferUsage usage;
    bool mapped_at_creation;
} doe_resource_buffer_desc;

/* Allocation-free preflight for native creators; output descriptors below are
 * normalized transactionally and remain unchanged on failure. */
doe_resource_status doe_resource_buffer_validate(const doe_resource_buffer_desc *desc);
doe_resource_status doe_resource_buffer_create(const doe_resource_config *config,
    const doe_resource_buffer_desc *desc, doe_resource_native native,
    doe_resource_buffer **out);
void doe_resource_buffer_retain(doe_resource_buffer *buffer);
void doe_resource_buffer_release(doe_resource_buffer *buffer);
/* Logical destruction rejects later work; native storage stays owned until all
 * recorded/submission references settle. No implicit GPU wait or completion. */
void doe_resource_buffer_destroy(doe_resource_buffer *buffer);
const doe_resource_buffer_desc *doe_resource_buffer_description(const doe_resource_buffer *buffer);
void *doe_resource_buffer_native(const doe_resource_buffer *buffer);
/* Pure map state/generation admission, not an async callback/future substitute.
 * Matching completion adopts a borrowed mapping at the START of the admitted
 * span, not the allocation base. Stale completion is rejected. Mutable ranges
 * require Write mode; const ranges admit Read or Write.
 * Unmap/destroy invalidates pending generations. The backend owns map/unmap and
 * establishes GPU completion before success. For mapped-at-creation attach the
 * initial borrowed mapping once with map_complete(generation=0, success=true).
 * Completion consumes token zero even when an empty mapping has a NULL pointer. */
doe_resource_status doe_resource_buffer_map_begin(doe_resource_buffer *buffer,
    WGPUMapMode mode, uint64_t offset, uint64_t size, uint64_t *generation);
doe_resource_status doe_resource_buffer_map_complete(doe_resource_buffer *buffer,
    uint64_t generation, bool success, unsigned char *mapped_bytes);
doe_resource_status doe_resource_buffer_mapped_range(doe_resource_buffer *buffer,
    uint64_t offset, uint64_t size, unsigned char **out);
doe_resource_status doe_resource_buffer_mapped_range_const(doe_resource_buffer *buffer,
    uint64_t offset, uint64_t size, const unsigned char **out);
doe_resource_status doe_resource_buffer_unmap(doe_resource_buffer *buffer);

typedef struct doe_resource_texture_desc {
    WGPUTextureDimension dimension;
    WGPUTextureFormat format;
    uint32_t width, height, depth_or_layers;
    uint32_t mip_levels, samples;
    WGPUTextureUsage usage;
    const WGPUTextureFormat *view_formats;
    size_t view_format_count;
} doe_resource_texture_desc;

typedef struct doe_resource_texture_limits {
    uint32_t dimension_1d, dimension_2d, dimension_3d, array_layers;
} doe_resource_texture_limits;

/* Allocation-free named format/aspect classification, before optional feature
 * promotion. Does not imply native format support or attachment renderability.
 * Unknown formats/invalid aspects fail without changing out. */
doe_resource_status doe_resource_format_sample_type(WGPUTextureFormat format,
    WGPUTextureAspect aspect, WGPUTextureSampleType *out);
/* Resolves a present aspect to its effective view format. All preserves format;
 * combined depth/stencil resolves to its depth-only format or Stencil8. */
doe_resource_status doe_resource_format_aspect_format(WGPUTextureFormat format,
    WGPUTextureAspect aspect, WGPUTextureFormat *out);

/* Named color channels, including compressed formats, not renderability.
 * Depth/stencil and unknown formats reject with out unchanged. */
doe_resource_status doe_resource_format_color_components(WGPUTextureFormat format,
    uint32_t *out);

doe_resource_status doe_resource_texture_validate(const doe_resource_texture_desc *desc,
    const doe_resource_texture_limits *limits);
doe_resource_status doe_resource_texture_create(const doe_resource_config *config,
    const doe_resource_texture_desc *desc, const doe_resource_texture_limits *limits,
    doe_resource_native native, doe_resource_texture **out);
void doe_resource_texture_retain(doe_resource_texture *texture);
void doe_resource_texture_release(doe_resource_texture *texture);
void doe_resource_texture_destroy(doe_resource_texture *texture);
const doe_resource_texture_desc *doe_resource_texture_description(const doe_resource_texture *texture);
void *doe_resource_texture_native(const doe_resource_texture *texture);

typedef struct doe_resource_view_desc {
    WGPUTextureFormat format;
    WGPUTextureViewDimension dimension;
    uint32_t base_mip, mip_count, base_layer, layer_count;
    WGPUTextureAspect aspect;
    WGPUTextureUsage usage;
} doe_resource_view_desc;
/* Zero/UINT32_MAX counts select defaults: remaining mips; one layer for 1D,
 * 2D and 3D views, six for Cube, remaining for array views. Other zero enum/usage
 * values select defaults, including the effective aspect format. The native ABI
 * adapter must reject explicit public zero counts before compact conversion.
 * A view retains its texture. */
doe_resource_status doe_resource_view_validate(const doe_resource_texture *texture,
    const doe_resource_view_desc *desc, doe_resource_view_desc *resolved);
doe_resource_status doe_resource_view_create(doe_resource_texture *texture,
    const doe_resource_view_desc *desc, doe_resource_native native,
    doe_resource_view **out);
void doe_resource_view_retain(doe_resource_view *view);
void doe_resource_view_release(doe_resource_view *view);
const doe_resource_view_desc *doe_resource_view_description(const doe_resource_view *view);
void *doe_resource_view_native(const doe_resource_view *view);
doe_resource_texture *doe_resource_view_texture(const doe_resource_view *view);

doe_resource_status doe_resource_sampler_validate(const WGPUSamplerDescriptor *desc,
    WGPUSamplerDescriptor *resolved);
doe_resource_status doe_resource_sampler_create(const doe_resource_config *config,
    const WGPUSamplerDescriptor *desc, doe_resource_native native,
    doe_resource_sampler **out);
void doe_resource_sampler_retain(doe_resource_sampler *sampler);
void doe_resource_sampler_release(doe_resource_sampler *sampler);
void *doe_resource_sampler_native(const doe_resource_sampler *sampler);
/* Borrowed canonical descriptor; label/chains are not retained. */
const WGPUSamplerDescriptor *doe_resource_sampler_description(const doe_resource_sampler *sampler);

/* Stable resource-kind codes match contracts/binding.zig. Binding arrays are
 * explicitly unsupported. External textures preserve their own plane owner. */
typedef enum doe_resource_binding_kind {
    DOE_BIND_BUFFER = 1, DOE_BIND_SAMPLER = 2,
    DOE_BIND_TEXTURE = 3, DOE_BIND_STORAGE_TEXTURE = 4, DOE_BIND_EXTERNAL_TEXTURE = 5
} doe_resource_binding_kind;

typedef struct doe_resource_layout_entry {
    uint32_t binding;
    WGPUShaderStage visibility;
    doe_resource_binding_kind kind;
    WGPUBufferBindingType buffer_type;
    bool dynamic_offset;
    uint64_t minimum_size;
    WGPUSamplerBindingType sampler_type;
    WGPUTextureSampleType sample_type;
    WGPUTextureViewDimension view_dimension;
    bool multisampled;
    WGPUStorageTextureAccess storage_access;
    WGPUTextureFormat storage_format;
} doe_resource_layout_entry;

/* Binding numbers are below MAX_BINDING_NUMBER (WebGPU maxBindingsPerBindGroup)
 * and stored compactly in ascending binding order, never indexed by number. A
 * group holds at most MAX_DYNAMIC_OFFSETS dynamic-offset buffers. */
enum { DOE_RESOURCE_MAX_BINDING_NUMBER = 1000, DOE_RESOURCE_MAX_DYNAMIC_OFFSETS = 32, DOE_RESOURCE_MAX_GROUPS = 4 };
/* Entries copied and canonicalized by binding, duplicates rejected. Automatic
 * layout identity remains exclusive: compatibility requires pointer equality,
 * and only one pipeline can ever claim an automatic layout. */
doe_resource_status doe_resource_layout_create(const doe_resource_config *config,
    const doe_resource_layout_entry *entries, size_t count, bool automatic,
    doe_resource_layout **out);
void doe_resource_layout_retain(doe_resource_layout *layout);
void doe_resource_layout_release(doe_resource_layout *layout);
bool doe_resource_layout_compatible(const doe_resource_layout *a, const doe_resource_layout *b);
const doe_resource_layout_entry *doe_resource_layout_entries(const doe_resource_layout *layout,
    size_t *count);

/* Explicit NULL/empty slots normalize to NULL and never require bound groups.
 * Zero-entry automatic layouts do not acquire pipeline exclusivity. */
doe_resource_status doe_resource_pipeline_layout_create(const doe_resource_config *config,
    doe_resource_layout *const *groups, size_t count, uint32_t immediate_size,
    doe_resource_pipeline_layout **out);
void doe_resource_pipeline_layout_retain(doe_resource_pipeline_layout *layout);
void doe_resource_pipeline_layout_release(doe_resource_pipeline_layout *layout);

typedef struct doe_resource_binding {
    uint32_t binding;
    doe_resource_binding_kind kind;
    union {
        struct { doe_resource_buffer *buffer; uint64_t offset, size; } buffer;
        doe_resource_view *view;
        doe_resource_sampler *sampler;
        doe_resource_external_texture *external_texture;
    } resource;
} doe_resource_binding;

typedef struct doe_resource_binding_limits {
    uint64_t uniform_alignment, storage_alignment;
    uint64_t max_uniform_size, max_storage_size;
} doe_resource_binding_limits;
typedef struct doe_resource_binding_features {
    /* Device-enabled WebGPU feature, not merely native format capability. */
    bool float32_filterable;
} doe_resource_binding_features;

doe_resource_status doe_resource_bind_group_create(doe_resource_layout *layout,
    const doe_resource_binding *bindings, size_t count,
    const doe_resource_binding_limits *limits, doe_resource_bind_group **out);
/* NULL features and the basic constructor disable optional binding features. */
doe_resource_status doe_resource_bind_group_create_with_features(doe_resource_layout *layout,
    const doe_resource_binding *bindings, size_t count,
    const doe_resource_binding_limits *limits, const doe_resource_binding_features *features,
    doe_resource_bind_group **out);
void doe_resource_bind_group_retain(doe_resource_bind_group *group);
void doe_resource_bind_group_release(doe_resource_bind_group *group);
const doe_resource_binding *doe_resource_bind_group_bindings(const doe_resource_bind_group *group,
    size_t *count);
/* Dynamic offsets ordered by ascending binding. Returns resolved copied views,
 * never mutates group. Output stays unchanged on all failures. */
doe_resource_status doe_resource_bind_group_resolve(const doe_resource_bind_group *group,
    const uint32_t *offsets, size_t offset_count,
    doe_resource_binding *out, size_t capacity);
/* Whole-group validation of doe_resource_bind_group_resolve without output:
 * liveness, map state and every dynamic offset's alignment and range. */
doe_resource_status doe_resource_bind_group_check(const doe_resource_bind_group *group,
    const uint32_t *offsets, size_t offset_count);
/* The same validated resolution for one binding number, without materializing
 * the whole group: that binding's liveness, map state and offset only, so
 * callers pair it with doe_resource_bind_group_check. The offsets cover the
 * whole group. Output unchanged on failure. */
doe_resource_status doe_resource_bind_group_resolve_binding(const doe_resource_bind_group *group,
    const uint32_t *offsets, size_t offset_count, uint32_t binding, doe_resource_binding *out);

doe_resource_status doe_resource_pipeline_create(const doe_resource_config *config,
    doe_resource_pipeline_layout *layout, doe_resource_native native,
    const uint32_t workgroup[3], doe_resource_pipeline **out);
void doe_resource_pipeline_retain(doe_resource_pipeline *pipeline);
void doe_resource_pipeline_release(doe_resource_pipeline *pipeline);
void *doe_resource_pipeline_native(const doe_resource_pipeline *pipeline);

typedef enum doe_resource_alias_policy {
    DOE_RESOURCE_DISTINCT_BUFFERS, DOE_RESOURCE_DISJOINT_RANGES
} doe_resource_alias_policy;
typedef struct doe_resource_buffer_copy {
    doe_resource_buffer *source, *destination;
    uint64_t source_offset, destination_offset, size;
} doe_resource_buffer_copy;

doe_resource_status doe_resource_buffer_copy_validate(const void *device,
    const doe_resource_buffer_copy *copy, doe_resource_alias_policy alias,
    uint64_t *resolved_size);

typedef enum doe_resource_copy_direction {
    DOE_RESOURCE_BUFFER_TO_TEXTURE, DOE_RESOURCE_TEXTURE_TO_BUFFER
} doe_resource_copy_direction;
/* NATIVE: zero strides mean tight and a partial block may reach the logical
 * edge (internal uploads). WEBGPU: buffer copies (whole blocks, undefined
 * stride sentinels, 256-byte bytesPerRow). QUEUE: writeTexture, the WEBGPU
 * rules without the 256-byte bytesPerRow alignment. */
typedef enum doe_resource_copy_alignment {
    DOE_RESOURCE_COPY_NATIVE, DOE_RESOURCE_COPY_WEBGPU, DOE_RESOURCE_COPY_QUEUE
} doe_resource_copy_alignment;
typedef struct doe_resource_texture_copy {
    uint64_t offset;
    uint32_t bytes_per_row, rows_per_image, mip;
    uint32_t width, height, depth_or_layers, origin[3];
    WGPUTextureAspect aspect;
} doe_resource_texture_copy;
typedef struct doe_resource_copy_region {
    WGPUTextureAspect aspect;
    uint32_t pitch, image_rows, row_length, image_height;
    uint64_t required_bytes;
} doe_resource_copy_region;

doe_resource_status doe_resource_texture_copy_validate(uint64_t buffer_size,
    const doe_resource_texture_desc *texture, const doe_resource_texture_copy *copy,
    doe_resource_copy_direction direction, doe_resource_copy_alignment alignment,
    doe_resource_copy_region *out);

/* Texture-to-texture copies share the block/mip geometry owner with buffer
 * transfers. Native execution still checks format capabilities and transitions.
 * Same-texture copies require distinct mip or array-layer subresources. */
typedef struct doe_resource_image_copy {
    doe_resource_texture *texture;
    uint32_t mip, origin[3];
    WGPUTextureAspect aspect;
} doe_resource_image_copy;
typedef struct doe_resource_texture_blit {
    doe_resource_image_copy source, destination;
    uint32_t width, height, depth_or_layers;
} doe_resource_texture_blit;
doe_resource_status doe_resource_texture_blit_validate(const void *device,
    const doe_resource_texture_blit *copy, doe_resource_copy_alignment alignment,
    doe_resource_texture_blit *normalized);

typedef struct doe_resource_dispatch_group {
    doe_resource_bind_group *group;
    const uint32_t *dynamic_offsets;
    size_t dynamic_offset_count;
} doe_resource_dispatch_group;

/* Optional immutable command-owned snapshot. Backend borrows it with commands. */
typedef struct doe_resource_dispatch_offsets {
    size_t counts[DOE_RESOURCE_MAX_GROUPS];
    uint32_t values[DOE_RESOURCE_MAX_GROUPS][DOE_RESOURCE_MAX_DYNAMIC_OFFSETS];
} doe_resource_dispatch_offsets;

typedef enum doe_resource_command_kind {
    DOE_COMMAND_COPY_BUFFER, DOE_COMMAND_CLEAR_BUFFER,
    DOE_COMMAND_BUFFER_TEXTURE, DOE_COMMAND_DISPATCH, DOE_COMMAND_WRITE_TIMESTAMP,
    DOE_COMMAND_RESOLVE_QUERY, DOE_COMMAND_BEGIN_OCCLUSION, DOE_COMMAND_RENDER,
    DOE_COMMAND_DISPATCH_INDIRECT, DOE_COMMAND_COPY_TEXTURE, DOE_COMMAND_END_OCCLUSION
} doe_resource_command_kind;
typedef struct doe_resource_command {
    doe_resource_command_kind kind;
    union {
        struct { doe_resource_query_set *set; uint32_t index, position; } timestamp;
        struct {
            doe_resource_query_set *set;
            uint32_t first, count;
            doe_resource_buffer *destination;
            uint64_t offset;
        } resolve;
        doe_resource_render_record *render;
        doe_resource_buffer_copy copy;
        doe_resource_texture_blit blit;
        struct { doe_resource_buffer *buffer; uint64_t offset, size; } clear;
        struct {
            doe_resource_buffer *buffer;
            doe_resource_texture *texture;
            doe_resource_texture_copy copy;
            doe_resource_copy_region region;
            doe_resource_copy_direction direction;
        } texture;
        struct {
            doe_resource_pipeline *pipeline;
            doe_resource_bind_group *groups[DOE_RESOURCE_MAX_GROUPS];
            size_t group_count;
            const doe_resource_dispatch_offsets *dynamic_offsets;
            doe_resource_buffer *indirect;
            uint64_t indirect_offset;
            uint32_t x, y, z;
        } dispatch;
    } value;
} doe_resource_command;

doe_resource_status doe_resource_encoder_create(const doe_resource_config *config,
    doe_resource_encoder **out);
void doe_resource_encoder_retain(doe_resource_encoder *encoder);
void doe_resource_encoder_release(doe_resource_encoder *encoder);
doe_resource_status doe_resource_encoder_copy_buffer(doe_resource_encoder *encoder,
    const doe_resource_buffer_copy *copy, doe_resource_alias_policy alias);
doe_resource_status doe_resource_encoder_clear_buffer(doe_resource_encoder *encoder,
    doe_resource_buffer *buffer, uint64_t offset, uint64_t size);
doe_resource_status doe_resource_encoder_copy_texture(doe_resource_encoder *encoder,
    doe_resource_buffer *buffer, doe_resource_texture *texture,
    const doe_resource_texture_copy *copy, doe_resource_copy_direction direction,
    doe_resource_copy_alignment alignment);
doe_resource_status doe_resource_encoder_blit_texture(doe_resource_encoder *encoder,
    const doe_resource_texture_blit *copy, doe_resource_copy_alignment alignment);
doe_resource_status doe_resource_encoder_dispatch(doe_resource_encoder *encoder,
    doe_resource_pipeline *pipeline, doe_resource_bind_group *const *groups,
    size_t group_count, uint32_t x, uint32_t y, uint32_t z);
/* Validates one complete dispatch usage scope before any recording allocation:
 * whole-buffer read/write compatibility, effective writable-range aliases and
 * texture mip/layer/aspect conflicts across all used groups (including planes).
 * Indirect dispatch includes its argument buffer as an input usage.
 * Copies dynamic offsets and retains groups/pipeline as one recording operation.
 * Backend capability limits and shader dispatch preconditions must still be
 * checked by the consuming execution owner before native queue admission. */
doe_resource_status doe_resource_encoder_dispatch_groups(doe_resource_encoder *encoder,
    doe_resource_pipeline *pipeline, const doe_resource_dispatch_group *groups,
    size_t group_count, uint32_t x, uint32_t y, uint32_t z);
/* Finish moves command ownership, never copies resource leases. A failure is
 * sticky and cannot publish a partial command buffer. Finish always ends the
 * encoder, successful or not (WebGPU "ended" state). */
doe_resource_status doe_resource_encoder_debug(doe_resource_encoder *encoder,
    doe_resource_debug_operation operation);
doe_resource_status doe_resource_encoder_finish(doe_resource_encoder *encoder,
    doe_resource_commands **out);
doe_resource_status doe_resource_encoder_status(const doe_resource_encoder *encoder);
/* WebGPU "ended" states: a command on an ended encoder or pass is reported at
 * the call; any other rejection only invalidates the encoder for Finish. End
 * always closes an open pass, even an invalid one. NULL is not ended. */
bool doe_resource_encoder_ended(const doe_resource_encoder *encoder);
bool doe_resource_compute_pass_ended(const doe_resource_compute_pass *pass);
/* Native descriptor preflight uses this same sticky error state, never a second
 * frontend poison flag. OK/PENDING and out-of-range statuses are not rejection. */
doe_resource_status doe_resource_encoder_invalidate(doe_resource_encoder *encoder,
    doe_resource_status rejection);
void doe_resource_commands_retain(doe_resource_commands *commands);
void doe_resource_commands_release(doe_resource_commands *commands);
const doe_resource_command *doe_resource_commands_borrow(const doe_resource_commands *commands,
    size_t *count);
/* begin_submit validates live dependencies, claims single use and adds an
 * internal submission reference. Call only immediately before backend admission.
 * settle must follow known completion or confirmed never-dispatched failure.
 * PENDING (timeout/cancel/unknown) does not drop ownership or enable reuse.
 * External future/callback delivery remains in existing owners. */
doe_resource_status doe_resource_commands_begin_submit(doe_resource_commands *commands);
doe_resource_status doe_resource_commands_settle(doe_resource_commands *commands,
    doe_resource_status terminal_status);
/* Reusable program ownership for the Doe compute-program extension only; the
 * standard queue path above stays single-use. reserve_program validates fresh,
 * live dependencies and adds one exclusive preparation reference. While reserved,
 * ordinary submit and other program/replay claims reject. publish_program is an
 * allocation-free commit: it converts that reference to permanent program
 * ownership without another retain or dependency validation. abort_program alone
 * returns an unpublished reservation to ordinary eligibility and drops only its
 * preparation reference, even if the device/dependencies became unavailable.
 * Failed publish/abort transitions leave state and references unchanged. Callers
 * serialize the transaction and dependency mutation and finish it exactly once.
 * claim_program is the reserve+publish convenience. Published claims never
 * return to ordinary eligibility, including after release_program. begin_replay
 * revalidates the same live dependencies before each program submission and
 * holds a submission reference; settle_replay follows known completion or a
 * confirmed never-dispatched failure. PENDING keeps the replay open and its own
 * reference; release_program drops only the program reference, so commands
 * with an unresolved replay stay alive until that replay settles. A second
 * release or a new replay after program release rejects without dropping a
 * reference; known settlement still releases the outstanding replay reference. */
doe_resource_status doe_resource_commands_reserve_program(doe_resource_commands *commands);
doe_resource_status doe_resource_commands_publish_program(doe_resource_commands *commands);
doe_resource_status doe_resource_commands_abort_program(doe_resource_commands *commands);
doe_resource_status doe_resource_commands_claim_program(doe_resource_commands *commands);
doe_resource_status doe_resource_commands_begin_replay(doe_resource_commands *commands);
doe_resource_status doe_resource_commands_settle_replay(doe_resource_commands *commands,
    doe_resource_status terminal_status);
doe_resource_status doe_resource_commands_release_program(doe_resource_commands *commands);

/* Typed borrowed identity/description access for native ABI adapters. */
void *doe_resource_buffer_device(const doe_resource_buffer *buffer);
void *doe_resource_texture_device(const doe_resource_texture *texture);
void *doe_resource_view_device(const doe_resource_view *view);
void *doe_resource_sampler_device(const doe_resource_sampler *sampler);
void *doe_resource_layout_device(const doe_resource_layout *layout);
void *doe_resource_pipeline_layout_device(const doe_resource_pipeline_layout *layout);
void *doe_resource_bind_group_device(const doe_resource_bind_group *group);
void *doe_resource_pipeline_device(const doe_resource_pipeline *pipeline);
void *doe_resource_encoder_device(const doe_resource_encoder *encoder);
void *doe_resource_commands_device(const doe_resource_commands *commands);
void *doe_resource_compute_pass_device(const doe_resource_compute_pass *pass);
WGPUBufferMapState doe_resource_buffer_map_state(const doe_resource_buffer *buffer);
WGPUMapMode doe_resource_buffer_map_mode(const doe_resource_buffer *buffer);
doe_resource_status doe_resource_buffer_map_span(const doe_resource_buffer *buffer, uint64_t *offset, uint64_t *size);
doe_resource_status doe_resource_buffer_write_validate(const doe_resource_buffer *buffer,
    const void *device, uint64_t offset, uint64_t size);
size_t doe_resource_pipeline_layout_count(const doe_resource_pipeline_layout *layout);
/* Empty layout slots never require a bound group; invalid indices return false. */
/* Materialize a canonical owner-held empty layout for absent/trailing slots
 * below MAX_GROUPS. Output is borrowed, untouched on failure. Caller serializes
 * this lazy mutation with other device operations; OOM is retriable. */
doe_resource_status doe_resource_pipeline_layout_get_group(doe_resource_pipeline_layout *layout,
    size_t index, doe_resource_layout **out);
/* Empty/NULL layout slots never require a group; invalid index returns false. */
bool doe_resource_pipeline_layout_group_required(const doe_resource_pipeline_layout *layout, size_t index);
doe_resource_layout *doe_resource_pipeline_layout_group(const doe_resource_pipeline_layout *layout,
    size_t index);
doe_resource_pipeline_layout *doe_resource_pipeline_get_layout(const doe_resource_pipeline *pipeline);

doe_resource_status doe_resource_encoder_begin_compute(doe_resource_encoder *encoder,
    doe_resource_compute_pass **out);
void doe_resource_compute_pass_retain(doe_resource_compute_pass *pass);
void doe_resource_compute_pass_release(doe_resource_compute_pass *pass);
/* The WebGPU invalid pass returned after a rejected begin: it holds one encoder
 * lease and records nothing. The encoder must already have failed (NULL
 * otherwise, or on allocation failure), so every pass command, End and Finish
 * report that rejection. */
doe_resource_compute_pass *doe_resource_compute_pass_rejected(doe_resource_encoder *encoder);
doe_resource_status doe_resource_compute_pass_set_pipeline(doe_resource_compute_pass *pass,
    doe_resource_pipeline *pipeline);
/* NULL descriptor, or NULL group with no dynamic offsets, unbinds the slot. */
doe_resource_status doe_resource_compute_pass_set_group(doe_resource_compute_pass *pass,
    uint32_t index, const doe_resource_dispatch_group *group);
doe_resource_status doe_resource_compute_pass_dispatch(doe_resource_compute_pass *pass,
    uint32_t x, uint32_t y, uint32_t z);
doe_resource_status doe_resource_compute_pass_dispatch_indirect(doe_resource_compute_pass *pass,
    doe_resource_buffer *arguments, uint64_t offset);
doe_resource_status doe_resource_encoder_dispatch_indirect(doe_resource_encoder *encoder,
    doe_resource_pipeline *pipeline, const doe_resource_dispatch_group *groups,
    size_t group_count, doe_resource_buffer *arguments, uint64_t offset);
/* ABI preflight failures poison this active pass through the canonical encoder.
 * OK/PENDING/unknown statuses reject without mutating recording state. */
doe_resource_status doe_resource_compute_pass_invalidate(doe_resource_compute_pass *pass,
    doe_resource_status rejection);
doe_resource_status doe_resource_compute_pass_debug(doe_resource_compute_pass *pass,
    doe_resource_debug_operation operation);
doe_resource_status doe_resource_compute_pass_end(doe_resource_compute_pass *pass);

#ifdef __cplusplus
}
#endif
#endif
