/* SPDX-License-Identifier: MIT */
#ifndef DOE_MTL_MOCK_NATIVE_H
#define DOE_MTL_MOCK_NATIVE_H
#include "../src/native.h"
#include <stdatomic.h>

typedef enum mock_operation {
    MOCK_NONE, MOCK_DEVICE, MOCK_QUEUE, MOCK_BUFFER, MOCK_CONTENTS,
    MOCK_PIPELINE, MOCK_COMMAND, MOCK_COPY, MOCK_FILL, MOCK_DISPATCH, MOCK_INDIRECT,
    MOCK_TEXTURE, MOCK_VIEW, MOCK_SAMPLER, MOCK_UPLOAD_IMAGE, MOCK_READ_IMAGE, MOCK_COPY_IMAGE,
    MOCK_RENDER_PIPELINE, MOCK_DEPTH_STATE, MOCK_RENDER, MOCK_TIMESTAMPS, MOCK_TIMESTAMP, MOCK_RESOLVE_TIMESTAMPS,
    MOCK_SHADER_SOURCE, MOCK_HANDLER
} mock_operation;
/* Shared ledger bound: the old 11-element array omitted event kind11,
 * so releasing a test event wrote past its own bookkeeping. */
typedef enum mock_kind {
    MOCK_KIND_DEVICE, MOCK_KIND_QUEUE, MOCK_KIND_BUFFER, MOCK_KIND_PIPELINE,
    MOCK_KIND_COMMAND, MOCK_KIND_TEXTURE, MOCK_KIND_VIEW, MOCK_KIND_SAMPLER,
    MOCK_KIND_RENDER_PIPELINE, MOCK_KIND_DEPTH_STATE, MOCK_KIND_TIMESTAMPS,
    MOCK_KIND_EVENT, MOCK_KIND_COUNT
} mock_kind;
typedef struct mock_object mock_object;
typedef struct mock_native {
    mock_operation fail;
    doe_mtl_status pipeline_failure;
    uint64_t buffer_limit;
    bool etc2, astc, astc_sliced_3d;
    unsigned pipeline_calls, fail_pipeline_at, render_sequence;
    unsigned shader_source_calls;
    unsigned live;
    unsigned created;
    unsigned released;
    unsigned closes;
    unsigned copies;
    unsigned fills;
    unsigned dispatches;
    unsigned indirects;
    unsigned content_queries;
    unsigned operation_sequence;
    unsigned copy_sequence, dispatch_sequence, indirect_sequence;
    void *indirect_arguments;
    size_t indirect_offset;
    void *indirect_pipeline;
    unsigned commits;
    unsigned handlers;
    unsigned polls;
    unsigned waits;
    atomic_uint blocks;
    void (*block_hook)(struct mock_native *, void *, uint64_t);
    unsigned releases_by_kind[MOCK_KIND_COUNT];
    size_t pipeline_max_threads;
    bool unknown_commit;
    char completion_message[256];
    mock_object *last_command;
    void *last_source;
    void *last_destination;
    size_t last_source_offset;
    size_t last_destination_offset;
    size_t last_length;
    uint8_t last_fill;
    doe_mtl_native_binding bindings[DOE_MTL_BUFFER_SLOTS];
    doe_mtl_native_resources resources;
    doe_mtl_native_resources direct_resources;
    doe_mtl_texture_desc texture_desc;
    doe_mtl_texture_view_desc view_desc;
    doe_mtl_sampler_desc sampler_desc;
    doe_mtl_native_texture_copy image_from, image_to;
    unsigned uploads, reads, image_copies;
    unsigned views, fail_view_at;
    doe_mtl_render_pipeline_desc render_pipeline_desc;
    doe_mtl_vertex_layout render_layouts[DOE_MTL_BUFFER_SLOTS];
    doe_mtl_vertex_attribute render_attributes[DOE_MTL_VERTEX_ATTRIBUTES];
    doe_mtl_native_render_pass render_pass;
    doe_mtl_native_render_draw render_draws[2];
    size_t render_draw_count;
    unsigned renders;
    unsigned timestamps, resolves;
    doe_mtl_native_timestamps timestamp_pair;
    uint32_t sizes_slot;
    doe_mtl_dimensions groups;
    doe_mtl_dimensions workgroup;
    uint32_t repeat;
    char entrypoint[64];
} mock_native;

doe_mtl_native mock_driver(mock_native *mock);
void mock_result(mock_native *mock, size_t status, int64_t code);
void mock_command_result(void *command, size_t status, int64_t code);
size_t mock_command_status(void *command);
void mock_assert_closed(const mock_native *mock);
#endif
