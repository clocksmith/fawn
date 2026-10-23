/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include <stdlib.h>

struct native_pending {
    native_pending *next;
    native_device *device;
    doe_vk_commands *native;
    doe_resource_commands **resources;
    size_t count;
};

static doe_vk_status record_dispatch(doe_vk_commands *commands, const doe_resource_command *record)
{
    native_pipeline *pipeline = doe_resource_pipeline_native(record->value.dispatch.pipeline);
    doe_resource_binding resolved[DOE_RESOURCE_MAX_GROUPS][DOE_RESOURCE_MAX_BINDINGS];
    size_t counts[DOE_RESOURCE_MAX_GROUPS] = {0};
    doe_resource_pipeline_layout *layout_owner = doe_resource_pipeline_get_layout(record->value.dispatch.pipeline);
    for (size_t i = 0; i < record->value.dispatch.group_count; ++i) {
        if (!doe_resource_pipeline_layout_group_required(layout_owner, (uint32_t)i)) continue;
        doe_resource_bind_group *group = record->value.dispatch.groups[i];
        (void)doe_resource_bind_group_bindings(group, &counts[i]);
        const doe_resource_dispatch_offsets *offsets = record->value.dispatch.dynamic_offsets;
        if (doe_resource_bind_group_resolve(group, offsets ? offsets->values[i] : NULL,
                offsets ? offsets->counts[i] : 0, resolved[i], DOE_RESOURCE_MAX_BINDINGS) != DOE_RESOURCE_OK)
            return DOE_VK_INVALID_ARGUMENT;
    }
    doe_vk_binding_resource bindings[DOE_RESOURCE_MAX_GROUPS * DOE_RESOURCE_MAX_BINDINGS];
    for (size_t i = 0; i < pipeline->binding_count; ++i) {
        doe_vk_binding_layout layout = pipeline->bindings[i];
        if (layout.group >= record->value.dispatch.group_count) return DOE_VK_INVALID_ARGUMENT;
        const doe_resource_binding *binding = NULL;
        for (size_t j = 0; j < counts[layout.group]; ++j)
            if (resolved[layout.group][j].binding == layout.binding) binding = &resolved[layout.group][j];
        if (!binding) return DOE_VK_INVALID_ARGUMENT;
        bindings[i].kind = layout.kind;
        if (layout.kind == DOE_VK_BINDING_UNIFORM || layout.kind == DOE_VK_BINDING_STORAGE) {
            if (binding->kind != DOE_BIND_BUFFER || binding->resource.buffer.size < pipeline->minimum_sizes[i])
                return DOE_VK_INVALID_ARGUMENT;
            native_buffer *buffer = doe_resource_buffer_native(binding->resource.buffer.buffer);
            bindings[i].value.buffer = (doe_vk_buffer_binding){buffer->buffer,
                binding->resource.buffer.offset, binding->resource.buffer.size};
        } else if (layout.kind == DOE_VK_BINDING_SAMPLER) {
            if (binding->kind != DOE_BIND_SAMPLER) return DOE_VK_INVALID_ARGUMENT;
            native_sampler *sampler = doe_resource_sampler_native(binding->resource.sampler);
            bindings[i].value.sampler = sampler->sampler;
        } else {
            doe_resource_binding_kind expected = layout.kind == DOE_VK_BINDING_SAMPLED_TEXTURE ?
                DOE_BIND_TEXTURE : DOE_BIND_STORAGE_TEXTURE;
            if (binding->kind != expected) return DOE_VK_INVALID_ARGUMENT;
            native_view *view = doe_resource_view_native(binding->resource.view);
            bindings[i].value.view = view->view;
        }
    }
    return doe_vk_commands_dispatch_resources(commands, pipeline->pipeline, bindings, pipeline->binding_count,
        record->value.dispatch.x, record->value.dispatch.y, record->value.dispatch.z);
}

static doe_vk_status record_commands(doe_vk_commands *native, doe_resource_commands *commands)
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
            status = doe_vk_commands_copy(native, source->buffer, record->value.copy.source_offset,
                destination->buffer, record->value.copy.destination_offset, record->value.copy.size);
            break;
        }
        case DOE_COMMAND_CLEAR_BUFFER: {
            native_buffer *buffer = doe_resource_buffer_native(record->value.clear.buffer);
            if (!record->value.clear.size) continue;
            status = doe_vk_commands_fill(native, buffer->buffer, record->value.clear.offset,
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
                doe_vk_commands_upload_texture(native, buffer->buffer, layout, texture->texture, region) :
                doe_vk_commands_read_texture(native, texture->texture, region, buffer->buffer, layout);
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

bool native_settle_pending(native_device *device)
{
    native_pending **cursor = &device->pending;
    while (*cursor) {
        native_pending *pending = *cursor;
        doe_vk_error error = {0};
        doe_vk_status status = doe_vk_commands_recover(pending->native, &error);
        doe_vk_status released = doe_vk_commands_release(pending->native);
        if (released == DOE_VK_BUSY) {
            device->queue_failed = true;
            return false;
        }
        if (released != DOE_VK_OK) abort();
        *cursor = pending->next;
        if (status != DOE_VK_OK) device->queue_failed = true;
        settle_resources(pending, status == DOE_VK_OK);
    }
    return true;
}

static doe_vk_status submit_pending(native_device *device, native_pending *pending)
{
    pending->next = device->pending;
    device->pending = pending;
    doe_vk_error error = {0};
    doe_vk_status status = doe_vk_commands_submit(pending->native, &error);
    if (status == DOE_VK_OK) status = doe_vk_commands_wait(pending->native, UINT64_MAX, &error);
    if (status != DOE_VK_OK) device->queue_failed = true;
    (void)native_settle_pending(device);
    return status;
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
    pending->native = commands;
    return submit_pending(device, pending);
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
    doe_vk_status status = native_device_alive(device) && native_settle_pending(device) ? DOE_VK_OK : DOE_VK_INVALID_STATE;
    for (size_t i = 0; status == DOE_VK_OK && i < count; ++i) {
        pending->resources[i] = (doe_resource_commands *)handles[i];
        if (doe_resource_commands_device(pending->resources[i]) != device) status = DOE_VK_INVALID_ARGUMENT;
        for (size_t j = 0; j < i; ++j)
            if (handles[i] == handles[j]) status = DOE_VK_INVALID_ARGUMENT;
    }
    doe_vk_error error = {0};
    doe_resource_status resource_status = DOE_RESOURCE_OK;
    if (status == DOE_VK_OK) status = doe_vk_commands_create(device->context, &pending->native, &error);
    for (size_t i = 0; status == DOE_VK_OK && i < count; ++i)
        status = record_commands(pending->native, pending->resources[i]);
    for (size_t i = 0; status == DOE_VK_OK && i < count; ++i) {
        resource_status = doe_resource_commands_begin_submit(pending->resources[i]);
        if (resource_status != DOE_RESOURCE_OK) status = DOE_VK_INVALID_ARGUMENT;
        else ++pending->count;
    }
    if (status == DOE_VK_OK) {
        /* recover preserves original submission failure; successful release is
         * the backend's proof of terminal or never-dispatched ownership. */
        status = submit_pending(device, pending);
    } else {
        if (doe_vk_commands_release(pending->native) != DOE_VK_OK) abort();
        settle_resources(pending, false);
    }
    native_unlock(device);
    if (status != DOE_VK_OK) native_error(device, resource_status != DOE_RESOURCE_OK ?
        native_resource_error(resource_status) : native_backend_error(status),
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
    bool complete = native_settle_pending(device);
    queue_delivery delivery = {callback, device->queue_failed ? WGPUQueueWorkDoneStatus_Error : WGPUQueueWorkDoneStatus_Success};
    native_unlock(device);
    WGPUFuture future = {0};
    if (complete) future = native_callback(device->instance, callback.mode, &delivery,
        sizeof(delivery), _Alignof(queue_delivery), copy_queue, deliver_queue);
    else native_error(device, WGPUErrorType_Internal, "queue completion remains unknown; no completion future published");
    native_device_release(device);
    return future;
}
