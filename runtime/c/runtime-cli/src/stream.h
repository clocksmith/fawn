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
    DOE_COMMAND_TEXTURE_DESTROY
} doe_command_kind;

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

/* A 2D, single-mip, single-sample texture keyed by its stream handle. */
typedef struct doe_texture_desc {
    uint64_t handle;
    uint32_t width, height, depth;
    doe_texture_format format;
    uint64_t usage; /* WebGPU texture usage bits the stream declares. */
} doe_texture_desc;

typedef struct doe_copy_side {
    uint64_t handle;
    doe_texture_desc texture; /* Texture sides only. */
    uint32_t bytes_per_row, rows_per_image; /* 0: undefined (tight). */
    uint64_t offset;
} doe_copy_side;

typedef enum doe_binding_buffer_type {
    DOE_BINDING_UNIFORM,
    DOE_BINDING_READ_ONLY_STORAGE,
    DOE_BINDING_STORAGE
} doe_binding_buffer_type;

typedef struct doe_kernel_binding {
    uint32_t group, binding;
    uint64_t handle;
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

typedef struct doe_command {
    doe_command_kind kind;
    /* The trace row's "command" label: the normalized kind name, as
     * doe-zig-runtime writes it ("buffer_write" for "write_buffer"). Static. */
    const char *label;
    union {
        struct { uint64_t bytes, align_bytes; } upload;
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
    } as;
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

/* The normalized kind tag the trace hashes ("buffer_write" for "write_buffer"). */
const char *doe_command_tag(doe_command_kind kind);
uint32_t doe_texture_format_bytes(doe_texture_format format);
#endif
