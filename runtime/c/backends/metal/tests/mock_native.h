/* SPDX-License-Identifier: MIT */
#ifndef DOE_MTL_MOCK_NATIVE_H
#define DOE_MTL_MOCK_NATIVE_H
#include "../src/native.h"

typedef enum mock_operation {
    MOCK_NONE, MOCK_DEVICE, MOCK_QUEUE, MOCK_BUFFER, MOCK_CONTENTS,
    MOCK_PIPELINE, MOCK_COMMAND, MOCK_COPY, MOCK_FILL, MOCK_DISPATCH
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
    unsigned releases_by_kind[6];
    size_t pipeline_max_threads;
    bool unknown_commit;
    mock_object *last_command;
    void *last_source;
    void *last_destination;
    size_t last_source_offset;
    size_t last_destination_offset;
    size_t last_length;
    uint8_t last_fill;
    doe_mtl_native_binding bindings[DOE_MTL_BUFFER_SLOTS];
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
