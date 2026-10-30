/* SPDX-License-Identifier: MIT */
#ifndef DOE_DX_MOCK_NATIVE_H
#define DOE_DX_MOCK_NATIVE_H
#include "../src/internal.h"
#include <assert.h>

typedef enum mock_failure {
    MOCK_NONE, MOCK_OPEN, MOCK_BUFFER, MOCK_PIPELINE, MOCK_COMMANDS,
    MOCK_DISPATCH, MOCK_FINISH, MOCK_SIGNAL, MOCK_COMPLETED, MOCK_WAIT,
    MOCK_WRITE, MOCK_READ, MOCK_TEXTURE, MOCK_GRAPHICS, MOCK_DRAW, MOCK_TIMESTAMPS, MOCK_FREQUENCY,
    MOCK_DISPATCH_INDIRECT, MOCK_NO_HARDWARE, MOCK_ZERO_READBACK
} mock_failure;
typedef struct mock_state {
    mock_failure failure;
    size_t opens, luid_queries;
    uint32_t query_low;
    int32_t query_high;
    doe_dx_status query_status, close_status;
    bool query_software, override_query_identity;
    doe_dx_adapter_identity query_identity;
    size_t surfaces,presents,surface_transitions,present_execute_count,present_signal_count;
    uint32_t surface_index;
    bool fail_present,surface_acquire_transition;
    doe_dx_texture_view_desc render_view;
    uint32_t render_depth_slice;
    uint32_t immediate_words[16];
    uint32_t logical_size_count,logical_size_indices[DOE_DX_MAX_BINDINGS];
    doe_dx_options open_options;
    bool override_caps;
    doe_dx_caps reported_caps;
    doe_dx_format_caps reported_format_caps;
    doe_dx_format queried_format;
    size_t contexts, buffers, pipelines, commands, textures, timestamps;
    size_t copies, dispatches, finishes, executes, signals, waits, recycled_commands;
    size_t buffer_orders, ordered_buffers, copies_before_order;
    size_t readback_zeroes, lends;
    doe_dx_status readback_zero_status;
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
    size_t render_passes, render_draws;
    doe_dx_render_pipeline_desc last_render_pipeline;
    doe_dx_native_render_depth render_depth;
    bool render_clear,render_discard;
    doe_dx_native_render_draw last_render_draw;
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
