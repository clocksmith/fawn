/* SPDX-License-Identifier: MIT */
#include "internal.h"
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
    if (!descriptor || descriptor->nextInChain || descriptor->size > SIZE_MAX ||
        descriptor->size > device->backend.limits.maxBufferSize) {
        native_error(device, WGPUErrorType_Validation, "buffer descriptor unsupported or exceeds limit");
        return NULL;
    }
    doe_resource_buffer_desc desc = {descriptor->size, descriptor->usage, descriptor->mappedAtCreation != 0};
    if (doe_resource_buffer_validate(&desc) != DOE_RESOURCE_OK) {
        native_error(device, WGPUErrorType_Validation, "buffer descriptor validation failed");
        return NULL;
    }
    native_buffer *payload = calloc(1, sizeof(*payload));
    if (!payload) { native_error(device, WGPUErrorType_OutOfMemory, "buffer allocation failed"); return NULL; }
    payload->device = device;
    uint32_t usage = 0;
    if (descriptor->usage & (WGPUBufferUsage_CopySrc | WGPUBufferUsage_MapWrite)) usage |= DOE_VK_BUFFER_COPY_SRC;
    if (descriptor->usage & (WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead | WGPUBufferUsage_QueryResolve)) usage |= DOE_VK_BUFFER_COPY_DST;
    if (descriptor->usage & WGPUBufferUsage_Uniform) usage |= DOE_VK_BUFFER_UNIFORM;
    if (descriptor->usage & WGPUBufferUsage_Storage) usage |= DOE_VK_BUFFER_STORAGE;
    if (descriptor->usage & WGPUBufferUsage_Vertex) usage |= DOE_VK_BUFFER_VERTEX;
    if (descriptor->usage & WGPUBufferUsage_Indirect) usage |= DOE_VK_BUFFER_INDIRECT;
    const WGPUBufferUsage supported = WGPUBufferUsage_MapRead | WGPUBufferUsage_MapWrite |
        WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform |
        WGPUBufferUsage_Storage | WGPUBufferUsage_Indirect | WGPUBufferUsage_QueryResolve |
        WGPUBufferUsage_Vertex;
    doe_resource_buffer *buffer = NULL;
    native_lock(device);
    native_result status = descriptor->usage & ~supported ? NATIVE_UNSUPPORTED : native_device_result(device);
    if (status == NATIVE_OK)
        status = native_buffer_create(payload, descriptor->size ? descriptor->size : 4, usage);
    doe_resource_status created = DOE_RESOURCE_OK;
    if (status == NATIVE_OK) {
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
    if (!buffer) {
        if (payload) native_buffer_release(NULL, payload);
        native_error(device, status != NATIVE_OK ? native_result_error(status) : native_resource_error(created),
            "buffer creation failed or unsupported usage");
    }
    if (buffer) native_label(device, buffer, descriptor->label);
    return (WGPUBuffer)buffer;
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
    native_result status = NATIVE_OK;
    uint64_t offset = 0, size = 0;
    if (payload->mapping && doe_resource_buffer_map_mode(buffer) == WGPUMapMode_Write &&
        doe_resource_buffer_map_span(buffer, &offset, &size) == DOE_RESOURCE_OK)
        status = native_buffer_write(payload, offset, payload->mapping, (size_t)size);
    doe_resource_status resource_status = doe_resource_buffer_unmap(buffer);
    free(payload->mapping);
    payload->mapping = NULL;
    native_unlock(device);
    if (status != NATIVE_OK || resource_status != DOE_RESOURCE_OK)
        native_error(device, status != NATIVE_OK ? native_result_error(status) : native_resource_error(resource_status),
            "buffer unmap or writeback failed");
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
    if (status != DOE_RESOURCE_OK) native_error(device, native_resource_error(status), "invalid mapped range");
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
    if (value.callback.callback)
        value.callback.callback(value.status, native_string(value.status == WGPUMapAsyncStatus_Success ? "" : "buffer map failed or aborted"),
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
    native_lock(device);
    uint64_t requested = size == WGPU_WHOLE_MAP_SIZE ? UINT64_MAX : size;
    doe_resource_status status = doe_resource_buffer_map_begin(buffer, mode, offset, requested, &delivery.generation);
    if (status == DOE_RESOURCE_OK) {
        delivery.began = true;
        const doe_resource_buffer_desc *desc = doe_resource_buffer_description(buffer);
        delivery.size = size == WGPU_WHOLE_MAP_SIZE ? (size_t)(desc->size - offset) : size;
        delivery.bytes = malloc(delivery.size ? delivery.size : 1);
        native_buffer *payload = doe_resource_buffer_native(buffer);
        if (delivery.bytes && native_settle_pending(device).status == NATIVE_OK &&
            native_buffer_read(payload, offset, delivery.bytes, delivery.size) == NATIVE_OK)
            delivery.status = WGPUMapAsyncStatus_Success;
    }
    native_unlock(device);
    WGPUFuture future = native_callback(device->instance, callback.mode, &delivery,
        sizeof(delivery), _Alignof(map_delivery), copy_map, deliver_map);
    if (!future.id) {
        native_lock(device);
        if (delivery.began)
            (void)doe_resource_buffer_map_complete(buffer, delivery.generation, false, NULL);
        native_unlock(device);
        free(delivery.bytes);
        doe_resource_buffer_release(buffer);
    }
    return future;
}

void wgpuQueueWriteBuffer(WGPUQueue handle, WGPUBuffer buffer_handle,
    uint64_t offset, const void *bytes, size_t size)
{
    native_device *device = (native_device *)handle;
    doe_resource_buffer *buffer = (doe_resource_buffer *)buffer_handle;
    if (!device) return;
    native_lock(device);
    doe_resource_status status = doe_resource_buffer_write_validate(buffer, device, offset, size);
    native_result native_status = NATIVE_INVALID_ARGUMENT;
    if (status == DOE_RESOURCE_OK && (bytes || !size)) {
        native_status = native_settle_pending(device).status;
        if (native_status == NATIVE_OK) {
            native_buffer *payload = doe_resource_buffer_native(buffer);
            native_status = native_buffer_write(payload, offset, bytes, size);
        }
    }
    native_unlock(device);
    if (status != DOE_RESOURCE_OK || native_status != NATIVE_OK)
        native_error(device, status != DOE_RESOURCE_OK ? native_resource_error(status) : native_result_error(native_status),
            "queue buffer write failed");
}
