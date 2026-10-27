/* SPDX-License-Identifier: MIT */
#include "runtime_internal.h"
#include "doe_native_adapter.h" /* doeNativeAdapterGetPciIdentity: the driver version webgpu.h does not report. */
/* Weak: another webgpu.h provider (Dawn) has no Doe extension; the driver
 * version is then 0, unreported. */
#ifndef _WIN32
#pragma weak doeNativeAdapterGetPciIdentity
#endif
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_io.h"

uint64_t doe_runtime_monotonic_ns(void)
{
    return doe_host_monotonic_ns();
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

/* Each admitted callback owns a distinct lease; a failed wait is not settlement.
 * WaitAnyOnly callbacks may run in a later wait or provider teardown. */
typedef struct runtime_reply {
    atomic_uint references;
    atomic_uint state;
    _Atomic(void *) object;
    int status;
    bool error;
    char message[256];
    doe_runtime *runtime; /* Device-request lease pins uncaptured-error userdata. */
    WGPUInstance instance;
} runtime_reply;

enum { REPLY_DONE = 1, REPLY_ABANDONED = 2 };

static void error_lock(doe_runtime *runtime)
{
    while (atomic_flag_test_and_set_explicit(&runtime->error_guard, memory_order_acquire)) {}
}
static void error_unlock(doe_runtime *runtime)
{
    atomic_flag_clear_explicit(&runtime->error_guard, memory_order_release);
}

static void runtime_host_release(doe_runtime *runtime)
{
    if (atomic_fetch_sub_explicit(&runtime->host_references, 1, memory_order_acq_rel) == 1) free(runtime);
}
static runtime_reply *reply_create(doe_runtime *runtime)
{
    runtime_reply *reply = calloc(1, sizeof(*reply));
    if (!reply) return NULL;
    atomic_init(&reply->references, 2);
    atomic_init(&reply->state, 0);
    atomic_init(&reply->object, NULL);
    reply->runtime = runtime;
    if (runtime) {
        atomic_fetch_add_explicit(&runtime->host_references, 1, memory_order_relaxed);
        reply->instance = runtime->instance;
        wgpuInstanceAddRef(reply->instance);
    }
    return reply;
}
static void reply_release(runtime_reply *reply)
{
    if (atomic_fetch_sub_explicit(&reply->references, 1, memory_order_acq_rel) != 1) return;
    doe_runtime *runtime = reply->runtime;
    WGPUInstance instance = reply->instance;
    free(reply);
    if (instance) wgpuInstanceRelease(instance);
    if (runtime) runtime_host_release(runtime);
}
static bool reply_done(runtime_reply *reply)
{
    return (atomic_load_explicit(&reply->state, memory_order_acquire) & REPLY_DONE) != 0;
}
static void *reply_take(runtime_reply *reply)
{
    return atomic_exchange_explicit(&reply->object, NULL, memory_order_acq_rel);
}
static void reply_unadmitted(runtime_reply *reply, WGPUFuture future)
{
    /* Zero means no registered future. An inline callback already consumed its lease. */
    if (!future.id && !reply_done(reply)) reply_release(reply);
}

/* Destroy stops new uncaptured errors. Dawn's terminal lost event additionally
 * clears and joins its spontaneous error callbacks; C publishes loss after
 * clearing its handler. Unknown completion must retain callback userdata. */
static bool device_quiesce(doe_runtime *runtime, WGPUDevice device)
{
    wgpuDeviceDestroy(device);
    if (runtime_wait_future(runtime, wgpuDeviceGetLostFuture(device))) return true;
    fprintf(stderr, "runtime close: device loss remains unresolved; callback storage retained\n");
    return false;
}

static void discard_adapter_reply(runtime_reply *reply)
{
    WGPUAdapter object = reply_take(reply);
    if (object) wgpuAdapterRelease(object);
}

static bool discard_device_reply(runtime_reply *reply)
{
    WGPUDevice object = reply_take(reply);
    if (!object) return true;
    if (!device_quiesce(reply->runtime, object)) {
        /* This unretired callback lease owns both the device and its userdata. */
        atomic_store_explicit(&reply->object, object, memory_order_release);
        return false;
    }
    wgpuDeviceRelease(object);
    return true;
}

static void adapter_reply(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message,
    void *userdata1, void *userdata2)
{
    (void)userdata2;
    runtime_reply *reply = userdata1;
    reply->status = (int)status;
    runtime_copy_view(reply->message, sizeof(reply->message), message);
    atomic_store_explicit(&reply->object, adapter, memory_order_release);
    unsigned previous = atomic_fetch_or_explicit(&reply->state, REPLY_DONE, memory_order_acq_rel);
    if (previous & REPLY_ABANDONED) discard_adapter_reply(reply);
    reply_release(reply);
}

static void device_reply(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message,
    void *userdata1, void *userdata2)
{
    (void)userdata2;
    runtime_reply *reply = userdata1;
    reply->status = (int)status;
    runtime_copy_view(reply->message, sizeof(reply->message), message);
    atomic_store_explicit(&reply->object, device, memory_order_release);
    unsigned previous = atomic_fetch_or_explicit(&reply->state, REPLY_DONE, memory_order_acq_rel);
    if ((previous & REPLY_ABANDONED) && !discard_device_reply(reply)) return;
    reply_release(reply);
}

static void uncaptured_error(WGPUDevice const *device, WGPUErrorType type, WGPUStringView message,
    void *userdata1, void *userdata2)
{
    (void)device;
    (void)type;
    (void)userdata2;
    doe_runtime *runtime = userdata1;
    error_lock(runtime);
    if (!runtime->closing && !runtime->uncaptured) {
        runtime->uncaptured = true;
        runtime_copy_view(runtime->uncaptured_message, sizeof(runtime->uncaptured_message), message);
    }
    error_unlock(runtime);
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
        doe_host_pause_ns(20000);
    }
}

static WGPUAdapter request_adapter(doe_runtime *runtime, bool fallback, char *message, size_t message_size)
{
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
#ifdef _WIN32
    options.backendType = WGPUBackendType_D3D12;
#elif defined(__APPLE__)
    options.backendType = WGPUBackendType_Metal;
#else
    options.backendType = WGPUBackendType_Vulkan;
#endif
    if (runtime->options.backend_lane) options.backendType = runtime->policy_backend;
    options.forceFallbackAdapter = fallback ? WGPU_TRUE : WGPU_FALSE;
    if (runtime->request_wait_failed) {
        snprintf(message, message_size, "a previous adapter request remains unresolved");
        return NULL;
    }
    runtime_reply *reply = reply_create(NULL);
    if (!reply) { snprintf(message, message_size, "adapter reply allocation failed"); return NULL; }
    WGPURequestAdapterCallbackInfo callback = {.mode = WGPUCallbackMode_WaitAnyOnly, .callback = adapter_reply,
        .userdata1 = reply};
    WGPUFuture future = wgpuInstanceRequestAdapter(runtime->instance, &options, callback);
    if (!runtime_wait_future(runtime, future) || !reply_done(reply)) {
        runtime->request_wait_failed = true;
        unsigned previous = atomic_fetch_or_explicit(&reply->state, REPLY_ABANDONED, memory_order_acq_rel);
        if (previous & REPLY_DONE) discard_adapter_reply(reply);
        reply_unadmitted(reply, future);
        reply_release(reply);
        snprintf(message, message_size, "adapter request did not complete");
        return NULL;
    }
    WGPUAdapter adapter = reply_take(reply);
    if (reply->status != (int)WGPURequestAdapterStatus_Success || !adapter) {
        snprintf(message, message_size, "no %s platform adapter: %.180s", fallback ? "fallback" : "nonfallback", reply->message);
        if (adapter) wgpuAdapterRelease(adapter);
        reply_release(reply);
        return NULL;
    }
    reply_release(reply);
    return adapter;
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
#if defined(_WIN32) && !defined(DOE_RUNTIME_DAWN)
    doeNativeAdapterGetPciIdentity(runtime->adapter, NULL, NULL, &runtime->info.driver_version);
#elif !defined(_WIN32)
    if (doeNativeAdapterGetPciIdentity) doeNativeAdapterGetPciIdentity(runtime->adapter, NULL, NULL, &runtime->info.driver_version);
#endif
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
    atomic_init(&runtime->host_references, 1);
    atomic_flag_clear(&runtime->error_guard);
    runtime->options = *options;
    DoeNativeDevicePolicy policy = {0};
    if (options->backend_lane && !doe_runtime_load_device_policy(options->backend_lane, &policy, message, message_size, runtime->device_policy_hash, &runtime->policy_backend)) {
        free(runtime);
        return NULL;
    }
    if (options->backend_lane && options->adapter == DOE_ADAPTER_AUTO) runtime->options.adapter = DOE_ADAPTER_PHYSICAL;
    runtime->info.adapter_type = "unknown";
    runtime->info.backend = "unknown";
    runtime->instance = wgpuCreateInstance(NULL);
    if (!runtime->instance) {
        snprintf(message, message_size, "wgpuCreateInstance failed");
        doe_runtime_close(runtime);
        return NULL;
    }
    char reason[256] = "";
    if (runtime->options.adapter != DOE_ADAPTER_SOFTWARE) runtime->adapter = request_adapter(runtime, false, reason, sizeof(reason));
    if (!runtime->adapter && runtime->options.adapter != DOE_ADAPTER_PHYSICAL)
        runtime->adapter = request_adapter(runtime, true, reason, sizeof(reason));
    if (!runtime->adapter) {
        snprintf(message, message_size, "%.250s", reason);
        doe_runtime_close(runtime);
        return NULL;
    }
    record_adapter(runtime);
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    /* The stream compiler may consume unchanged shaders with enable f16.
     * Request the real adapter feature; unavailable devices still reject f16. */
    WGPUFeatureName features[2];
    size_t feature_count = 0;
    if (wgpuAdapterHasFeature(runtime->adapter, WGPUFeatureName_ShaderF16))
        features[feature_count++] = WGPUFeatureName_ShaderF16;
    runtime->timestamps_enabled = options->timestamp_mode != DOE_TIMESTAMP_OFF && !options->defer_queue_sync &&
        wgpuAdapterHasFeature(runtime->adapter, WGPUFeatureName_TimestampQuery);
    if (options->timestamp_mode == DOE_TIMESTAMP_REQUIRE && !runtime->timestamps_enabled) {
        snprintf(message, message_size, "GPU timestamps require adapter timestamp-query and per-command synchronization");
        doe_runtime_close(runtime);
        return NULL;
    }
    if (runtime->timestamps_enabled) features[feature_count++] = WGPUFeatureName_TimestampQuery;
    descriptor.requiredFeatures = features;
    descriptor.requiredFeatureCount = feature_count;
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
    runtime_reply *reply = reply_create(runtime);
    if (!reply) {
        snprintf(message, message_size, "device reply allocation failed");
        doe_runtime_close(runtime);
        return NULL;
    }
    WGPURequestDeviceCallbackInfo callback = {.mode = WGPUCallbackMode_WaitAnyOnly, .callback = device_reply,
        .userdata1 = reply};
#ifdef DOE_RUNTIME_DAWN
    WGPUFuture device_future = wgpuAdapterRequestDevice(runtime->adapter, &descriptor, callback);
#else
    WGPUFuture device_future = options->backend_lane ?
        doeNativeAdapterRequestDeviceWithPolicy(runtime->adapter, &descriptor, callback,
            DOE_NATIVE_DEVICE_POLICY_ABI_VERSION, &policy, sizeof(policy)) :
        wgpuAdapterRequestDevice(runtime->adapter, &descriptor, callback);
#endif
    if (!runtime_wait_future(runtime, device_future) ||
        !reply_done(reply) || reply->status != (int)WGPURequestDeviceStatus_Success ||
        !atomic_load_explicit(&reply->object, memory_order_acquire)) {
        unsigned previous = atomic_fetch_or_explicit(&reply->state, REPLY_ABANDONED, memory_order_acq_rel);
        bool settled = !(previous & REPLY_DONE) || discard_device_reply(reply);
        snprintf(message, message_size, "device request failed: %.200s", reply_done(reply) ? reply->message : "unresolved");
        reply_unadmitted(reply, device_future);
        if (settled) reply_release(reply);
        doe_runtime_close(runtime);
        return NULL;
    }
    runtime->device_policy = policy;
    runtime->device = reply_take(reply);
    reply_release(reply);
    runtime->queue = wgpuDeviceGetQueue(runtime->device);
#ifndef DOE_RUNTIME_DAWN
    runtime->observed_sync_bits = doeNativeQueueSyncInfo(runtime->queue);
#endif
    runtime_pipeline_cache_open(runtime);
    return runtime;
}

const doe_runtime_adapter *doe_runtime_adapter_info(const doe_runtime *runtime)
{
    return &runtime->info;
}

bool doe_runtime_close(doe_runtime *runtime)
{
    if (!runtime) return true;
    error_lock(runtime);
    runtime->closing = true;
    error_unlock(runtime);
    if (runtime->device && !device_quiesce(runtime, runtime->device)) return false;
    if (!runtime_retire_readback(runtime)) return false;
    for (size_t i = 0; i < runtime->pipeline_count; i++) {
        pipeline_entry *entry = &runtime->pipelines[i];
        if (entry->pipeline) wgpuComputePipelineRelease(entry->pipeline);
        for (uint32_t g = 0; g < entry->group_count; g++) if (entry->groups[g]) wgpuBindGroupLayoutRelease(entry->groups[g]);
        free(entry->source_path);
        free(entry->entry_point);
        free(entry->signature);
    }
    free(runtime->pipelines);
    runtime_kernel_sources_clear(runtime);
    runtime_release_render(runtime);
    runtime_release_surfaces(runtime);
    runtime_release_diagnostics(runtime);
    runtime_release_textures(runtime);
    runtime_buffer_table_clear(&runtime->buffers);
    if (runtime->indirect_args) wgpuBufferRelease(runtime->indirect_args);
    if (runtime->map_buffer) wgpuBufferRelease(runtime->map_buffer);
    if (runtime->queue) wgpuQueueRelease(runtime->queue);
    if (runtime->device) wgpuDeviceRelease(runtime->device);
    if (runtime->adapter) wgpuAdapterRelease(runtime->adapter);
    if (runtime->instance) wgpuInstanceRelease(runtime->instance);
    runtime_host_release(runtime);
    return true;
}

/* Pushes validation, out-of-memory and internal scopes around one command. */
void runtime_push_scopes(doe_runtime *runtime)
{
    wgpuDevicePushErrorScope(runtime->device, WGPUErrorFilter_Internal);
    wgpuDevicePushErrorScope(runtime->device, WGPUErrorFilter_OutOfMemory);
    wgpuDevicePushErrorScope(runtime->device, WGPUErrorFilter_Validation);
    error_lock(runtime);
    runtime->uncaptured = false;
    error_unlock(runtime);
}

static void scope_popped(WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message,
    void *userdata1, void *userdata2)
{
    (void)userdata2;
    runtime_reply *reply = userdata1;
    if (status != WGPUPopErrorScopeStatus_Success || type != WGPUErrorType_NoError) {
        reply->error = true;
        runtime_copy_view(reply->message, sizeof(reply->message), message);
        if (!reply->message[0]) snprintf(reply->message, sizeof(reply->message), "error scope reported type %d", (int)type);
    }
    atomic_fetch_or_explicit(&reply->state, REPLY_DONE, memory_order_acq_rel);
    reply_release(reply);
}

/* Pops the three scopes; the first reported error wins. */
bool runtime_pop_scopes(doe_runtime *runtime, doe_trace_execution *execution)
{
    if (runtime->scope_wait_failed) {
        if (execution->ok) runtime_fail(execution, "error_scope_unresolved", "a previous error scope remains unresolved");
        return false;
    }
    bool ok = true;
    for (int i = 0; i < 3; i++) {
        runtime_reply *reply = reply_create(NULL);
        if (!reply) {
            runtime->scope_wait_failed = true;
            if (ok && execution->ok) runtime_fail(execution, "out_of_memory", "error scope reply allocation failed");
            return false;
        }
        WGPUPopErrorScopeCallbackInfo callback = {.mode = WGPUCallbackMode_WaitAnyOnly, .callback = scope_popped,
            .userdata1 = reply};
        WGPUFuture future = wgpuDevicePopErrorScope(runtime->device, callback);
        if (!runtime_wait_future(runtime, future) || !reply_done(reply)) {
            runtime->scope_wait_failed = true;
            /* An earlier failure of this command stays the reported one. */
            if (ok && execution->ok) runtime_fail(execution, "error_scope_unresolved", "an error scope did not resolve");
            ok = false;
        } else if (reply->error) {
            if (ok && execution->ok) runtime_fail(execution, "webgpu_error", "%s", reply->message);
            ok = false;
        }
        reply_unadmitted(reply, future);
        reply_release(reply);
    }
    char message[sizeof(runtime->uncaptured_message)];
    error_lock(runtime);
    bool uncaptured = runtime->uncaptured;
    memcpy(message, runtime->uncaptured_message, sizeof(message));
    error_unlock(runtime);
    if (uncaptured) {
        if (ok && execution->ok) runtime_fail(execution, "webgpu_uncaptured_error", "%s", message);
        ok = false;
    }
    return ok;
}

buffer_entry *runtime_buffer_table_find(runtime_buffer_table *table, uint64_t handle)
{
    for (size_t i = 0; i < table->count; i++) if (table->entries[i].handle == handle) return &table->entries[i];
    return NULL;
}

buffer_entry *runtime_find_buffer(doe_runtime *runtime, uint64_t handle)
{
    return runtime_buffer_table_find(&runtime->buffers, handle);
}

void runtime_buffer_table_clear(runtime_buffer_table *table)
{
    for (size_t i = 0; i < table->count; i++) wgpuBufferRelease(table->entries[i].buffer);
    free(table->entries);
    *table = (runtime_buffer_table){0};
}

/* Handle buffers serve uploads, uniforms and storage alike. WebGPU zero-fills
 * new buffers, which is the "initialize_buffers_on_create" contents. */
static buffer_entry *create_buffer_usage(doe_runtime *runtime, runtime_buffer_table *table, uint64_t handle,
    uint64_t size, WGPUBufferUsage usage, doe_trace_execution *execution)
{
    if (size == 0 || size > UINT64_MAX - 3) {
        runtime_fail(execution, "buffer_size_invalid", "buffer %llu needs a nonzero size", (unsigned long long)handle);
        return NULL;
    }
    if (table->count == table->capacity) {
        size_t capacity = table->capacity ? table->capacity * 2 : 16;
        buffer_entry *grown = realloc(table->entries, capacity * sizeof(*grown));
        if (!grown) { runtime_fail(execution, "out_of_memory", "out of memory"); return NULL; }
        table->entries = grown;
        table->capacity = capacity;
    }
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.size = (size + 3) & ~UINT64_C(3);
    descriptor.usage = usage;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(runtime->device, &descriptor);
    if (!buffer) {
        runtime_fail(execution, "buffer_create_failed", "buffer %llu of %llu bytes was not created",
            (unsigned long long)handle, (unsigned long long)size);
        return NULL;
    }
    buffer_entry *entry = &table->entries[table->count++];
    *entry = (buffer_entry){.handle = handle, .buffer = buffer, .size = descriptor.size, .usage = usage};
    return entry;
}

buffer_entry *runtime_buffer_table_create(doe_runtime *runtime, runtime_buffer_table *table,
    uint64_t handle, uint64_t size, doe_trace_execution *execution)
{
    return create_buffer_usage(runtime, table, handle, size, WGPUBufferUsage_Storage | WGPUBufferUsage_Uniform |
        WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc, execution);
}

buffer_entry *runtime_create_buffer(doe_runtime *runtime, uint64_t handle, uint64_t size, doe_trace_execution *execution)
{
    return runtime_buffer_table_create(runtime, &runtime->buffers, handle, size, execution);
}

buffer_entry *runtime_ensure_buffer_usage(doe_runtime *runtime, uint64_t handle, uint64_t size, WGPUBufferUsage usage,
    doe_trace_execution *execution)
{
    buffer_entry *entry = runtime_find_buffer(runtime, handle);
    if (entry && entry->size >= size && (entry->usage & usage) == usage) return entry;
    if (!entry) return create_buffer_usage(runtime, &runtime->buffers, handle, size, usage, execution);
    usage |= entry->usage;
    size_t position = (size_t)(entry - runtime->buffers.entries);
    WGPUBuffer previous = entry->buffer;
    runtime->buffers.entries[position] = runtime->buffers.entries[--runtime->buffers.count];
    buffer_entry *created = create_buffer_usage(runtime, &runtime->buffers, handle, size, usage, execution);
    wgpuBufferRelease(previous);
    return created;
}

buffer_entry *runtime_ensure_buffer(doe_runtime *runtime, uint64_t handle, uint64_t size, doe_trace_execution *execution)
{
    return runtime_ensure_buffer_usage(runtime, handle, size, WGPUBufferUsage_Storage | WGPUBufferUsage_Uniform |
        WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc, execution);
}

bool runtime_submit(doe_runtime *runtime, WGPUCommandBuffer commands, doe_trace_execution *execution)
{
    if (runtime->queue_wait_failed) {
        if (commands) wgpuCommandBufferRelease(commands);
        return runtime_fail(execution, "queue_wait_unresolved", "a previous queue wait failed; no new work submitted");
    }
    if (!commands) return runtime_fail(execution, "encode_failed", "command buffer was not created");
    wgpuQueueSubmit(runtime->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    return runtime_after_submit(runtime, execution);
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
    runtime->queue_pending = true;
    runtime->uploads_since_wait++;
    if (runtime->uploads_since_wait >= runtime->options.upload_submit_every) {
        ok = runtime_after_submit(runtime, execution);
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
        ok = runtime_after_submit(runtime, execution);
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
    error_lock(runtime);
    bool closing = runtime->closing;
    error_unlock(runtime);
    if (closing || runtime->scope_wait_failed || runtime->readback || runtime->queue_wait_failed)
        return runtime_fail(execution, "runtime_unresolved", "runtime teardown or error scopes remain unresolved");
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
    case DOE_COMMAND_SURFACE_CREATE:
    case DOE_COMMAND_SURFACE_CAPABILITIES:
    case DOE_COMMAND_SURFACE_CONFIGURE:
    case DOE_COMMAND_SURFACE_ACQUIRE:
    case DOE_COMMAND_SURFACE_PRESENT:
    case DOE_COMMAND_SURFACE_UNCONFIGURE:
    case DOE_COMMAND_SURFACE_RELEASE:
        ok = runtime_execute_surface(runtime, command, execution); break;
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
            [DOE_COMMAND_SURFACE_CREATE] = "surface_create_timing_cpu_wall_clock_comparability_directional",
            [DOE_COMMAND_SURFACE_CAPABILITIES] = "surface_capabilities_timing_cpu_wall_clock_comparability_directional",
            [DOE_COMMAND_SURFACE_CONFIGURE] = "surface_configure_timing_cpu_wall_clock_comparability_directional",
            [DOE_COMMAND_SURFACE_ACQUIRE] = "surface_acquire_timing_cpu_wall_clock_comparability_directional",
            [DOE_COMMAND_SURFACE_PRESENT] = "surface_present_timing_cpu_wall_clock_comparability_directional",
            [DOE_COMMAND_SURFACE_UNCONFIGURE] = "surface_unconfigure_timing_cpu_wall_clock_comparability_directional",
            [DOE_COMMAND_SURFACE_RELEASE] = "surface_release_timing_cpu_wall_clock_comparability_directional",
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
