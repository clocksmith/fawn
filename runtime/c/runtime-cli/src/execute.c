/* SPDX-License-Identifier: MIT */
#include "runtime_internal.h"
#include "doe_native_adapter.h" /* doeNativeAdapterGetPciIdentity: the driver version webgpu.h does not report. */
/* Weak: another webgpu.h provider (Dawn) has no Doe extension; the driver
 * version is then 0, unreported. */
#pragma weak doeNativeAdapterGetPciIdentity
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

uint64_t doe_runtime_monotonic_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

void runtime_copy_view(char *out, size_t size, WGPUStringView view)
{
    size_t length = view.data ? (view.length == WGPU_STRLEN ? strlen(view.data) : view.length) : 0;
    if (length >= size) length = size - 1;
    if (length) memcpy(out, view.data, length);
    out[length] = '\0';
}

bool runtime_fail(doe_trace_execution *execution, const char *code, const char *format, ...)
{
    execution->ok = false;
    execution->status_code = code;
    va_list args;
    va_start(args, format);
    vsnprintf(execution->message, sizeof(execution->message), format, args);
    va_end(args);
    return false;
}

typedef struct request_reply {
    int done;
    int status;
    void *object;
    char message[256];
} request_reply;

static void adapter_reply(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message,
    void *userdata1, void *userdata2)
{
    (void)userdata2;
    request_reply *reply = userdata1;
    reply->done = 1;
    reply->status = (int)status;
    reply->object = adapter;
    runtime_copy_view(reply->message, sizeof(reply->message), message);
}

static void device_reply(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message,
    void *userdata1, void *userdata2)
{
    (void)userdata2;
    request_reply *reply = userdata1;
    reply->done = 1;
    reply->status = (int)status;
    reply->object = device;
    runtime_copy_view(reply->message, sizeof(reply->message), message);
}

static void uncaptured_error(WGPUDevice const *device, WGPUErrorType type, WGPUStringView message,
    void *userdata1, void *userdata2)
{
    (void)device;
    (void)type;
    (void)userdata2;
    doe_runtime *runtime = userdata1;
    if (runtime->uncaptured) return;
    runtime->uncaptured = true;
    runtime_copy_view(runtime->uncaptured_message, sizeof(runtime->uncaptured_message), message);
}

/* Polls with zero-timeout waits, which need no TimedWaitAny instance feature,
 * until the future completes or the wait bound elapses. */
bool runtime_wait_future(doe_runtime *runtime, WGPUFuture future)
{
    if (!future.id) return false;
    uint64_t deadline = doe_runtime_monotonic_ns() + WAIT_TIMEOUT_NS;
    for (;;) {
        WGPUFutureWaitInfo wait = {.future = future};
        WGPUWaitStatus status = wgpuInstanceWaitAny(runtime->instance, 1, &wait, 0);
        if (status == WGPUWaitStatus_Success && wait.completed) return true;
        if (status != WGPUWaitStatus_TimedOut || doe_runtime_monotonic_ns() > deadline) return false;
        struct timespec pause = {.tv_sec = 0, .tv_nsec = 20000};
        nanosleep(&pause, NULL);
    }
}

static WGPUAdapter request_adapter(doe_runtime *runtime, bool fallback, char *message, size_t message_size)
{
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.backendType = WGPUBackendType_Vulkan;
    options.forceFallbackAdapter = fallback ? WGPU_TRUE : WGPU_FALSE;
    request_reply reply = {0};
    WGPURequestAdapterCallbackInfo callback = {.mode = WGPUCallbackMode_WaitAnyOnly, .callback = adapter_reply,
        .userdata1 = &reply};
    if (!runtime_wait_future(runtime, wgpuInstanceRequestAdapter(runtime->instance, &options, callback)) || !reply.done) {
        snprintf(message, message_size, "adapter request did not complete");
        return NULL;
    }
    if (reply.status != (int)WGPURequestAdapterStatus_Success || !reply.object) {
        snprintf(message, message_size, "no %s Vulkan adapter: %.180s", fallback ? "fallback" : "nonfallback", reply.message);
        return NULL;
    }
    return reply.object;
}

static void record_adapter(doe_runtime *runtime)
{
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(runtime->adapter, &info) != WGPUStatus_Success) return;
    runtime_copy_view(runtime->info.vendor, sizeof(runtime->info.vendor), info.vendor);
    runtime_copy_view(runtime->info.architecture, sizeof(runtime->info.architecture), info.architecture);
    runtime_copy_view(runtime->info.device, sizeof(runtime->info.device), info.device);
    runtime_copy_view(runtime->info.description, sizeof(runtime->info.description), info.description);
    runtime->info.vendor_id = info.vendorID;
    runtime->info.device_id = info.deviceID;
    if (doeNativeAdapterGetPciIdentity) doeNativeAdapterGetPciIdentity(runtime->adapter, NULL, NULL, &runtime->info.driver_version);
    switch (info.adapterType) {
    case WGPUAdapterType_DiscreteGPU: runtime->info.adapter_type = "discrete_gpu"; break;
    case WGPUAdapterType_IntegratedGPU: runtime->info.adapter_type = "integrated_gpu"; break;
    case WGPUAdapterType_CPU: runtime->info.adapter_type = "cpu"; break;
    default: runtime->info.adapter_type = "unknown"; break;
    }
    runtime->info.backend = info.backendType == WGPUBackendType_Vulkan ? "vulkan" :
        info.backendType == WGPUBackendType_Metal ? "metal" : info.backendType == WGPUBackendType_D3D12 ? "d3d12" : "unknown";
    wgpuAdapterInfoFreeMembers(info);
}

doe_runtime *doe_runtime_open(const doe_runtime_options *options, char *message, size_t message_size)
{
    doe_runtime *runtime = calloc(1, sizeof(*runtime));
    if (!runtime) {
        snprintf(message, message_size, "out of memory");
        return NULL;
    }
    runtime->options = *options;
    runtime->info.adapter_type = "unknown";
    runtime->info.backend = "unknown";
    runtime->instance = wgpuCreateInstance(NULL);
    if (!runtime->instance) {
        snprintf(message, message_size, "wgpuCreateInstance failed");
        doe_runtime_close(runtime);
        return NULL;
    }
    char reason[256] = "";
    if (options->adapter != DOE_ADAPTER_SOFTWARE) runtime->adapter = request_adapter(runtime, false, reason, sizeof(reason));
    if (!runtime->adapter && options->adapter != DOE_ADAPTER_PHYSICAL)
        runtime->adapter = request_adapter(runtime, true, reason, sizeof(reason));
    if (!runtime->adapter) {
        snprintf(message, message_size, "%.250s", reason);
        doe_runtime_close(runtime);
        return NULL;
    }
    record_adapter(runtime);
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    /* Buffer size limits at the adapter's maxima, as doe-zig-runtime's native
     * lane allocates and binds against device capabilities (the 256 MiB
     * matrix-vector weights exceed WebGPU's 128 MiB default binding limit).
     * Every other limit stays at the WebGPU default. */
    WGPULimits adapter_limits = WGPU_LIMITS_INIT, required = WGPU_LIMITS_INIT;
    if (wgpuAdapterGetLimits(runtime->adapter, &adapter_limits) == WGPUStatus_Success) {
        required.maxStorageBufferBindingSize = adapter_limits.maxStorageBufferBindingSize;
        required.maxBufferSize = adapter_limits.maxBufferSize;
        descriptor.requiredLimits = &required;
    }
    descriptor.uncapturedErrorCallbackInfo = (WGPUUncapturedErrorCallbackInfo){.callback = uncaptured_error,
        .userdata1 = runtime};
    request_reply reply = {0};
    WGPURequestDeviceCallbackInfo callback = {.mode = WGPUCallbackMode_WaitAnyOnly, .callback = device_reply,
        .userdata1 = &reply};
    if (!runtime_wait_future(runtime, wgpuAdapterRequestDevice(runtime->adapter, &descriptor, callback)) ||
        reply.status != (int)WGPURequestDeviceStatus_Success || !reply.object) {
        snprintf(message, message_size, "device request failed: %.200s", reply.message);
        doe_runtime_close(runtime);
        return NULL;
    }
    runtime->device = reply.object;
    runtime->queue = wgpuDeviceGetQueue(runtime->device);
    return runtime;
}

const doe_runtime_adapter *doe_runtime_adapter_info(const doe_runtime *runtime)
{
    return &runtime->info;
}

void doe_runtime_close(doe_runtime *runtime)
{
    if (!runtime) return;
    for (size_t i = 0; i < runtime->pipeline_count; i++) {
        pipeline_entry *entry = &runtime->pipelines[i];
        if (entry->pipeline) wgpuComputePipelineRelease(entry->pipeline);
        for (uint32_t g = 0; g < entry->group_count; g++) if (entry->groups[g]) wgpuBindGroupLayoutRelease(entry->groups[g]);
        free(entry->source_path);
        free(entry->entry_point);
        free(entry->signature);
    }
    free(runtime->pipelines);
    runtime_release_render(runtime);
    runtime_release_diagnostics(runtime);
    runtime_release_textures(runtime);
    for (size_t i = 0; i < runtime->buffer_count; i++) wgpuBufferRelease(runtime->buffers[i].buffer);
    free(runtime->buffers);
    if (runtime->indirect_args) wgpuBufferRelease(runtime->indirect_args);
    if (runtime->map_buffer) wgpuBufferRelease(runtime->map_buffer);
    if (runtime->queue) wgpuQueueRelease(runtime->queue);
    if (runtime->device) wgpuDeviceRelease(runtime->device);
    if (runtime->adapter) wgpuAdapterRelease(runtime->adapter);
    if (runtime->instance) wgpuInstanceRelease(runtime->instance);
    free(runtime);
}

/* Pushes validation, out-of-memory and internal scopes around one command. */
void runtime_push_scopes(doe_runtime *runtime)
{
    wgpuDevicePushErrorScope(runtime->device, WGPUErrorFilter_Internal);
    wgpuDevicePushErrorScope(runtime->device, WGPUErrorFilter_OutOfMemory);
    wgpuDevicePushErrorScope(runtime->device, WGPUErrorFilter_Validation);
    runtime->uncaptured = false;
}

typedef struct scope_reply {
    bool done, error;
    char message[256];
} scope_reply;

static void scope_popped(WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message,
    void *userdata1, void *userdata2)
{
    (void)userdata2;
    scope_reply *reply = userdata1;
    reply->done = true;
    if (status != WGPUPopErrorScopeStatus_Success || type != WGPUErrorType_NoError) {
        reply->error = true;
        runtime_copy_view(reply->message, sizeof(reply->message), message);
        if (!reply->message[0]) snprintf(reply->message, sizeof(reply->message), "error scope reported type %d", (int)type);
    }
}

/* Pops the three scopes; the first reported error wins. */
bool runtime_pop_scopes(doe_runtime *runtime, doe_trace_execution *execution)
{
    bool ok = true;
    for (int i = 0; i < 3; i++) {
        scope_reply reply = {0};
        WGPUPopErrorScopeCallbackInfo callback = {.mode = WGPUCallbackMode_WaitAnyOnly, .callback = scope_popped,
            .userdata1 = &reply};
        if (!runtime_wait_future(runtime, wgpuDevicePopErrorScope(runtime->device, callback)) || !reply.done) {
            /* An earlier failure of this command stays the reported one. */
            if (ok && execution->ok) runtime_fail(execution, "error_scope_unresolved", "an error scope did not resolve");
            ok = false;
        } else if (reply.error) {
            if (ok && execution->ok) runtime_fail(execution, "webgpu_error", "%s", reply.message);
            ok = false;
        }
    }
    if (runtime->uncaptured) {
        if (ok && execution->ok) runtime_fail(execution, "webgpu_uncaptured_error", "%s", runtime->uncaptured_message);
        ok = false;
    }
    return ok;
}

typedef struct done_reply {
    bool done;
    WGPUQueueWorkDoneStatus status;
} done_reply;

static void work_done(WGPUQueueWorkDoneStatus status, WGPUStringView message, void *userdata1, void *userdata2)
{
    (void)message;
    (void)userdata2;
    done_reply *reply = userdata1;
    reply->done = true;
    reply->status = status;
}

bool runtime_wait_queue(doe_runtime *runtime, doe_trace_execution *execution)
{
    done_reply reply = {0};
    WGPUQueueWorkDoneCallbackInfo callback = {.mode = WGPUCallbackMode_WaitAnyOnly, .callback = work_done,
        .userdata1 = &reply};
    if (!runtime_wait_future(runtime, wgpuQueueOnSubmittedWorkDone(runtime->queue, callback)) || !reply.done)
        return runtime_fail(execution, "queue_wait_unresolved", "queue work did not complete within the wait bound");
    if (reply.status != WGPUQueueWorkDoneStatus_Success)
        return runtime_fail(execution, "queue_work_failed", "queue work completed with status %d", (int)reply.status);
    return true;
}

buffer_entry *runtime_find_buffer(doe_runtime *runtime, uint64_t handle)
{
    for (size_t i = 0; i < runtime->buffer_count; i++) if (runtime->buffers[i].handle == handle) return &runtime->buffers[i];
    return NULL;
}

/* Handle buffers serve uploads, uniforms and storage alike. WebGPU zero-fills
 * new buffers, which is the "initialize_buffers_on_create" contents. */
buffer_entry *runtime_create_buffer(doe_runtime *runtime, uint64_t handle, uint64_t size, doe_trace_execution *execution)
{
    if (size == 0 || size > UINT64_MAX - 3) {
        runtime_fail(execution, "buffer_size_invalid", "buffer %llu needs a nonzero size", (unsigned long long)handle);
        return NULL;
    }
    if (runtime->buffer_count == runtime->buffer_capacity) {
        size_t capacity = runtime->buffer_capacity ? runtime->buffer_capacity * 2 : 16;
        buffer_entry *grown = realloc(runtime->buffers, capacity * sizeof(*grown));
        if (!grown) { runtime_fail(execution, "out_of_memory", "out of memory"); return NULL; }
        runtime->buffers = grown;
        runtime->buffer_capacity = capacity;
    }
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.size = (size + 3) & ~UINT64_C(3);
    descriptor.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(runtime->device, &descriptor);
    if (!buffer) {
        runtime_fail(execution, "buffer_create_failed", "buffer %llu of %llu bytes was not created",
            (unsigned long long)handle, (unsigned long long)size);
        return NULL;
    }
    buffer_entry *entry = &runtime->buffers[runtime->buffer_count++];
    *entry = (buffer_entry){.handle = handle, .buffer = buffer, .size = descriptor.size};
    return entry;
}

buffer_entry *runtime_ensure_buffer(doe_runtime *runtime, uint64_t handle, uint64_t size, doe_trace_execution *execution)
{
    buffer_entry *entry = runtime_find_buffer(runtime, handle);
    if (entry && entry->size >= size) return entry;
    if (!entry) return runtime_create_buffer(runtime, handle, size, execution);
    size_t position = (size_t)(entry - runtime->buffers);
    WGPUBuffer previous = entry->buffer;
    runtime->buffers[position] = runtime->buffers[--runtime->buffer_count];
    buffer_entry *created = runtime_create_buffer(runtime, handle, size, execution);
    wgpuBufferRelease(previous);
    return created;
}

bool runtime_submit_and_wait(doe_runtime *runtime, WGPUCommandBuffer commands, doe_trace_execution *execution)
{
    if (!commands) return runtime_fail(execution, "encode_failed", "command buffer was not created");
    wgpuQueueSubmit(runtime->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    return runtime_wait_queue(runtime, execution);
}

static bool execute_upload(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution)
{
    uint64_t align = command->as.upload.align_bytes;
    uint64_t bytes = command->as.upload.bytes;
    if (bytes > UINT64_MAX - (align - 1)) return runtime_fail(execution, "upload_size_invalid", "upload size overflows its alignment");
    uint64_t size = (bytes + align - 1) / align * align;
    if (size > SIZE_MAX) return runtime_fail(execution, "upload_size_invalid", "upload exceeds host memory addressing");
    uint64_t start = doe_runtime_monotonic_ns();
    void *data = calloc(1, (size_t)size);
    if (!data) return runtime_fail(execution, "out_of_memory", "upload staging of %llu bytes failed", (unsigned long long)size);
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.size = size;
    descriptor.usage = WGPUBufferUsage_CopyDst | (runtime->options.upload_copy_src ? WGPUBufferUsage_CopySrc : WGPUBufferUsage_None);
    runtime_push_scopes(runtime);
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(runtime->device, &descriptor);
    uint64_t setup_end = doe_runtime_monotonic_ns();
    if (buffer) wgpuQueueWriteBuffer(runtime->queue, buffer, 0, data, (size_t)size);
    free(data);
    bool ok = true;
    runtime->uploads_since_wait++;
    if (runtime->uploads_since_wait >= runtime->options.upload_submit_every) {
        ok = runtime_wait_queue(runtime, execution);
        runtime->uploads_since_wait = 0;
        execution->submit_count = 1;
    }
    uint64_t end = doe_runtime_monotonic_ns();
    if (!runtime_pop_scopes(runtime, execution)) ok = false;
    if (buffer) wgpuBufferRelease(buffer);
    if (ok && !buffer) return runtime_fail(execution, "upload_buffer_failed", "upload buffer was not created");
    execution->setup_ns = setup_end - start;
    execution->submit_wait_ns = end - setup_end;
    return ok;
}

static bool execute_buffer_write(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution)
{
    uint64_t data_bytes = (uint64_t)command->as.buffer_write.word_count * 4u;
    uint64_t offset = command->as.buffer_write.offset;
    if (offset > UINT64_MAX - data_bytes) return runtime_fail(execution, "buffer_write_range", "buffer_write range overflows");
    uint64_t end_byte = offset + data_bytes;
    uint64_t start = doe_runtime_monotonic_ns();
    buffer_entry *entry = runtime_find_buffer(runtime, command->as.buffer_write.handle);
    runtime_push_scopes(runtime);
    if (!entry) {
        uint64_t size = command->as.buffer_write.buffer_size > end_byte ? command->as.buffer_write.buffer_size : end_byte;
        entry = runtime_create_buffer(runtime, command->as.buffer_write.handle, size, execution);
    }
    bool ok = entry != NULL;
    if (ok && end_byte > entry->size) {
        ok = runtime_fail(execution, "buffer_write_range", "buffer_write ends at byte %llu past buffer %llu of %llu bytes",
            (unsigned long long)end_byte, (unsigned long long)entry->handle, (unsigned long long)entry->size);
    }
    uint64_t setup_end = doe_runtime_monotonic_ns();
    if (ok) {
        wgpuQueueWriteBuffer(runtime->queue, entry->buffer, offset, command->as.buffer_write.words, (size_t)data_bytes);
        ok = runtime_wait_queue(runtime, execution);
        execution->submit_count = 1;
    }
    uint64_t end = doe_runtime_monotonic_ns();
    if (!runtime_pop_scopes(runtime, execution)) ok = false;
    execution->setup_ns = setup_end - start;
    execution->submit_wait_ns = end - setup_end;
    return ok;
}

static bool execute_barrier(doe_runtime *runtime, doe_trace_execution *execution)
{
    uint64_t start = doe_runtime_monotonic_ns();
    bool ok = runtime_wait_queue(runtime, execution);
    runtime->uploads_since_wait = 0;
    execution->submit_count = 1;
    execution->submit_wait_ns = doe_runtime_monotonic_ns() - start;
    return ok;
}

bool doe_runtime_execute(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution)
{
    *execution = (doe_trace_execution){.ok = true};
    uint64_t start = doe_runtime_monotonic_ns();
    bool ok = false;
    switch (command->kind) {
    case DOE_COMMAND_UPLOAD: ok = execute_upload(runtime, command, execution); break;
    case DOE_COMMAND_BUFFER_WRITE: ok = execute_buffer_write(runtime, command, execution); break;
    case DOE_COMMAND_BARRIER: ok = execute_barrier(runtime, execution); break;
    case DOE_COMMAND_KERNEL_DISPATCH:
        ok = runtime_execute_kernel(runtime, command, execution);
        if (ok) {
            runtime->has_last_kernel = true;
            runtime->last_kernel = *command;
        }
        break;
    case DOE_COMMAND_DISPATCH:
    case DOE_COMMAND_DISPATCH_INDIRECT: ok = runtime_execute_grid(runtime, command, execution); break;
    case DOE_COMMAND_COPY: ok = runtime_execute_copy(runtime, command, execution); break;
    case DOE_COMMAND_TEXTURE_WRITE:
    case DOE_COMMAND_TEXTURE_QUERY:
    case DOE_COMMAND_TEXTURE_DESTROY: ok = runtime_execute_texture(runtime, command, execution); break;
    case DOE_COMMAND_RENDER_DRAW:
    case DOE_COMMAND_RENDER_PASS:
    case DOE_COMMAND_DRAW_INDIRECT:
    case DOE_COMMAND_DRAW_INDEXED_INDIRECT: ok = runtime_execute_render(runtime, command, execution); break;
    case DOE_COMMAND_ASYNC_DIAGNOSTICS: ok = runtime_execute_diagnostics(runtime, command, execution); break;
    case DOE_COMMAND_MAP_ASYNC: ok = runtime_execute_map(runtime, command, execution); break;
    case DOE_COMMAND_SAMPLER_CREATE:
    case DOE_COMMAND_SAMPLER_DESTROY: ok = runtime_execute_sampler(runtime, command, execution); break;
    }
    execution->duration_ns = doe_runtime_monotonic_ns() - start;
    if (ok) {
        static const char *const codes[] = {
            [DOE_COMMAND_UPLOAD] = "upload_timing_cpu_wall_clock_comparability_directional",
            [DOE_COMMAND_BUFFER_WRITE] = "buffer_write_timing_cpu_wall_clock_comparability_directional",
            [DOE_COMMAND_BARRIER] = "barrier_timing_cpu_wall_clock_comparability_directional",
            [DOE_COMMAND_KERNEL_DISPATCH] = "kernel_dispatch_timing_cpu_wall_clock_comparability_directional",
            [DOE_COMMAND_COPY] = "copy_buffer_to_texture_timing_cpu_wall_clock_comparability_directional",
            [DOE_COMMAND_TEXTURE_WRITE] = "texture_write_timing_cpu_wall_clock_comparability_directional",
            [DOE_COMMAND_TEXTURE_QUERY] = "texture_query_timing_cpu_wall_clock_comparability_directional",
            [DOE_COMMAND_TEXTURE_DESTROY] = "texture_destroy_timing_cpu_wall_clock_comparability_directional",
            [DOE_COMMAND_RENDER_DRAW] = "render_draw_timing_cpu_wall_clock_comparability_directional",
            [DOE_COMMAND_RENDER_PASS] = "render_pass_timing_cpu_wall_clock_comparability_directional",
            [DOE_COMMAND_SAMPLER_CREATE] = "sampler_create_timing_cpu_wall_clock_comparability_directional",
            [DOE_COMMAND_SAMPLER_DESTROY] = "sampler_destroy_timing_cpu_wall_clock_comparability_directional",
            [DOE_COMMAND_DISPATCH] = "dispatch_timing_cpu_wall_clock_comparability_directional",
            [DOE_COMMAND_DISPATCH_INDIRECT] = "dispatch_indirect_timing_cpu_wall_clock_comparability_directional",
            [DOE_COMMAND_DRAW_INDIRECT] = "draw_indirect_timing_cpu_wall_clock_comparability_directional",
            [DOE_COMMAND_DRAW_INDEXED_INDIRECT] = "draw_indexed_indirect_timing_cpu_wall_clock_comparability_directional",
            [DOE_COMMAND_ASYNC_DIAGNOSTICS] = "async_diagnostics_timing_cpu_wall_clock_comparability_directional",
            [DOE_COMMAND_MAP_ASYNC] = "map_async_timing_cpu_wall_clock_comparability_directional",
        };
        execution->ok = true;
        execution->status_code = codes[command->kind];
        if (!execution->message[0]) snprintf(execution->message, sizeof(execution->message), "%s timing=cpu_wall_clock comparability=directional",
            doe_command_tag(command->kind));
    }
    return ok;
}

bool doe_runtime_validate_oracle(doe_runtime *runtime, const doe_command *command, const doe_output_oracle *oracle,
    char actual_sha256[65], doe_trace_execution *execution)
{
    return runtime_validate_oracle(runtime, command, oracle, actual_sha256, execution);
}
