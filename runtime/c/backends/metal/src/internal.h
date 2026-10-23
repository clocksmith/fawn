/* SPDX-License-Identifier: MIT */
#ifndef DOE_MTL_INTERNAL_H
#define DOE_MTL_INTERNAL_H
#include "native.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

struct doe_mtl_context {
    uint32_t references;
    doe_mtl_native native;
    void *device;
    void *queue;
    doe_mtl_caps caps;
};
struct doe_mtl_buffer {
    doe_mtl_context *context;
    uint32_t references;
    void *native;
    size_t size;
    doe_mtl_memory memory;
    bool initialized;
    bool poisoned;
    doe_mtl_commands *owner; /* Borrowed; this command owns a buffer reference. */
};
struct doe_mtl_pipeline {
    doe_mtl_context *context;
    uint32_t references;
    void *native;
    doe_mtl_dimensions workgroup;
    uint32_t sizes_slot;
};
typedef struct doe_mtl_buffer_use {
    doe_mtl_buffer *buffer;
    bool initialized; /* Speculative until successful native completion. */
    struct doe_mtl_buffer_use *next;
} doe_mtl_buffer_use;
typedef struct doe_mtl_pipeline_use {
    doe_mtl_pipeline *pipeline;
    struct doe_mtl_pipeline_use *next;
} doe_mtl_pipeline_use;
typedef enum doe_mtl_command_state {
    DOE_MTL_RECORDING, DOE_MTL_POISONED, DOE_MTL_SUBMITTED,
    DOE_MTL_COMPLETE, DOE_MTL_FAILED
} doe_mtl_command_state;
struct doe_mtl_commands {
    doe_mtl_context *context;
    void *native;
    doe_mtl_command_state state;
    doe_mtl_error failure;
    doe_mtl_buffer_use *buffers;
    doe_mtl_pipeline_use *pipelines;
    struct doe_mtl_texture_use *textures;
    struct doe_mtl_view_use *views;
    struct doe_mtl_sampler_use *samplers;
};
doe_mtl_status doe_mtl_native_record_result(doe_mtl_commands *commands,
    doe_mtl_status status, const doe_mtl_error *detail, doe_mtl_error *error);
doe_mtl_status doe_mtl_buffer_preflight(const doe_mtl_commands *commands,
    const doe_mtl_buffer *buffer, bool readable);
bool doe_mtl_range(size_t allocation, uint64_t offset, uint64_t size);
doe_mtl_status doe_mtl_recording(doe_mtl_commands *commands, doe_mtl_error *error);
doe_mtl_status doe_mtl_poison(doe_mtl_commands *commands, doe_mtl_status status,
    const char *operation, doe_mtl_error *error);
doe_mtl_status doe_mtl_use_buffer(doe_mtl_commands *commands,
    doe_mtl_buffer *buffer, doe_mtl_buffer_use **out);
bool doe_mtl_buffer_initialized(const doe_mtl_commands *commands,
    const doe_mtl_buffer *buffer);
#endif
