/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void native_buffer_release(void *userdata, void *handle)
{
    (void)userdata;
    native_buffer *payload = handle;
    free(payload->mapping);
    native_lock(payload->device);
    native_buffer_close(payload);
    native_unlock(payload->device);
    free(payload);
}

WGPUBuffer wgpuDeviceCreateBuffer(WGPUDevice handle, const WGPUBufferDescriptor *descriptor)
{
    native_device *device = (native_device *)handle;
    if (!device) return NULL;
    if (native_device_dead(device)) return native_lost_buffer(device, descriptor);
    return native_device_create_buffer(device, descriptor, device->backend.limits.maxBufferSize);
}

uint64_t native_allocation_bound(const native_device *device)
{
    /* One backend allocation backs one buffer; private staging uses the native
     * allocation bound independently of requested public buffer limits. */
    if (device->backend.kind == WGPUBackendType_Vulkan && device->backend.value.vulkan.caps.max_memory_allocation_size)
        return device->backend.value.vulkan.caps.max_memory_allocation_size;
    if (device->backend.kind == WGPUBackendType_Metal)
        return device->backend.value.metal.caps.max_buffer_length;
    if (device->backend.kind == WGPUBackendType_D3D12)
        return device->backend.value.d3d12.caps.limits.max_buffer_size;
    return UINT64_MAX;
}

native_buffer_creation native_device_create_buffer_result(native_device *device, const WGPUBufferDescriptor *descriptor,
    uint64_t max_size)
{
    native_buffer_creation result = {.failure=native_failure_status(NATIVE_OK),
        .status=DOE_RESOURCE_OK,.message="buffer creation failed or unsupported usage"};
    if (!descriptor || descriptor->nextInChain || descriptor->size > SIZE_MAX || descriptor->size > max_size) {
        result.status = DOE_RESOURCE_INVALID_ARGUMENT;
        result.message = "buffer descriptor unsupported or exceeds limit";
        return result;
    }
    doe_resource_buffer_desc desc = {descriptor->size, descriptor->usage, descriptor->mappedAtCreation != 0};
    result.status = doe_resource_buffer_validate(&desc);
    if (result.status != DOE_RESOURCE_OK) {
        result.message = "buffer descriptor validation failed";
        return result;
    }
    native_buffer *payload = calloc(1, sizeof(*payload));
    if (!payload) {
        result.failure = native_failure_status(NATIVE_OUT_OF_MEMORY);
        result.message = "buffer allocation failed";
        return result;
    }
    payload->device = device;
    uint32_t usage = 0;
    if (descriptor->usage & (WGPUBufferUsage_CopySrc | WGPUBufferUsage_MapWrite)) usage |= DOE_VK_BUFFER_COPY_SRC;
    if (descriptor->usage & (WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead | WGPUBufferUsage_QueryResolve)) usage |= DOE_VK_BUFFER_COPY_DST;
    if (descriptor->usage & WGPUBufferUsage_Uniform) usage |= DOE_VK_BUFFER_UNIFORM;
    if (descriptor->usage & WGPUBufferUsage_Storage) usage |= DOE_VK_BUFFER_STORAGE;
    if (descriptor->usage & WGPUBufferUsage_Vertex) usage |= DOE_VK_BUFFER_VERTEX;
    if (descriptor->usage & WGPUBufferUsage_Index) usage |= DOE_VK_BUFFER_INDEX;
    if (descriptor->usage & WGPUBufferUsage_Indirect) usage |= DOE_VK_BUFFER_INDIRECT | DOE_VK_BUFFER_COPY_SRC;
    const WGPUBufferUsage supported = WGPUBufferUsage_MapRead | WGPUBufferUsage_MapWrite |
        WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform |
        WGPUBufferUsage_Storage | WGPUBufferUsage_Indirect | WGPUBufferUsage_QueryResolve |
        WGPUBufferUsage_Vertex | WGPUBufferUsage_Index;
    doe_resource_buffer *buffer = NULL;
    native_lock(device);
    native_failure failure = native_failure_status(descriptor->usage & ~supported ?
        NATIVE_UNSUPPORTED : native_device_result(device));
    if (failure.status == NATIVE_OK)
        failure = native_buffer_create(payload, descriptor->size ? descriptor->size : 4, usage,
            (descriptor->usage & WGPUBufferUsage_MapRead) && !descriptor->mappedAtCreation);
    doe_resource_status created = DOE_RESOURCE_OK;
    if (failure.status == NATIVE_OK) {
        created = doe_resource_buffer_create(&device->resources, &desc,
            (doe_resource_native){payload, NULL, native_buffer_release}, &buffer);
        if (created == DOE_RESOURCE_OK && descriptor->mappedAtCreation) {
            payload->mapping = calloc(1, descriptor->size ? (size_t)descriptor->size : 1);
            created = payload->mapping ? doe_resource_buffer_map_complete(buffer, 0, true,
                payload->mapping) : DOE_RESOURCE_OUT_OF_MEMORY;
            if (created != DOE_RESOURCE_OK) {
                doe_resource_buffer_release(buffer);
                buffer = NULL;
                payload = NULL;
            }
        } else if (created != DOE_RESOURCE_OK) buffer = NULL;
    }
    native_unlock(device);
    if (!buffer && payload) native_buffer_release(NULL, payload);
    result.buffer = (WGPUBuffer)buffer;
    result.failure = failure;
    result.status = created;
    return result;
}

WGPUBuffer native_device_create_buffer(native_device *device, const WGPUBufferDescriptor *descriptor,
    uint64_t max_size)
{
    native_device_retain(device); /* Protect error/label callback reentry through return. */
    native_buffer_creation result = native_device_create_buffer_result(device, descriptor, max_size);
    if (!result.buffer) {
        char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
        if (result.failure.status == NATIVE_OK) result.failure = native_failure_status(NATIVE_INVALID_STATE);
        native_failure_format(&result.failure, result.message, message);
        native_error(device, result.status != DOE_RESOURCE_OK ? native_resource_error(result.status) :
            native_result_error(result.failure.status), message);
    }
    if (result.buffer) native_label(device, result.buffer, descriptor->label);
    native_device_release(device);
    return result.buffer;
}

void wgpuBufferAddRef(WGPUBuffer buffer) { doe_resource_buffer_retain((doe_resource_buffer *)buffer); }
void wgpuBufferRelease(WGPUBuffer buffer) { doe_resource_buffer_release((doe_resource_buffer *)buffer); }
uint64_t wgpuBufferGetSize(WGPUBuffer buffer)
{
    const doe_resource_buffer_desc *desc = doe_resource_buffer_description((doe_resource_buffer *)buffer);
    return desc ? desc->size : 0;
}
WGPUBufferUsage wgpuBufferGetUsage(WGPUBuffer buffer)
{
    const doe_resource_buffer_desc *desc = doe_resource_buffer_description((doe_resource_buffer *)buffer);
    return desc ? desc->usage : WGPUBufferUsage_None;
}
WGPUBufferMapState wgpuBufferGetMapState(WGPUBuffer handle)
{
    doe_resource_buffer *buffer = (doe_resource_buffer *)handle;
    native_device *device = doe_resource_buffer_device(buffer);
    if (!device) return WGPUBufferMapState_Unmapped;
    native_lock(device);
    WGPUBufferMapState state = doe_resource_buffer_map_state(buffer);
    native_unlock(device);
    return state;
}

void wgpuBufferDestroy(WGPUBuffer handle)
{
    doe_resource_buffer *buffer = (doe_resource_buffer *)handle;
    native_device *device = doe_resource_buffer_device(buffer);
    if (!device) return;
    native_lock(device);
    native_buffer *payload = doe_resource_buffer_native(buffer);
    doe_resource_buffer_destroy(buffer);
    free(payload->mapping);
    payload->mapping = NULL;
    native_unlock(device);
}

void wgpuBufferUnmap(WGPUBuffer handle)
{
    doe_resource_buffer *buffer = (doe_resource_buffer *)handle;
    native_device *device = doe_resource_buffer_device(buffer);
    if (!device) return;
    native_lock(device);
    native_buffer *payload = doe_resource_buffer_native(buffer);
    native_failure failure = native_failure_status(NATIVE_OK);
    uint64_t offset = 0, size = 0;
    if (payload->mapping && doe_resource_buffer_map_mode(buffer) == WGPUMapMode_Write &&
        doe_resource_buffer_map_span(buffer, &offset, &size) == DOE_RESOURCE_OK)
        failure = native_buffer_write(payload, offset, payload->mapping, (size_t)size);
    doe_resource_status resource_status = doe_resource_buffer_unmap(buffer);
    free(payload->mapping);
    payload->mapping = NULL;
    native_unlock(device);
    if (failure.status != NATIVE_OK || resource_status != DOE_RESOURCE_OK) {
        WGPUErrorType type = failure.status != NATIVE_OK ? native_result_error(failure.status) :
            native_resource_error(resource_status);
        char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
        if (failure.status == NATIVE_OK) failure = native_failure_status(NATIVE_INVALID_STATE);
        native_failure_format(&failure, "buffer unmap or writeback failed", message);
        native_error(device, type, message);
    }
}

static void *mapped_range(WGPUBuffer handle, size_t offset, size_t size, bool writable)
{
    doe_resource_buffer *buffer = (doe_resource_buffer *)handle;
    native_device *device = doe_resource_buffer_device(buffer);
    if (!device) return NULL;
    native_lock(device);
    unsigned char *bytes = NULL;
    doe_resource_status status = DOE_RESOURCE_STATE;
    if (writable)
        status = doe_resource_buffer_mapped_range(buffer, offset,
            size == WGPU_WHOLE_MAP_SIZE ? UINT64_MAX : size, &bytes);
    else {
        const unsigned char *read_bytes = NULL;
        status = doe_resource_buffer_mapped_range_const(buffer, offset,
            size == WGPU_WHOLE_MAP_SIZE ? UINT64_MAX : size, &read_bytes);
        bytes = (unsigned char *)read_bytes;
    }
    native_unlock(device);
    /* Pinned native BufferMapping: getter failures return NULL with logging,
     * not a device error. Consumers may probe an unmapped buffer before upload. */
    if (status != DOE_RESOURCE_OK)
        fprintf(stderr, "doe: %s failed (resource status %u)\n",
            writable ? "wgpuBufferGetMappedRange" : "wgpuBufferGetConstMappedRange",
            (unsigned)status);
    return status == DOE_RESOURCE_OK ? bytes : NULL;
}
void *wgpuBufferGetMappedRange(WGPUBuffer buffer, size_t offset, size_t size)
{
    return mapped_range(buffer, offset, size, true);
}
const void *wgpuBufferGetConstMappedRange(WGPUBuffer buffer, size_t offset, size_t size)
{
    return mapped_range(buffer, offset, size, false);
}

WGPUStatus wgpuBufferReadMappedRange(WGPUBuffer handle, size_t offset, void *data, size_t size)
{
    doe_resource_buffer *buffer = (doe_resource_buffer *)handle;
    native_device *device = doe_resource_buffer_device(buffer);
    if (!device || (!data && size) || size == WGPU_WHOLE_MAP_SIZE) return WGPUStatus_Error;
    native_lock(device);
    const unsigned char *source = NULL;
    doe_resource_status status = doe_resource_buffer_mapped_range_const(buffer, offset, size, &source);
    if (status == DOE_RESOURCE_OK && size) memmove(data, source, size);
    native_unlock(device);
    return status == DOE_RESOURCE_OK ? WGPUStatus_Success : WGPUStatus_Error;
}
WGPUStatus wgpuBufferWriteMappedRange(WGPUBuffer handle, size_t offset, const void *data, size_t size)
{
    doe_resource_buffer *buffer = (doe_resource_buffer *)handle;
    native_device *device = doe_resource_buffer_device(buffer);
    if (!device || (!data && size) || size == WGPU_WHOLE_MAP_SIZE) return WGPUStatus_Error;
    native_lock(device);
    unsigned char *destination = NULL;
    doe_resource_status status = doe_resource_buffer_mapped_range(buffer, offset, size, &destination);
    if (status == DOE_RESOURCE_OK && size) memmove(destination, data, size);
    native_unlock(device);
    return status == DOE_RESOURCE_OK ? WGPUStatus_Success : WGPUStatus_Error;
}

typedef struct map_delivery {
    WGPUBufferMapCallbackInfo callback;
    doe_resource_buffer *buffer;
    unsigned char *bytes;
    uint64_t generation, offset;
    size_t size;
    bool began;
    WGPUMapAsyncStatus status;
    char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
} map_delivery;
static void copy_map(void *destination, const void *source)
{
    *(map_delivery *)destination = *(const map_delivery *)source;
}
static void deliver_map(void *context)
{
    map_delivery value = *(map_delivery *)context;
    native_device *device = doe_resource_buffer_device(value.buffer);
    native_lock(device);
    if (value.began) {
        bool success = value.status == WGPUMapAsyncStatus_Success;
        doe_resource_status status = doe_resource_buffer_map_complete(value.buffer,
            value.generation, success, value.bytes);
        if (status == DOE_RESOURCE_OK) {
            native_buffer *payload = doe_resource_buffer_native(value.buffer);
            payload->mapping = value.bytes;
            value.bytes = NULL;
        } else if (success) value.status = WGPUMapAsyncStatus_Aborted;
    }
    native_unlock(device);
    free(value.bytes);
    if (value.status != WGPUMapAsyncStatus_Success && !value.message[0]) {
        native_failure failure = native_failure_status(NATIVE_INVALID_STATE);
        native_failure_format(&failure, "buffer map failed or aborted", value.message);
    }
    if (value.callback.callback)
        value.callback.callback(value.status, native_string(value.message),
            value.callback.userdata1, value.callback.userdata2);
    doe_resource_buffer_release(value.buffer);
}

WGPUFuture wgpuBufferMapAsync(WGPUBuffer handle, WGPUMapMode mode,
    size_t offset, size_t size, WGPUBufferMapCallbackInfo callback)
{
    doe_resource_buffer *buffer = (doe_resource_buffer *)handle;
    native_device *device = doe_resource_buffer_device(buffer);
    if (!device || callback.nextInChain) return (WGPUFuture){0};
    doe_resource_buffer_retain(buffer);
    map_delivery delivery = {.callback = callback, .buffer = buffer, .offset = offset,
        .status = WGPUMapAsyncStatus_Error};
    native_failure failure = native_failure_status(NATIVE_INVALID_ARGUMENT);
    native_failure consumed_failure = native_failure_status(NATIVE_OK);
    native_lock(device);
    uint64_t requested = size == WGPU_WHOLE_MAP_SIZE ? UINT64_MAX : size;
    const native_result device_status = native_device_result(device);
    doe_resource_status status = device_status == NATIVE_OK ?
        doe_resource_buffer_map_begin(buffer, mode, offset, requested, &delivery.generation) :
        DOE_RESOURCE_UNAVAILABLE;
    const bool rejected = device_status == NATIVE_OK && status != DOE_RESOURCE_OK;
    if (device_status != NATIVE_OK) {
        delivery.status = WGPUMapAsyncStatus_Aborted;
        failure = native_failure_status(device_status);
    }
    if (status == DOE_RESOURCE_OK) {
        delivery.began = true;
        const doe_resource_buffer_desc *desc = doe_resource_buffer_description(buffer);
        delivery.size = size == WGPU_WHOLE_MAP_SIZE ? (size_t)(desc->size - offset) : size;
        delivery.bytes = malloc(delivery.size ? delivery.size : 1);
        native_buffer *payload = doe_resource_buffer_native(buffer);
        if (delivery.bytes) {
            failure = native_settle_for_buffer(device, payload);
            consumed_failure = failure;
            if (failure.status == NATIVE_OK)
                failure = native_buffer_read(payload, offset, delivery.bytes, delivery.size);
            if (failure.status != NATIVE_OK) consumed_failure = failure;
            if (failure.status == NATIVE_OK) delivery.status = WGPUMapAsyncStatus_Success;
            else if (native_device_result(device) != NATIVE_OK)
                delivery.status = WGPUMapAsyncStatus_Aborted;
        } else {
            failure = native_failure_status(NATIVE_OUT_OF_MEMORY);
            consumed_failure = failure;
        }
    }
    native_unlock(device);
    native_failure_format(&failure, "buffer map failed or aborted", delivery.message);
    /* Invalid arguments generate a device error independently of callback
     * delivery. Publish before admission: a spontaneous callback may pop its
     * error scope or release all public handles. This buffer lease protects the
     * device through reporting and then transfers to the callback owner. */
    if (rejected) native_error(device, native_resource_error(status), delivery.message);
    doe_callback_status admitted;
    WGPUFuture future = native_callback_admit(device->instance, callback.mode, &delivery,
        sizeof(delivery), _Alignof(map_delivery), copy_map, deliver_map, &admitted);
    if (!future.id) {
        native_lock(device);
        if (delivery.began)
            (void)doe_resource_buffer_map_complete(buffer, delivery.generation, false, NULL);
        native_unlock(device);
        free(delivery.bytes);
        /* Admission did not transfer a callback obligation. The buffer lease
         * still protects device storage while this consumed error is reported. */
        if (consumed_failure.status != NATIVE_OK)
            native_error(device, native_result_error(consumed_failure.status), delivery.message);
        else if (!rejected) native_callback_refused(device, admitted);
        doe_resource_buffer_release(buffer);
    }
    return future;
}

/* Queue-write staging uses the ordinary command and pending owners. Both
 * buffers are held by the recorded copy, including uncertain submissions. */
static native_failure staged_metal_write(native_buffer *destination, uint64_t offset,
    const void *bytes, size_t size)
{
    if (!size) return native_failure_status(NATIVE_OK);
    native_device *device = destination->device;
    doe_mtl_buffer *staging = NULL;
    native_command_handle commands = {0};
    doe_mtl_error error = {0};
    doe_mtl_status status = doe_mtl_buffer_create(device->backend.value.metal.context,
        size, DOE_MTL_SHARED, &staging, &error);
    if (status == DOE_MTL_OK) status = doe_mtl_buffer_write(staging, 0, bytes, size);
    native_failure failure = native_failure_metal(status, &error);
    if (failure.status == NATIVE_OK) failure = native_command_create(&device->backend, &commands);
    if (failure.status == NATIVE_OK) {
        status = doe_mtl_commands_copy(commands.value.metal, staging, 0,
            destination->handle.metal, offset, size, &error);
        failure = native_failure_metal(status, &error);
    }
    doe_mtl_buffer_release(staging);
    if (failure.status == NATIVE_OK) return native_submit_backend_commands(device, commands);
    if (commands.value.metal && native_command_release(&commands) != NATIVE_OK) abort();
    return failure;
}

static native_failure staged_queue_write(native_buffer *destination, uint64_t offset,
    const void *bytes, size_t size)
{
    if (!size) return native_failure_status(NATIVE_OK);
    native_device *device = destination->device;
    doe_vk_context *context = device->backend.value.vulkan.context;
    doe_vk_buffer *staging = NULL;
    doe_vk_commands *commands = NULL;
    doe_vk_error error = {0};
    doe_vk_status status = doe_vk_buffer_create(context, size, DOE_VK_BUFFER_COPY_SRC,
        DOE_VK_MEMORY_HOST, &staging, &error);
    if (status == DOE_VK_OK) status = doe_vk_buffer_write(staging, 0, bytes, size);
    if (status == DOE_VK_OK) status = doe_vk_commands_create(context, &commands, &error);
    if (status == DOE_VK_OK) status = doe_vk_commands_copy(commands, staging, 0,
        destination->handle.vulkan, offset, size);
    doe_vk_buffer_release(staging);
    if (status != DOE_VK_OK) {
        if (commands && doe_vk_commands_release(commands) != DOE_VK_OK) abort();
        return native_failure_vulkan(status, &error);
    }
    return native_submit_commands(device, commands); /* Always adopts, even on failure. */
}

void wgpuQueueWriteBuffer(WGPUQueue handle, WGPUBuffer buffer_handle,
    uint64_t offset, const void *bytes, size_t size)
{
    native_device *device = (native_device *)handle;
    doe_resource_buffer *buffer = (doe_resource_buffer *)buffer_handle;
    if (!device) return;
    native_device_retain(device);
    doe_resource_buffer_retain(buffer);
    native_lock(device);
    doe_resource_status status = doe_resource_buffer_write_validate(buffer, device, offset, size);
    native_failure failure = native_failure_status(NATIVE_INVALID_ARGUMENT);
    if (status == DOE_RESOURCE_OK && (bytes || !size)) {
        native_buffer *payload = doe_resource_buffer_native(buffer);
        /* D3D12 snapshots writes into retained upload staging, then copies on
         * its ordered queue. It never CPU-writes the destination allocation.
         * CPU read/map below still waits for that buffer's accepted owner. */
        failure = device->backend.kind == WGPUBackendType_D3D12 ?
            native_settle_routine(device).failure : native_settle_for_buffer(device, payload);
        /* A Metal resource wait drops the mutex. A concurrent map/destroy must
         * be admitted by the same resource owner before any CPU write. */
        if (failure.status == NATIVE_OK)
            status = doe_resource_buffer_write_validate(buffer, device, offset, size);
        if (failure.status == NATIVE_OK && status == DOE_RESOURCE_OK) {
            if (device->backend.upload_policy == DOE_NATIVE_UPLOAD_STAGED_COPY && device->backend.kind == WGPUBackendType_Vulkan)
                failure = staged_queue_write(payload, offset, bytes, size);
            else if (device->backend.upload_policy == DOE_NATIVE_UPLOAD_STAGED_COPY && device->backend.kind == WGPUBackendType_Metal)
                failure = staged_metal_write(payload, offset, bytes, size);
            else failure = native_buffer_write(payload, offset, bytes, size);
        }
    }
    native_unlock(device);
    if (status != DOE_RESOURCE_OK || failure.status != NATIVE_OK) {
        char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
        native_failure_format(&failure, "queue buffer write failed", message);
        native_error(device, status != DOE_RESOURCE_OK ? native_resource_error(status) : native_result_error(failure.status),
            message);
    }
    doe_resource_buffer_release(buffer);
    native_device_release(device);
}
