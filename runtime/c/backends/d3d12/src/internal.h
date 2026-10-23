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
struct doe_dx_pipeline {
    doe_dx_context *context;
    doe_dx_native_pipeline *native;
    uint32_t references;
    size_t binding_count;
    doe_dx_binding_layout bindings[DOE_DX_MAX_BINDINGS];
};
typedef struct doe_dx_buffer_use {
    doe_dx_buffer *buffer;
    bool initialized; /* Speculative until completion. */
} doe_dx_buffer_use;
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
    doe_dx_pipeline *pipelines[DOE_DX_MAX_COMMAND_RESOURCES];
    size_t pipeline_count;
    uint32_t operation_count;
    doe_dx_commands *next;
};

bool doe_dx_range(uint64_t allocation, uint64_t offset, uint64_t size);
doe_dx_status doe_dx_recording(doe_dx_commands *commands, doe_dx_error *error);
doe_dx_status doe_dx_buffer_preflight(doe_dx_commands *commands,
    doe_dx_buffer *buffer, bool readable);
doe_dx_buffer_use *doe_dx_find_buffer(doe_dx_commands *commands, doe_dx_buffer *buffer);
void doe_dx_use_buffer(doe_dx_commands *commands, doe_dx_buffer *buffer);
doe_dx_status doe_dx_context_result(doe_dx_context *context, doe_dx_status status,
    const doe_dx_error *error);
void doe_dx_context_lose(doe_dx_context *context, const doe_dx_error *error);
void doe_dx_commands_dispose(doe_dx_commands *commands);
void doe_dx_commands_settle(doe_dx_commands *commands, bool lost, const doe_dx_error *error);
#endif
