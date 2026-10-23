/* SPDX-License-Identifier: MIT */
#ifndef DOE_DX_INTERNAL_H
#define DOE_DX_INTERNAL_H
#include "native.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

struct doe_dx_context {
    doe_dx_native_context *native;
    doe_dx_caps caps;
    size_t objects;
    uint64_t next_fence;
    bool lost;
    doe_dx_commands *pending;
};
struct doe_dx_buffer {
    doe_dx_context *context;
    doe_dx_native_buffer *native;
    doe_dx_commands *owner; /* Borrowed: this command holds a buffer reference. */
    uint32_t references;
    uint64_t size;
    doe_dx_memory memory;
    bool initialized;
};
struct doe_dx_texture {
    doe_dx_context *context;
    doe_dx_native_texture *native;
    doe_dx_commands *owner;
    doe_dx_texture_desc desc;
    uint32_t references;
    bool initialized;
};
struct doe_dx_timestamps {
    doe_dx_context *context;
    doe_dx_native_timestamps *native;
    doe_dx_commands *owner;
    uint32_t references, count;
    uint64_t initialized;
};
struct doe_dx_pipeline {
    doe_dx_context *context;
    doe_dx_native_pipeline *native;
    uint32_t references;
    size_t binding_count, texture_count;
    bool graphics;
    doe_dx_format color_format;
    bool depth_enabled;
    size_t vertex_buffer_count;
    doe_dx_vertex_layout vertex_buffers[DOE_DX_MAX_VERTEX_BUFFERS];
    uint32_t vertex_ends[DOE_DX_MAX_VERTEX_BUFFERS];
    doe_dx_texture_binding_layout textures[DOE_DX_MAX_BINDINGS];
    doe_dx_binding_layout bindings[DOE_DX_MAX_BINDINGS];
};
typedef struct doe_dx_buffer_use {
    doe_dx_buffer *buffer;
    bool initialized; /* Speculative until completion. */
} doe_dx_buffer_use;
typedef struct doe_dx_texture_use {
    doe_dx_texture *texture;
    bool initialized;
} doe_dx_texture_use;
typedef struct doe_dx_timestamp_use {
    doe_dx_timestamps *timestamps;
    uint64_t initialized;
} doe_dx_timestamp_use;
typedef enum doe_dx_command_state {
    DOE_DX_RECORDING, DOE_DX_POISONED, DOE_DX_SUBMITTED, DOE_DX_COMPLETE, DOE_DX_FAILED
} doe_dx_command_state;
struct doe_dx_commands {
    doe_dx_context *context;
    doe_dx_native_commands *native;
    doe_dx_command_state state;
    bool caller_owned;
    uint64_t fence; /* Zero after execution means signal completion is unknown. */
    doe_dx_error failure;
    doe_dx_buffer_use buffers[DOE_DX_MAX_COMMAND_RESOURCES];
    size_t buffer_count;
    doe_dx_texture_use textures[DOE_DX_MAX_COMMAND_RESOURCES];
    size_t texture_count;
    doe_dx_timestamp_use timestamps[DOE_DX_MAX_COMMAND_RESOURCES];
    size_t timestamp_count;
    doe_dx_pipeline *pipelines[DOE_DX_MAX_COMMAND_RESOURCES];
    size_t pipeline_count;
    uint32_t operation_count;
    doe_dx_commands *next;
};

uint32_t doe_dx_vertex_format_size(doe_dx_vertex_format format);
doe_dx_status doe_dx_validate_mesh(const doe_dx_mesh_pipeline_desc *desc, uint32_t *ends);
doe_dx_status doe_dx_prepare_geometry(doe_dx_commands *commands, doe_dx_pipeline *pipeline,
    const doe_dx_compute_resources *resources, const doe_dx_mesh_draw_desc *draw,
    doe_dx_native_geometry *native, size_t *added, doe_dx_error *error);
void doe_dx_use_geometry(doe_dx_commands *commands, const doe_dx_mesh_draw_desc *draw);
doe_dx_status doe_dx_prepare_resources(doe_dx_commands *commands, doe_dx_pipeline *pipeline,
    const doe_dx_compute_resources *resources, doe_dx_native_resources *native,
    size_t *added, doe_dx_error *error);
void doe_dx_use_resources(doe_dx_commands *commands, doe_dx_pipeline *pipeline,
    const doe_dx_compute_resources *resources);
bool doe_dx_range(uint64_t allocation, uint64_t offset, uint64_t size);
doe_dx_status doe_dx_recording(doe_dx_commands *commands, doe_dx_error *error);
doe_dx_status doe_dx_buffer_preflight(doe_dx_commands *commands,
    doe_dx_buffer *buffer, bool readable);
doe_dx_buffer_use *doe_dx_find_buffer(doe_dx_commands *commands, doe_dx_buffer *buffer);
bool doe_dx_format_valid(doe_dx_format format);
doe_dx_status doe_dx_texture_preflight(doe_dx_commands *commands,
    doe_dx_texture *texture, uint32_t usage, bool readable);
doe_dx_texture_use *doe_dx_find_texture(doe_dx_commands *commands, doe_dx_texture *texture);
void doe_dx_use_texture(doe_dx_commands *commands, doe_dx_texture *texture);
void doe_dx_use_buffer(doe_dx_commands *commands, doe_dx_buffer *buffer);
doe_dx_status doe_dx_context_result(doe_dx_context *context, doe_dx_status status,
    const doe_dx_error *error);
void doe_dx_context_lose(doe_dx_context *context, const doe_dx_error *error);
void doe_dx_commands_dispose(doe_dx_commands *commands);
void doe_dx_commands_settle(doe_dx_commands *commands, bool lost, const doe_dx_error *error);
#endif
