/* SPDX-License-Identifier: MIT */
#include "surface.h"
#include "d3d12_frontend.h"
#include "../../backends/metal/src/progress.h"
#include <stdlib.h>

typedef enum native_async_kind { NATIVE_ASYNC_QUEUE, NATIVE_ASYNC_MAP } native_async_kind;
typedef struct native_async {
    native_pending pending; /* One closed variant on the ordinary pending owner. */
    native_async_kind kind;
    doe_callback_event *event;
    uint64_t target_serial, failure_serial;
    native_failure failure;
    union {
        WGPUQueueWorkDoneCallbackInfo queue;
        WGPUBufferMapCallbackInfo map;
    } callback;
    doe_resource_buffer *buffer;
    uint64_t generation, offset;
    size_t size;
    WGPUMapMode mode;
    WGPUMapAsyncStatus map_status;
    unsigned char *bytes;
    const unsigned char *direct;
    bool began;
    char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
} native_async;

bool native_async_pending(const native_device *device)
{
    for (const native_pending *p = device->pending; p; p = p->next)
        if (p->kind == NATIVE_PENDING_ASYNC) return true;
    return false;
}

void native_async_submission_settled(native_device *device, uint64_t serial, native_failure failure)
{
    if (failure.status == NATIVE_OK) return;
    for (native_pending *p = device->pending; p; p = p->next) {
        if (p->kind != NATIVE_PENDING_ASYNC) continue;
        native_async *value = p->async;
        if (value->kind != NATIVE_ASYNC_QUEUE || serial > value->target_serial) continue;
        if (value->failure.status == NATIVE_OK || serial < value->failure_serial) {
            value->failure = failure;
            value->failure_serial = serial;
        }
    }
}

static bool earlier_commands(const native_async *value)
{
    for (const native_pending *p = value->pending.device->pending; p; p = p->next)
        if (p->kind == NATIVE_PENDING_COMMANDS && p->serial <= value->target_serial) return true;
    return false;
}

static bool buffer_busy(const native_buffer *buffer)
{
    switch (buffer->device->backend.kind) {
    case WGPUBackendType_Vulkan: return doe_vk_buffer_busy(buffer->handle.vulkan);
    case WGPUBackendType_Metal: return doe_mtl_buffer_owner(buffer->handle.metal) != NULL;
    case WGPUBackendType_D3D12:
#if defined(_WIN32)
        return buffer->handle.d3d12 && doe_dx_buffer_owner(buffer->handle.d3d12->allocation) != NULL;
#else
        return false;
#endif
    default: return false;
    }
}

static bool prepare_map(native_async *value)
{
    native_device *device = value->pending.device;
    if (!value->began) return true;
    if (!doe_resource_buffer_map_pending(value->buffer, value->generation)) {
        value->map_status = WGPUMapAsyncStatus_Aborted;
        value->failure = native_failure_status(NATIVE_INVALID_STATE);
        return true;
    }
    native_result alive = native_device_result(device);
    if (alive != NATIVE_OK) {
        value->map_status = WGPUMapAsyncStatus_Aborted;
        value->failure = native_failure_status(alive);
        return true;
    }
    native_buffer *buffer = doe_resource_buffer_native(value->buffer);
    if (buffer_busy(buffer) && earlier_commands(value)) return false;
    /* A captured submission error does not prove this buffer's owner terminal.
     * Preserve the pending map obligation until availability or cancellation. */
    if (value->failure.status != NATIVE_OK) return true;
    /* Resource map admission excludes later GPU uses. Only after the accepted
     * owner retires may the progress owner borrow bytes or copy the range. */
    if (value->mode == WGPUMapMode_Write && native_buffer_host_writable(buffer)) {
        unsigned char *direct = NULL;
        value->failure = native_buffer_host_write_range(buffer, value->offset, value->size, &direct);
        value->direct = direct;
    } else if (value->mode == WGPUMapMode_Read && native_buffer_host_lendable(buffer)) {
        value->failure = native_buffer_host_range(buffer, value->offset, value->size, &value->direct);
    } else {
        value->bytes = malloc(value->size ? value->size : 1);
        value->failure = value->bytes ? native_buffer_read(buffer, value->offset, value->bytes, value->size) :
            native_failure_status(NATIVE_OUT_OF_MEMORY);
    }
    if (value->failure.status == NATIVE_OK) value->map_status = WGPUMapAsyncStatus_Success;
    return true;
}

native_pending *native_async_take_ready(native_device *device)
{
    native_pending **cursor = &device->pending;
    while (*cursor) {
        native_pending *pending = *cursor;
        if (pending->kind != NATIVE_PENDING_ASYNC || !pending->async->event) {
            cursor = &pending->next;
            continue;
        }
        native_async *value = pending->async;
        bool ready = value->kind == NATIVE_ASYNC_QUEUE ? !earlier_commands(value) : prepare_map(value);
        if (!ready) {
            cursor = &pending->next;
            continue;
        }
        if (value->kind == NATIVE_ASYNC_QUEUE && device->retired_failure.status != NATIVE_OK &&
                device->retired_failure_serial <= value->target_serial) {
            /* WorkDone is a public observer of this receipt. Each admitted
             * obligation already owns its independent failure snapshot. */
            if (value->failure.status == NATIVE_OK) value->failure = device->retired_failure;
            device->retired_failure = native_failure_status(NATIVE_OK);
            device->retired_failure_serial = 0;
        }
        native_failure_format(&value->failure, value->kind == NATIVE_ASYNC_QUEUE ?
            "queue work failed" : "buffer map failed or aborted", value->message);
        *cursor = pending->next;
        pending->next = NULL;
        native_metal_completion_observe(device, native_async_pending(device));
        return pending;
    }
    return NULL;
}

void native_async_publish_ready(native_pending *pending)
{
    /* mark_ready may deliver/free the entire operation, including this record.
     * The caller's worker/device lease remains independent through return. */
    doe_callback_mark_ready(pending->async->event);
}

static void copy_async(void *destination, const void *source)
{
    *(native_async **)destination = *(native_async *const *)source;
}

static void deliver_async(void *context)
{
    native_async *value = *(native_async **)context;
    native_device *device = value->pending.device;
    if (value->kind == NATIVE_ASYNC_MAP) {
        native_lock(device);
        if (value->began) {
            bool success = value->map_status == WGPUMapAsyncStatus_Success &&
                doe_resource_buffer_map_pending(value->buffer, value->generation);
            unsigned char *bytes = success ?
                (value->direct ? (unsigned char *)value->direct : value->bytes) : NULL;
            doe_resource_status status = doe_resource_buffer_map_complete(value->buffer,
                value->generation, success, bytes);
            if (status == DOE_RESOURCE_OK) {
                native_buffer *buffer = doe_resource_buffer_native(value->buffer);
                buffer->mapping = value->bytes;
                value->bytes = NULL;
            } else if (value->map_status == WGPUMapAsyncStatus_Success) {
                /* Unmap/destroy already returned that generation's direct lend.
                 * Returning it again could unmap a newer admitted generation. */
                value->map_status = WGPUMapAsyncStatus_Aborted;
            }
        }
        native_unlock(device);
        free(value->bytes);
        if (value->map_status != WGPUMapAsyncStatus_Success && !value->message[0]) {
            native_failure failure = native_failure_status(NATIVE_INVALID_STATE);
            native_failure_format(&failure, "buffer map failed or aborted", value->message);
        }
        if (value->callback.map.callback)
            value->callback.map.callback(value->map_status, native_string(value->message),
                value->callback.map.userdata1, value->callback.map.userdata2);
        doe_resource_buffer_release(value->buffer);
    } else if (value->callback.queue.callback) {
        value->callback.queue.callback(value->failure.status == NATIVE_OK ?
            WGPUQueueWorkDoneStatus_Success : WGPUQueueWorkDoneStatus_Error, native_string(value->message),
            value->callback.queue.userdata1, value->callback.queue.userdata2);
    }
    free(value);
    native_device_release(device);
}

static void publish_async(doe_callback_event *event, void *userdata)
{
    native_async *value = userdata;
    native_device *device = value->pending.device;
    native_lock(device);
    value->event = event;
    native_metal_completion_observe(device, true);
    native_pending_progress_wake(device);
    native_unlock(device);
}

static void link_async(native_async *value)
{
    native_device *device = value->pending.device;
    value->target_serial = device->submitted_serial;
    native_pending **cursor = &device->pending;
    while (*cursor) cursor = &(*cursor)->next;
    *cursor = &value->pending;
    native_device_retain(device); /* Transferred to admitted callback cleanup. */
}

static WGPUFuture admit_async(native_async *value, WGPUCallbackMode mode, bool rejected,
    native_failure consumed_failure)
{
    native_device *device = value->pending.device;
    doe_callback_status status;
    WGPUFuture future = native_callback_publish_admit(device->instance, mode, &value,
        sizeof(value), _Alignof(native_async *), copy_async, deliver_async, publish_async, value, &status);
    if (future.id) return future; /* A spontaneous callback may have freed value. */
    native_lock(device);
    native_pending **cursor = &device->pending;
    while (*cursor != &value->pending) cursor = &(*cursor)->next;
    *cursor = value->pending.next;
    if (value->began) (void)doe_resource_buffer_map_complete(value->buffer, value->generation, false, NULL);
    native_metal_completion_observe(device, native_async_pending(device));
    native_unlock(device);
    if (consumed_failure.status != NATIVE_OK) {
        native_failure_format(&consumed_failure, "asynchronous completion admission failed", value->message);
        native_error(device, native_result_error(consumed_failure.status), value->message);
    } else if (!rejected) native_callback_refused(device, status);
    doe_resource_buffer_release(value->buffer);
    free(value);
    native_device_release(device);
    return future;
}

WGPUFuture wgpuQueueOnSubmittedWorkDone(WGPUQueue handle, WGPUQueueWorkDoneCallbackInfo callback)
{
    native_device *device = (native_device *)handle;
    if (!device || callback.nextInChain) return (WGPUFuture){0};
    native_device_retain(device);
    native_async *value = calloc(1, sizeof(*value));
    if (!value) {
        native_callback_refused(device, DOE_CALLBACK_OUT_OF_MEMORY);
        native_device_release(device);
        return (WGPUFuture){0};
    }
    value->pending = (native_pending){.kind=NATIVE_PENDING_ASYNC,.device=device,.async=value};
    value->callback.queue = callback;
    native_lock(device);
    native_failure flushed = device->backend.kind == WGPUBackendType_D3D12 ?
        native_d3d12_flush_writes(device) : native_failure_status(NATIVE_OK);
    native_queue_settlement polled = native_poll_pending(device);
    value->failure = polled.failure.status != NATIVE_OK ? polled.failure : flushed;
    value->failure_serial = polled.failure.status != NATIVE_OK ? polled.failure_serial : 0;
    if (value->failure.status == NATIVE_PENDING || value->failure.status == NATIVE_UNKNOWN ||
        value->failure.status == NATIVE_TIMEOUT || value->failure.status == NATIVE_BUSY)
        value->failure_serial = UINT64_MAX; /* Uncertainty is not a terminal diagnostic. */
    if (value->failure.status == NATIVE_OK && device->queue_failed) {
        value->failure = native_failure_status(NATIVE_FAILURE);
        value->failure_serial = UINT64_MAX; /* Prefer an actual subsequently retired failure. */
    }
    link_async(value);
    native_unlock(device);
    WGPUFuture future = admit_async(value, callback.mode, false,
        polled.failure.status != NATIVE_OK ? polled.failure : flushed);
    native_device_release(device);
    return future;
}

WGPUFuture wgpuBufferMapAsync(WGPUBuffer handle, WGPUMapMode mode,
    size_t offset, size_t size, WGPUBufferMapCallbackInfo callback)
{
    doe_resource_buffer *buffer = (doe_resource_buffer *)handle;
    native_device *device = doe_resource_buffer_device(buffer);
    if (!device || callback.nextInChain) return (WGPUFuture){0};
    native_device_retain(device); /* Independent API call lease through admission. */
    native_async *value = calloc(1, sizeof(*value));
    if (!value) {
        native_callback_refused(device, DOE_CALLBACK_OUT_OF_MEMORY);
        native_device_release(device);
        return (WGPUFuture){0};
    }
    doe_resource_buffer_retain(buffer);
    value->pending = (native_pending){.kind=NATIVE_PENDING_ASYNC,.device=device,.async=value};
    value->kind = NATIVE_ASYNC_MAP;
    value->callback.map = callback;
    value->buffer = buffer;
    value->mode = mode;
    value->offset = offset;
    value->map_status = WGPUMapAsyncStatus_Error;
    native_lock(device);
    native_result alive = native_device_result(device);
    doe_resource_status status = alive == NATIVE_OK ? doe_resource_buffer_map_begin(buffer, mode, offset,
        size == WGPU_WHOLE_MAP_SIZE ? UINT64_MAX : size, &value->generation) : DOE_RESOURCE_UNAVAILABLE;
    bool rejected = alive == NATIVE_OK && status != DOE_RESOURCE_OK;
    value->failure = native_failure_status(alive == NATIVE_OK ? NATIVE_INVALID_ARGUMENT : alive);
    native_failure consumed = native_failure_status(NATIVE_OK);
    if (alive != NATIVE_OK) value->map_status = WGPUMapAsyncStatus_Aborted;
    if (status == DOE_RESOURCE_OK) {
        value->began = true;
        const doe_resource_buffer_desc *desc = doe_resource_buffer_description(buffer);
        value->size = size == WGPU_WHOLE_MAP_SIZE ? (size_t)(desc->size - offset) : size;
        value->failure = device->backend.kind == WGPUBackendType_D3D12 ?
            native_d3d12_flush_writes(device) : native_failure_status(NATIVE_OK);
        consumed = native_poll_pending(device).failure;
        if (consumed.status != NATIVE_OK) value->failure = consumed;
        else consumed = value->failure;
    }
    link_async(value);
    native_unlock(device);
    /* Validation reporting precedes admission and any spontaneous delivery. */
    if (rejected) {
        char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
        native_failure_format(&value->failure, "buffer map failed or aborted", message);
        native_error(device, native_resource_error(status), message);
    }
    WGPUFuture future = admit_async(value, callback.mode, rejected, consumed);
    native_device_release(device);
    return future;
}
