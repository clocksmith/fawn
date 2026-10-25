/* SPDX-License-Identifier: MIT
 * Private state of the WebGPU command runtime, shared by its command
 * implementations (execute.c, kernel.c). */
#ifndef DOE_RUNTIME_INTERNAL_H
#define DOE_RUNTIME_INTERNAL_H
#include "execute.h"
#include "webgpu.h"
#include <stddef.h>

enum { MAX_GROUPS = 4, MAX_GROUP_BINDINGS = 64, MAX_KERNEL_SOURCE_BYTES = 16 * 1024 * 1024 };
#define WAIT_TIMEOUT_NS UINT64_C(600000000000) /* ten minutes: a hung queue is a failure, not a pass */

typedef struct buffer_entry {
    uint64_t handle;
    WGPUBuffer buffer;
    uint64_t size;
} buffer_entry;

typedef struct pipeline_entry {
    char *source_path;
    char *entry_point;
    /* Binding signature the explicit layout was built from. */
    doe_kernel_binding *signature;
    size_t signature_count;
    WGPUComputePipeline pipeline;
    WGPUBindGroupLayout groups[MAX_GROUPS];
    uint32_t group_count;
} pipeline_entry;

struct doe_runtime {
    doe_runtime_options options;
    WGPUInstance instance;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;
    doe_runtime_adapter info;
    buffer_entry *buffers;
    size_t buffer_count, buffer_capacity;
    pipeline_entry *pipelines;
    size_t pipeline_count, pipeline_capacity;
    uint64_t uploads_since_wait;
    /* The first uncaptured device error since the last command check. */
    bool uncaptured;
    char uncaptured_message[256];
};

bool runtime_fail(doe_trace_execution *execution, const char *code, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
void runtime_copy_view(char *out, size_t size, WGPUStringView view);
bool runtime_wait_future(doe_runtime *runtime, WGPUFuture future);
void runtime_push_scopes(doe_runtime *runtime);
bool runtime_pop_scopes(doe_runtime *runtime, doe_trace_execution *execution);
bool runtime_wait_queue(doe_runtime *runtime, doe_trace_execution *execution);
buffer_entry *runtime_find_buffer(doe_runtime *runtime, uint64_t handle);
buffer_entry *runtime_create_buffer(doe_runtime *runtime, uint64_t handle, uint64_t size, doe_trace_execution *execution);
bool runtime_execute_kernel(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution);
bool runtime_validate_oracle(doe_runtime *runtime, const doe_command *command, const doe_output_oracle *oracle,
    char actual_sha256[65], doe_trace_execution *execution);
#endif
