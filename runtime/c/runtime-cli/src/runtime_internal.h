/* SPDX-License-Identifier: MIT
 * Private state of the WebGPU command runtime, shared by its command
 * implementations (execute.c, kernel.c). */
#ifndef DOE_RUNTIME_INTERNAL_H
#define DOE_RUNTIME_INTERNAL_H
#include "execute.h"
#include "webgpu.h"
#include "surface_platform.h"
#include <stddef.h>

enum { MAX_GROUPS = 4, MAX_GROUP_BINDINGS = 64, MAX_KERNEL_SOURCE_BYTES = 16 * 1024 * 1024 };
#define WAIT_TIMEOUT_NS UINT64_C(600000000000) /* ten minutes: a hung queue is a failure, not a pass */

typedef struct buffer_entry {
    uint64_t handle;
    WGPUBuffer buffer;
    uint64_t size;
} buffer_entry;

/* A stream texture: `declared` is the stream's descriptor; the WebGPU texture
 * also carries CopySrc|CopyDst so copies and writes keep its contents. */
typedef struct texture_entry {
    WGPUTexture texture;
    doe_texture_desc declared;
    WGPUTextureUsage usage;
} texture_entry;

typedef struct sampler_entry {
    uint64_t handle;
    WGPUSampler sampler;
} sampler_entry;

/* The default proxy render pipeline for one color target format. */
typedef struct render_pipeline_entry {
    doe_texture_format format;
    WGPURenderPipeline pipeline;
} render_pipeline_entry;

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

typedef struct surface_entry {
    uint64_t handle;
    WGPUSurface surface;
    WGPUTexture acquired;
    doe_surface_host *host;
    bool configured;
} surface_entry;

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
    texture_entry *textures;
    size_t texture_count, texture_capacity;
    sampler_entry *samplers;
    size_t sampler_count, sampler_capacity;
    render_pipeline_entry *render_pipelines;
    size_t render_pipeline_count, render_pipeline_capacity;
    surface_entry *surfaces;
    size_t surface_count, surface_capacity;
    uint64_t uploads_since_wait;
    bool queue_pending, queue_done, queue_wait_failed;
    WGPUQueueWorkDoneStatus queue_status;
    /* The last executed kernel_dispatch (stream-owned bindings), whose pipeline
     * and bindings dispatch and dispatch_indirect reuse. */
    bool has_last_kernel;
    doe_command last_kernel;
    WGPUBuffer indirect_args; /* 16 bytes, Indirect | CopyDst */
    /* A reusable MapRead | CopyDst or MapWrite | CopySrc map_async buffer. */
    WGPUBuffer map_buffer;
    uint64_t map_size;
    bool map_write;
    /* Callback storage outlives a failed wait, through instance release. */
    bool map_done;
    WGPUMapAsyncStatus map_status;
    /* Diagnostics callbacks may remain registered after a failed wait. */
    bool diagnostic_done, diagnostic_ok, diagnostic_abandoned;
    WGPURenderPipeline diagnostic_pipeline;
    bool diagnostic_render_compiled, diagnostic_compute_compiled;
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
bool runtime_after_submit(doe_runtime *runtime, doe_trace_execution *execution);
bool runtime_wait_queue(doe_runtime *runtime, doe_trace_execution *execution);
buffer_entry *runtime_find_buffer(doe_runtime *runtime, uint64_t handle);
buffer_entry *runtime_create_buffer(doe_runtime *runtime, uint64_t handle, uint64_t size, doe_trace_execution *execution);
/* The handle's buffer when it holds `size` bytes; otherwise a new zero-filled
 * buffer replaces it (its contents are lost), as doe-zig-runtime does. The
 * pointer is valid until the next buffer is created. */
buffer_entry *runtime_ensure_buffer(doe_runtime *runtime, uint64_t handle, uint64_t size, doe_trace_execution *execution);
/* Submits and releases the command buffer; waits unless deferred sync is selected. */
bool runtime_submit(doe_runtime *runtime, WGPUCommandBuffer commands, doe_trace_execution *execution);
/* Copies `size` bytes (a multiple of 4) at `offset` of `source` to `out`. */
bool runtime_read_back(doe_runtime *runtime, WGPUBuffer source, uint64_t offset, uint64_t size, uint8_t *out,
    doe_trace_execution *execution);
bool runtime_execute_copy(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution);
bool runtime_execute_texture(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution);
void runtime_release_textures(doe_runtime *runtime);
/* The handle's texture entry, or NULL. */
texture_entry *runtime_find_texture(doe_runtime *runtime, uint64_t handle);
WGPUTexture runtime_binding_texture(doe_runtime *runtime, uint64_t handle, WGPUTextureUsage required, bool has_format,
    doe_texture_format format, doe_texture_format *view_format, doe_trace_execution *execution);
WGPUTextureFormat runtime_webgpu_format(doe_texture_format format);
/* The kernel source by doe-zig-runtime's resolution order; the returned path
 * and *source are the caller's to free. NULL when no candidate is readable. */
char *runtime_resolve_kernel(const doe_runtime *runtime, const char *kernel, char **source, size_t *size);
bool runtime_execute_render(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution);
void runtime_release_surfaces(doe_runtime *runtime);
bool runtime_execute_surface(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution);
bool runtime_execute_diagnostics(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution);
bool runtime_diagnostic_lifecycle(doe_runtime *runtime, doe_texture_format format, doe_trace_execution *execution);
WGPURenderPipeline runtime_diagnostic_pipeline(doe_runtime *runtime, doe_texture_format format, doe_trace_execution *execution);
WGPUTextureView runtime_diagnostic_view(doe_runtime *runtime, doe_texture_format format, doe_trace_execution *execution);
void runtime_release_diagnostics(doe_runtime *runtime);
bool runtime_execute_map(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution);
bool runtime_execute_sampler(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution);
void runtime_release_render(doe_runtime *runtime);
bool runtime_execute_kernel(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution);
/* dispatch and dispatch_indirect (the grid as indirect arguments). */
bool runtime_execute_grid(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution);
bool runtime_validate_oracle(doe_runtime *runtime, const doe_command *command, const doe_output_oracle *oracle,
    char actual_sha256[65], doe_trace_execution *execution);
#endif
