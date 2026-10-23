/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <stdlib.h>

struct native_pending {
    native_pending *next;
    native_device *device;
    native_command_handle native;
    doe_resource_commands **resources;
    size_t count;
};

static doe_vk_status record_dispatch(doe_vk_commands *commands, const doe_resource_command *record)
{
    native_pipeline *pipeline = doe_resource_pipeline_native(record->value.dispatch.pipeline);
    doe_vk_binding_resource bindings[DOE_RESOURCE_MAX_GROUPS * DOE_RESOURCE_MAX_BINDINGS];
    doe_vk_status status = native_resolve_bindings(pipeline,
        doe_resource_pipeline_get_layout(record->value.dispatch.pipeline), record->value.dispatch.groups,
        record->value.dispatch.dynamic_offsets, bindings);
    if (status != DOE_VK_OK) return status;
    return doe_vk_commands_dispatch_resources(commands, pipeline->handle.vulkan, bindings, pipeline->binding_count,
        record->value.dispatch.x, record->value.dispatch.y, record->value.dispatch.z);
}

static doe_vk_status record_vulkan_commands(doe_vk_commands *native, doe_resource_commands *commands)
{
    size_t count = 0;
    const doe_resource_command *records = doe_resource_commands_borrow(commands, &count);
    for (size_t i = 0; i < count; ++i) {
        const doe_resource_command *record = &records[i];
        doe_vk_status status;
        switch (record->kind) {
        case DOE_COMMAND_COPY_BUFFER: {
            native_buffer *source = doe_resource_buffer_native(record->value.copy.source);
            native_buffer *destination = doe_resource_buffer_native(record->value.copy.destination);
            if (!record->value.copy.size) continue;
            status = doe_vk_commands_copy(native, source->handle.vulkan, record->value.copy.source_offset,
                destination->handle.vulkan, record->value.copy.destination_offset, record->value.copy.size);
            break;
        }
        case DOE_COMMAND_CLEAR_BUFFER: {
            native_buffer *buffer = doe_resource_buffer_native(record->value.clear.buffer);
            if (!record->value.clear.size) continue;
            status = doe_vk_commands_fill(native, buffer->handle.vulkan, record->value.clear.offset,
                record->value.clear.size, 0);
            break;
        }
        case DOE_COMMAND_BUFFER_TEXTURE: {
            const doe_resource_texture_copy *copy = &record->value.texture.copy;
            if (!copy->width || !copy->height || !copy->depth_or_layers) continue;
            native_buffer *buffer = doe_resource_buffer_native(record->value.texture.buffer);
            native_texture *texture = doe_resource_texture_native(record->value.texture.texture);
            doe_vk_texture_buffer_layout layout = {copy->offset, record->value.texture.region.pitch,
                record->value.texture.region.image_rows};
            doe_vk_texture_region region = {.mip=copy->mip,
                .origin={copy->origin[0], copy->origin[1], copy->origin[2]},
                .extent={copy->width, copy->height, copy->depth_or_layers}};
            status = record->value.texture.direction == DOE_RESOURCE_BUFFER_TO_TEXTURE ?
                doe_vk_commands_upload_texture(native, buffer->handle.vulkan, layout, texture->texture, region) :
                doe_vk_commands_read_texture(native, texture->texture, region, buffer->handle.vulkan, layout);
            break;
        }
        case DOE_COMMAND_COPY_TEXTURE: {
            const doe_resource_texture_blit *copy = &record->value.blit;
            if (!copy->width || !copy->height || !copy->depth_or_layers) continue;
            native_texture *source = doe_resource_texture_native(copy->source.texture);
            native_texture *destination = doe_resource_texture_native(copy->destination.texture);
            doe_vk_texture_region from = {.mip=copy->source.mip,
                .origin={copy->source.origin[0],copy->source.origin[1],copy->source.origin[2]},
                .extent={copy->width,copy->height,copy->depth_or_layers}};
            doe_vk_texture_region to = {.mip=copy->destination.mip,
                .origin={copy->destination.origin[0],copy->destination.origin[1],copy->destination.origin[2]},
                .extent={copy->width,copy->height,copy->depth_or_layers}};
            status = doe_vk_commands_copy_texture(native, source->texture, from, destination->texture, to);
            break;
        }
        case DOE_COMMAND_RENDER: {
            size_t consumed = 0;
            status = native_record_render(native, records + i, count - i, &consumed);
            if (status == DOE_VK_OK) i += consumed - 1;
            break;
        }
        case DOE_COMMAND_WRITE_TIMESTAMP:
        case DOE_COMMAND_RESOLVE_QUERY:
            status = native_record_query(native, record);
            break;
        case DOE_COMMAND_DISPATCH:
            status = record_dispatch(native, record);
            break;
        default:
            return DOE_VK_UNSUPPORTED;
        }
        if (status != DOE_VK_OK) return status;
    }
    return DOE_VK_OK;
}

static native_result record_commands(native_command_handle *native, doe_resource_commands *commands)
{
    if (native->kind == WGPUBackendType_Vulkan)
        return native_from_vulkan(record_vulkan_commands(native->value.vulkan, commands));
    if (native->kind != WGPUBackendType_Metal) return NATIVE_UNSUPPORTED;
    size_t count = 0;
    const doe_resource_command *records = doe_resource_commands_borrow(commands, &count);
    for (size_t i = 0; i < count; ++i) {
        const doe_resource_command *record = &records[i];
        doe_mtl_status status;
        if (record->kind == DOE_COMMAND_COPY_BUFFER) {
            native_buffer *source = doe_resource_buffer_native(record->value.copy.source);
            native_buffer *destination = doe_resource_buffer_native(record->value.copy.destination);
            if (!record->value.copy.size) continue;
            status = doe_mtl_commands_copy(native->value.metal, source->handle.metal,
                record->value.copy.source_offset, destination->handle.metal,
                record->value.copy.destination_offset, record->value.copy.size, NULL);
        } else if (record->kind == DOE_COMMAND_CLEAR_BUFFER) {
            native_buffer *buffer = doe_resource_buffer_native(record->value.clear.buffer);
            if (!record->value.clear.size) continue;
            status = doe_mtl_commands_fill(native->value.metal, buffer->handle.metal,
                record->value.clear.offset, record->value.clear.size, 0, NULL);
        } else if (record->kind == DOE_COMMAND_DISPATCH) {
            native_result dispatched = native_metal_dispatch(native->value.metal,record);
            if (dispatched != NATIVE_OK) return dispatched;
            continue;
        } else return NATIVE_UNSUPPORTED;
        if (status != DOE_MTL_OK) return native_from_metal(status);
    }
    return NATIVE_OK;
}

static void settle_resources(native_pending *pending, bool success)
{
    for (size_t i = 0; i < pending->count; ++i) {
        doe_resource_status status = doe_resource_commands_settle(pending->resources[i],
            success ? DOE_RESOURCE_OK : DOE_RESOURCE_EXECUTION_FAILED);
        if (status != DOE_RESOURCE_OK && status != DOE_RESOURCE_EXECUTION_FAILED) abort();
    }
    free(pending->resources);
    native_device *device = pending->device;
    free(pending);
    native_device_release(device);
}

native_queue_settlement native_settle_pending(native_device *device)
{
    native_queue_settlement result = {true, NATIVE_OK};
    native_pending **cursor = &device->pending;
    while (*cursor) {
        native_pending *pending = *cursor;
        native_result status = native_command_recover(&pending->native);
        native_result released = native_command_release(&pending->native);
        if (result.status == NATIVE_OK && status != NATIVE_OK) result.status = status;
        if (released == NATIVE_BUSY) {
            device->queue_failed = true;
            result.complete = false;
            if (result.status == NATIVE_OK) result.status = NATIVE_PENDING;
            return result;
        }
        if (released != NATIVE_OK) abort();
        *cursor = pending->next;
        if (status != NATIVE_OK) {
            device->queue_failed = true;
            /* Proven terminal failure may poison the shared normalization scratch.
             * A later timestamp resolve reconstructs it, never reuses failed data. */
            doe_vk_timestamp_normalizer_release(device->timestamp_normalizer);
            device->timestamp_normalizer = NULL;
        }
        settle_resources(pending, status == NATIVE_OK);
    }
    return result;
}

static native_result submit_pending(native_device *device, native_pending *pending)
{
    pending->next = device->pending;
    device->pending = pending;
    native_result status = native_command_submit(&pending->native);
    if (status == NATIVE_OK) status = native_command_wait(&pending->native);
    if (status != NATIVE_OK) device->queue_failed = true;
    native_queue_settlement settled = native_settle_pending(device);
    return status == NATIVE_OK ? settled.status : status;
}

doe_vk_status native_submit_commands(native_device *device, doe_vk_commands *commands)
{
    native_pending *pending = calloc(1, sizeof(*pending));
    if (!pending) {
        if (doe_vk_commands_release(commands) != DOE_VK_OK) abort();
        return DOE_VK_OUT_OF_MEMORY;
    }
    pending->device = device;
    native_device_retain(device);
    pending->native = (native_command_handle){.kind=WGPUBackendType_Vulkan,.value.vulkan=commands};
    return native_to_vulkan(submit_pending(device, pending));
}

void wgpuQueueSubmit(WGPUQueue handle, size_t count, const WGPUCommandBuffer *handles)
{
    native_device *device = (native_device *)handle;
    if (!device) return;
    if (!count) return;
    if (!handles || count > SIZE_MAX / sizeof(doe_resource_commands *)) {
        native_error(device, WGPUErrorType_Validation, "invalid queue submit array");
        return;
    }
    native_pending *pending = calloc(1, sizeof(*pending));
    if (pending) pending->resources = calloc(count, sizeof(*pending->resources));
    if (!pending || !pending->resources) {
        free(pending);
        native_error(device, WGPUErrorType_OutOfMemory, "submission ownership allocation failed");
        return;
    }
    pending->device = device;
    native_device_retain(device);
    native_lock(device);
    native_result status = native_device_result(device);
    if (status == NATIVE_OK) status = native_settle_pending(device).status;
    for (size_t i = 0; status == NATIVE_OK && i < count; ++i) {
        pending->resources[i] = (doe_resource_commands *)handles[i];
        if (doe_resource_commands_device(pending->resources[i]) != device) status = NATIVE_INVALID_ARGUMENT;
        for (size_t j = 0; j < i; ++j)
            if (handles[i] == handles[j]) status = NATIVE_INVALID_ARGUMENT;
    }
    doe_resource_status resource_status = DOE_RESOURCE_OK;
    if (status == NATIVE_OK) status = native_command_create(&device->backend, &pending->native);
    for (size_t i = 0; status == NATIVE_OK && i < count; ++i)
        status = record_commands(&pending->native, pending->resources[i]);
    for (size_t i = 0; status == NATIVE_OK && i < count; ++i) {
        resource_status = doe_resource_commands_begin_submit(pending->resources[i]);
        if (resource_status != DOE_RESOURCE_OK) status = NATIVE_INVALID_ARGUMENT;
        else ++pending->count;
    }
    if (status == NATIVE_OK) {
        /* recover preserves original submission failure; successful release is
         * the backend's proof of terminal or never-dispatched ownership. */
        status = submit_pending(device, pending);
    } else {
        if (native_command_release(&pending->native) != NATIVE_OK) abort();
        settle_resources(pending, false);
    }
    native_unlock(device);
    if (status != NATIVE_OK) native_error(device, resource_status != DOE_RESOURCE_OK ?
        native_resource_error(resource_status) : native_result_error(status),
        "queue submission failed; unresolved work stays retained");
}

typedef struct queue_delivery {
    WGPUQueueWorkDoneCallbackInfo callback;
    WGPUQueueWorkDoneStatus status;
} queue_delivery;
static void copy_queue(void *destination, const void *source)
{
    *(queue_delivery *)destination = *(const queue_delivery *)source;
}
static void deliver_queue(void *context)
{
    queue_delivery value = *(queue_delivery *)context;
    if (value.callback.callback)
        value.callback.callback(value.status, native_string(value.status == WGPUQueueWorkDoneStatus_Success ? "" : "queue work failed"),
            value.callback.userdata1, value.callback.userdata2);
}
WGPUFuture wgpuQueueOnSubmittedWorkDone(WGPUQueue handle, WGPUQueueWorkDoneCallbackInfo callback)
{
    native_device *device = (native_device *)handle;
    if (!device || callback.nextInChain) return (WGPUFuture){0};
    native_device_retain(device);
    native_lock(device);
    native_queue_settlement settled = native_settle_pending(device);
    queue_delivery delivery = {callback, device->queue_failed ? WGPUQueueWorkDoneStatus_Error : WGPUQueueWorkDoneStatus_Success};
    native_unlock(device);
    WGPUFuture future = {0};
    if (settled.complete) future = native_callback(device->instance, callback.mode, &delivery,
        sizeof(delivery), _Alignof(queue_delivery), copy_queue, deliver_queue);
    else native_error(device, native_result_error(settled.status), "queue completion remains unknown; no completion future published");
    native_device_release(device);
    return future;
}
