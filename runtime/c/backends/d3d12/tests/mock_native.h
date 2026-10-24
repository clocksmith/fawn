/* SPDX-License-Identifier: MIT */
#ifndef DOE_DX_MOCK_NATIVE_H
#define DOE_DX_MOCK_NATIVE_H
#include "../src/internal.h"
#include <assert.h>

typedef enum mock_failure {
    MOCK_NONE, MOCK_OPEN, MOCK_BUFFER, MOCK_PIPELINE, MOCK_COMMANDS,
    MOCK_DISPATCH, MOCK_FINISH, MOCK_SIGNAL, MOCK_COMPLETED, MOCK_WAIT,
    MOCK_WRITE, MOCK_READ, MOCK_TEXTURE, MOCK_GRAPHICS, MOCK_DRAW, MOCK_TIMESTAMPS, MOCK_FREQUENCY,
    MOCK_DISPATCH_INDIRECT, MOCK_NO_HARDWARE
} mock_failure;
typedef struct mock_state {
    mock_failure failure;
    size_t opens;
    doe_dx_options open_options;
    bool override_caps;
    doe_dx_caps reported_caps;
    size_t contexts, buffers, pipelines, commands, textures, timestamps;
    size_t copies, dispatches, finishes, executes, signals, waits;
    size_t allocations, allocation_attempts, allocation_failure;
    uint64_t completed, last_signal;
    bool lost;
    doe_dx_native_buffer *copy_source, *copy_destination;
    uint64_t source_offset, destination_offset, copy_size;
    doe_dx_native_binding bindings[DOE_DX_MAX_BINDINGS];
    size_t binding_count, texture_count, texture_transfers, texture_copies;
    doe_dx_native_texture *texture_bindings[DOE_DX_MAX_BINDINGS];
    doe_dx_native_texture *transfer_texture, *texture_source, *texture_destination;
    doe_dx_native_buffer *transfer_buffer;
    doe_dx_texture_footprint footprint;
    doe_dx_texture_region region;
    uint32_t destination_x, destination_y;
    size_t texture_blits;
    doe_dx_sampler_layout dynamic_layouts[DOE_DX_MAX_DYNAMIC_SAMPLERS];
    doe_dx_sampler_desc dynamic_values[DOE_DX_MAX_DYNAMIC_SAMPLERS];
    size_t dynamic_sampler_count;
    bool upload;
    doe_dx_resource_pipeline_desc pipeline_desc;
    doe_dx_texture_binding_layout texture_layouts[DOE_DX_MAX_BINDINGS];
    doe_dx_static_sampler samplers[DOE_DX_MAX_STATIC_SAMPLERS];
    size_t draws, timestamp_writes, timestamp_resolves;
    doe_dx_native_texture *draw_color;
    doe_dx_draw_desc draw;
    doe_dx_native_geometry geometry;
    doe_dx_index_binding indices;
    size_t vertex_count;
    bool indexed, clear_depth;
    float depth_clear;
    doe_dx_native_timestamps *timestamp_heap;
    uint32_t timestamp_index, resolve_first, resolve_count;
    uint64_t resolve_offset, frequency;
    doe_dx_native_buffer *resolve_buffer;
    uint32_t x, y, z;
    size_t indirect_dispatches;
    doe_dx_native_buffer *indirect_arguments;
    uint64_t indirect_offset;
} mock_state;
extern mock_state mock;
void mock_reset(void);
doe_dx_caps mock_default_caps(void);
void *doe_dx_test_calloc(size_t count, size_t size);
void doe_dx_test_free(void *pointer);
#endif
