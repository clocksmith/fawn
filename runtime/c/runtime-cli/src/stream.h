/* SPDX-License-Identifier: MIT
 * Typed runtime command streams: the doe-zig-runtime JSON command format
 * (runtime/zig/src/command) for the kinds this C runtime executes. Every other
 * command kind and every field these kinds do not implement is rejected with
 * the command index, never ignored. */
#ifndef DOE_RUNTIME_STREAM_H
#define DOE_RUNTIME_STREAM_H
#include "json.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum doe_command_kind {
    DOE_COMMAND_UPLOAD,
    DOE_COMMAND_BUFFER_WRITE,
    DOE_COMMAND_BARRIER,
    DOE_COMMAND_KERNEL_DISPATCH,
    DOE_COMMAND_COPY, /* Every copy direction; doe-zig-runtime's copy_buffer_to_texture kind. */
    DOE_COMMAND_TEXTURE_WRITE,
    DOE_COMMAND_TEXTURE_QUERY,
    DOE_COMMAND_TEXTURE_DESTROY,
    DOE_COMMAND_RENDER_DRAW, /* render_draw, draw, draw_call, draw_indexed */
    DOE_COMMAND_RENDER_PASS,
    DOE_COMMAND_SAMPLER_CREATE,
    DOE_COMMAND_SAMPLER_DESTROY,
    DOE_COMMAND_DISPATCH,         /* dispatch, dispatch_workgroups, dispatch_invocations */
    DOE_COMMAND_DISPATCH_INDIRECT,
    DOE_COMMAND_DRAW_INDIRECT,        /* render_draw recorded as one indirect draw per draw */
    DOE_COMMAND_DRAW_INDEXED_INDIRECT, /* the indexed form */
    DOE_COMMAND_MAP_ASYNC,            /* flat kind aliases or nested map_async */
    DOE_COMMAND_ASYNC_DIAGNOSTICS,
    DOE_COMMAND_SURFACE_CREATE,
    DOE_COMMAND_SURFACE_CAPABILITIES,
    DOE_COMMAND_SURFACE_CONFIGURE,
    DOE_COMMAND_SURFACE_ACQUIRE,
    DOE_COMMAND_SURFACE_PRESENT,
    DOE_COMMAND_SURFACE_UNCONFIGURE,
    DOE_COMMAND_SURFACE_RELEASE
} doe_command_kind;

/* The kinds whose payload is doe_render_draw. */
static inline bool doe_command_is_render(doe_command_kind kind)
{
    return kind == DOE_COMMAND_RENDER_DRAW || kind == DOE_COMMAND_RENDER_PASS || kind == DOE_COMMAND_DRAW_INDIRECT ||
        kind == DOE_COMMAND_DRAW_INDEXED_INDIRECT;
}

typedef enum doe_copy_direction {
    DOE_COPY_BUFFER_TO_BUFFER,
    DOE_COPY_BUFFER_TO_TEXTURE,
    DOE_COPY_TEXTURE_TO_BUFFER,
    DOE_COPY_TEXTURE_TO_TEXTURE
} doe_copy_direction;

/* Uncompressed color formats this runtime creates; every other is rejected. */
typedef enum doe_texture_format {
    DOE_FORMAT_R8_UNORM,
    DOE_FORMAT_RG8_UNORM,
    DOE_FORMAT_RGBA8_UNORM,
    DOE_FORMAT_RGBA8_UNORM_SRGB,
    DOE_FORMAT_BGRA8_UNORM,
    DOE_FORMAT_R32_UINT,
    DOE_FORMAT_R32_FLOAT,
    DOE_FORMAT_RGBA16_FLOAT,
    DOE_FORMAT_RGBA32_FLOAT
} doe_texture_format;

typedef enum doe_diagnostic_mode {
    DOE_DIAG_PIPELINE, DOE_DIAG_CAPABILITIES, DOE_DIAG_RESOURCE_TABLE,
    DOE_DIAG_LIFECYCLE, DOE_DIAG_PIXEL_LOCAL, DOE_DIAG_FULL
} doe_diagnostic_mode;

typedef struct doe_diagnostics {
    doe_diagnostic_mode mode;
    doe_texture_format format;
    uint32_t iterations;
    bool emulate; /* Explicit retained featurePolicy, never an implicit fallback. */
} doe_diagnostics;

/* A 2D, single-sample texture keyed by its stream handle. */
typedef struct doe_texture_desc {
    uint64_t handle;
    uint32_t width, height, depth;
    uint32_t mip_levels; /* At least this many mip levels (0: one). */
    doe_texture_format format;
    uint64_t usage; /* WebGPU texture usage bits the stream declares. */
} doe_texture_desc;

typedef struct doe_copy_side {
    uint64_t handle;
    doe_texture_desc texture; /* Texture sides only. */
    uint32_t bytes_per_row, rows_per_image; /* 0: undefined (tight). */
    uint64_t offset;
    uint32_t mip_level; /* Texture sides: the copied level, whose extent the copy uses. */
} doe_copy_side;

typedef enum doe_binding_buffer_type {
    DOE_BINDING_UNIFORM,
    DOE_BINDING_READ_ONLY_STORAGE,
    DOE_BINDING_STORAGE
} doe_binding_buffer_type;

typedef enum doe_binding_resource {
    DOE_RESOURCE_BUFFER,
    DOE_RESOURCE_TEXTURE,        /* a sampled 2D texture view */
    DOE_RESOURCE_STORAGE_TEXTURE /* a 2D storage texture view */
} doe_binding_resource;

typedef struct doe_kernel_binding {
    uint32_t group, binding;
    uint64_t handle;
    doe_binding_resource resource;
    /* Textures: WGPUTextureSampleType or WGPUStorageTextureAccess values, and
     * the view format (has_format false: the texture's own). */
    uint32_t sample_type, storage_access;
    bool has_format;
    doe_texture_format format;
    doe_binding_buffer_type buffer_type;
    uint64_t buffer_offset;
    uint64_t buffer_size; /* UINT64_MAX: the whole buffer. */
} doe_kernel_binding;

/* texture_query: each expectation is checked only when the command names it. */
typedef struct doe_texture_query {
    uint64_t handle;
    bool has_width, has_height, has_depth, has_format, has_dimension, has_view_dimension, has_sample_count, has_usage;
    uint32_t width, height, depth, sample_count;
    doe_texture_format format;
    bool dimension_2d;
    int view_dimension; /* 0 "2d", 1 "2d-array", 2 any other view dimension. */
    uint64_t usage;
} doe_texture_query;

/* render_draw and render_pass: `draw_count` draws of the default fullscreen
 * proxy pipeline into one color target, as doe-zig-runtime's native lane. */
typedef struct doe_render_draw {
    uint32_t draw_count, vertex_count, instance_count, first_vertex, first_instance;
    bool indexed, index_uint32;
    uint32_t *indices; /* Owned. */
    size_t index_data_count;
    uint32_t index_count, first_index;
    int32_t base_vertex;
    uint64_t target_handle; /* 0: a scratch target created for the command. */
    uint32_t target_width, target_height;
    doe_texture_format target_format;
    bool has_viewport_width, has_viewport_height, has_scissor_width, has_scissor_height;
    bool has_blend_constant, has_stencil_reference;
    float viewport_x, viewport_y, viewport_width, viewport_height, viewport_min_depth, viewport_max_depth;
    uint32_t scissor_x, scissor_y, scissor_width, scissor_height;
    double blend[4];
    uint32_t stencil_reference;
} doe_render_draw;

/* sampler_create: WebGPU enum values as doe-zig-runtime parses them. */
typedef struct doe_sampler_desc {
    uint64_t handle;
    uint32_t address_mode[3], mag_filter, min_filter, mipmap_filter, compare;
    float lod_min_clamp, lod_max_clamp;
    uint16_t max_anisotropy;
} doe_sampler_desc;

/* Semantic operation identity on a kernel_dispatch (doe-zig-runtime's
 * contracts/semantic.zig SemanticContext) and its optional buffer capture.
 * Strings are document-owned; NULL is absent. */
typedef struct doe_semantic {
    const char *op_id, *stage, *phase, *execution_plan_hash;
    bool has_token_index, has_layer_index;
    uint32_t token_index, layer_index;
    /* Bytes [capture_offset, capture_offset + capture_size) of the capture
     * buffer after the command, written next to the operator record. */
    bool has_capture;
    uint64_t capture_handle, capture_offset, capture_size;
} doe_semantic;

typedef struct doe_surface {
    uint64_t handle, usage;
    uint32_t width, height, alpha_mode, present_mode;
    doe_texture_format format;
} doe_surface;

typedef struct doe_command {
    doe_command_kind kind;
    /* The trace row's "command" label: the normalized kind name, as
     * doe-zig-runtime writes it ("buffer_write" for "write_buffer"). Static. */
    const char *label;
    union {
        struct { uint64_t bytes, align_bytes; } upload;
        struct { uint64_t bytes; bool write; } map;
        doe_diagnostics diagnostics;
        doe_surface surface;
        struct {
            uint64_t handle, offset, buffer_size;
            uint32_t *words;
            size_t word_count;
        } buffer_write;
        struct { uint64_t dependency_count; } barrier;
        struct {
            const char *kernel;
            const char *entry_point; /* "main" unless the command names one. */
            uint32_t x, y, z;
            uint64_t repeat, warmup;
            /* repeatSynchronization "independent": the repeats do not read
             * each other's results (doe-zig-runtime omits Vulkan barriers). */
            bool repeat_independent;
            bool initialize_buffers;
            doe_kernel_binding *bindings;
            size_t binding_count;
            /* The command's output_oracle object (document-owned), checked
             * only when oracle validation is requested, as doe-zig-runtime does. */
            const doe_json_value *oracle;
        } dispatch;
        struct {
            doe_copy_direction direction;
            doe_copy_side src, dst;
            uint64_t bytes;
            /* Set by an active-mode quirk: buffer-to-texture and texture-to-texture
             * copies go through a staging buffer of this alignment. */
            bool temporary_buffer;
            uint32_t temporary_buffer_alignment;
        } copy;
        struct {
            doe_texture_desc texture;
            uint32_t bytes_per_row, rows_per_image; /* 0: tight rows, the texture height. */
            uint64_t offset;
            uint8_t *data;
            size_t data_size; /* 0: create the texture only. */
        } texture_write;
        doe_texture_query texture_query;
        struct { uint64_t handle; } texture_destroy;
        doe_render_draw render;
        doe_sampler_desc sampler;
        struct { uint64_t handle; } sampler_destroy;
        /* dispatch and dispatch_indirect: a grid for the last kernel_dispatch's
         * pipeline and bindings, else dispatch_noop.wgsl, as doe-zig-runtime does. */
        struct { uint32_t x, y, z; } grid;
    } as;
    doe_semantic semantic; /* kernel_dispatch only; all absent elsewhere. */
} doe_command;

typedef struct doe_stream {
    doe_json_document document;
    doe_command *commands;
    size_t count;
} doe_stream;

typedef struct doe_stream_error {
    size_t command_index; /* SIZE_MAX for a whole-document failure. */
    char message[256];
} doe_stream_error;

/* Parses a JSON array of commands, or an object with a "commands" array. On
 * failure *stream is left empty and error names the first rejected command. */
bool doe_stream_parse(const char *text, size_t size, doe_stream *stream, doe_stream_error *error);
void doe_stream_free(doe_stream *stream);
/* An isolated-dispatch SHA-256 output oracle (schema version 1): the binding
 * after `dispatch_count` dispatches on zero-filled buffers. */
typedef struct doe_output_oracle {
    uint32_t group, binding;
    uint64_t dispatch_count;
    char expected_sha256[65];
    const char *reference_id;
} doe_output_oracle;

/* Reads a kernel_dispatch output_oracle; false with a reason for every
 * schema, kind, scope or initialization this runtime does not implement. */
bool doe_output_oracle_parse(const doe_json_value *value, doe_output_oracle *out, char *message, size_t message_size);

/* True when any semantic identity field is present (a capture alone is not one). */
bool doe_semantic_present(const doe_semantic *semantic);

/* The normalized kind tag the trace hashes ("buffer_write" for "write_buffer"). */
const char *doe_command_tag(doe_command_kind kind);
uint32_t doe_texture_format_bytes(doe_texture_format format);
#endif
