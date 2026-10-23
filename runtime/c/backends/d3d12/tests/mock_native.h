/* SPDX-License-Identifier: MIT */
#ifndef DOE_DX_MOCK_NATIVE_H
#define DOE_DX_MOCK_NATIVE_H
#include "../src/internal.h"
#include <assert.h>

typedef enum mock_failure {
    MOCK_NONE, MOCK_OPEN, MOCK_BUFFER, MOCK_PIPELINE, MOCK_COMMANDS,
    MOCK_DISPATCH, MOCK_FINISH, MOCK_SIGNAL, MOCK_COMPLETED, MOCK_WAIT,
    MOCK_WRITE, MOCK_READ
} mock_failure;
typedef struct mock_state {
    mock_failure failure;
    size_t contexts, buffers, pipelines, commands;
    size_t copies, dispatches, finishes, executes, signals, waits;
    size_t allocations, allocation_attempts, allocation_failure;
    uint64_t completed, last_signal;
    bool lost;
    doe_dx_native_buffer *copy_source, *copy_destination;
    uint64_t source_offset, destination_offset, copy_size;
    doe_dx_native_binding bindings[DOE_DX_MAX_BINDINGS];
    size_t binding_count;
    uint32_t x, y, z;
} mock_state;
extern mock_state mock;
void mock_reset(void);
void *doe_dx_test_calloc(size_t count, size_t size);
void doe_dx_test_free(void *pointer);
#endif
