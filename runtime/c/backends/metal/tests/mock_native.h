/* SPDX-License-Identifier: MIT */
#ifndef DOE_MTL_MOCK_NATIVE_H
#define DOE_MTL_MOCK_NATIVE_H
#include "../src/native.h"

typedef enum mock_operation {
    MOCK_NONE, MOCK_DEVICE, MOCK_QUEUE, MOCK_BUFFER, MOCK_CONTENTS,
    MOCK_PIPELINE, MOCK_COMMAND, MOCK_COPY, MOCK_FILL, MOCK_DISPATCH,
    MOCK_TEXTURE, MOCK_VIEW, MOCK_SAMPLER, MOCK_UPLOAD_IMAGE, MOCK_READ_IMAGE, MOCK_COPY_IMAGE,
    MOCK_RENDER_PIPELINE, MOCK_DEPTH_STATE, MOCK_RENDER, MOCK_TIMESTAMPS, MOCK_TIMESTAMP, MOCK_RESOLVE_TIMESTAMPS
} mock_operation;
typedef struct mock_object mock_object;
typedef struct mock_native {
    mock_operation fail;
    unsigned live;
    unsigned created;
    unsigned released;
    unsigned closes;
    unsigned copies;
    unsigned fills;
    unsigned dispatches;
    unsigned commits;
    unsigned polls;
    unsigned waits;
    unsigned releases_by_kind[11];
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
    doe_mtl_texture_desc texture_desc;
    doe_mtl_texture_view_desc view_desc;
    doe_mtl_sampler_desc sampler_desc;
    doe_mtl_native_texture_copy image_from, image_to;
    unsigned uploads, reads, image_copies;
    unsigned views, fail_view_at;
    doe_mtl_render_pipeline_desc render_pipeline_desc;
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
void mock_assert_closed(const mock_native *mock);
#endif
