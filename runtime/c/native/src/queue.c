/* SPDX-License-Identifier: MIT */
#include "surface.h"
#include <stdlib.h>

static doe_vk_status record_dispatch(doe_vk_commands *commands, const doe_resource_command *record)
{
    native_pipeline *pipeline = doe_resource_pipeline_native(record->value.dispatch.pipeline);
    doe_vk_binding_resource bindings[DOE_RESOURCE_MAX_GROUPS * DOE_RESOURCE_MAX_BINDINGS];
    doe_vk_status status = native_resolve_bindings(pipeline,
        doe_resource_pipeline_get_layout(record->value.dispatch.pipeline), record->value.dispatch.groups,
        record->value.dispatch.dynamic_offsets, bindings);
    if (status != DOE_VK_OK) return status;
    if (record->kind == DOE_COMMAND_DISPATCH_INDIRECT) {
        status = native_dispatch_guard_initialize(pipeline->device);
        if (status != DOE_VK_OK) return status;
        native_buffer *arguments = doe_resource_buffer_native(record->value.dispatch.indirect);
        return doe_vk_commands_dispatch_indirect(commands,pipeline->device->dispatch_validator,
            pipeline->handle.vulkan,bindings,pipeline->binding_count,arguments->handle.vulkan,
            record->value.dispatch.indirect_offset);
    }
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
            /* The resource region carries the admitted single aspect. */
            if (!native_texture_aspect(record->value.texture.region.aspect, &region.aspect)) {
                status = DOE_VK_INVALID_ARGUMENT;
                break;
            }
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
            /* Admitted texture copies use every aspect of both endpoints. */
            if (!native_texture_aspect(copy->source.aspect, &from.aspect) ||
                !native_texture_aspect(copy->destination.aspect, &to.aspect)) {
                status = DOE_VK_INVALID_ARGUMENT;
                break;
            }
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
        case DOE_COMMAND_DISPATCH_INDIRECT:
            status = record_dispatch(native, record);
            break;
        default:
            return DOE_VK_UNSUPPORTED;
        }
        if (status != DOE_VK_OK) return status;
    }
    return DOE_VK_OK;
}

native_failure native_record_resource_commands(native_command_handle *native, doe_resource_commands *commands)
{
    if (native->kind == WGPUBackendType_Vulkan)
        return native_failure_status(native_from_vulkan(record_vulkan_commands(native->value.vulkan, commands)));
    if (native->kind != WGPUBackendType_Metal) return native_failure_status(NATIVE_UNSUPPORTED);
    size_t count = 0;
    const doe_resource_command *records = doe_resource_commands_borrow(commands, &count);
    for (size_t i = 0; i < count; ++i) {
        const doe_resource_command *record = &records[i];
        doe_mtl_status status;
        doe_mtl_error error = {0};
        if (record->kind == DOE_COMMAND_COPY_BUFFER) {
            native_buffer *source = doe_resource_buffer_native(record->value.copy.source);
            native_buffer *destination = doe_resource_buffer_native(record->value.copy.destination);
            if (!record->value.copy.size) continue;
            status = doe_mtl_commands_copy(native->value.metal, source->handle.metal,
                record->value.copy.source_offset, destination->handle.metal,
                record->value.copy.destination_offset, record->value.copy.size, &error);
        } else if (record->kind == DOE_COMMAND_CLEAR_BUFFER) {
            native_buffer *buffer = doe_resource_buffer_native(record->value.clear.buffer);
            if (!record->value.clear.size) continue;
            status = doe_mtl_commands_fill(native->value.metal, buffer->handle.metal,
                record->value.clear.offset, record->value.clear.size, 0, &error);
        } else if (record->kind == DOE_COMMAND_DISPATCH) {
            native_failure dispatched = native_metal_dispatch(native->value.metal,record);
            if (dispatched.status != NATIVE_OK) return dispatched;
            continue;
        } else return native_failure_status(NATIVE_UNSUPPORTED);
        if (status != DOE_MTL_OK) return native_failure_metal(status, &error);
    }
    return native_failure_status(NATIVE_OK);
}

static void settle_resources(native_pending *pending, bool success)
{
    for (size_t i = 0; i < pending->count; ++i) {
        doe_resource_status terminal = success ? DOE_RESOURCE_OK : DOE_RESOURCE_EXECUTION_FAILED;
        doe_resource_status status = pending->replay ?
            doe_resource_commands_settle_replay(pending->resources[i], terminal) :
            doe_resource_commands_settle(pending->resources[i], terminal);
        if (status != DOE_RESOURCE_OK && status != DOE_RESOURCE_EXECUTION_FAILED) abort();
    }
    free(pending->resources);
    native_device *device = pending->device;
    free(pending);
    /* The foreground call or progress worker still owns a device lease while
     * this recursive drop returns to its enclosing device-mutex scope. */
    native_device_release(device);
}

static native_queue_settlement settle_pending(native_device *device, bool background)
{
    native_queue_settlement result = {.complete = true};
    native_pending **cursor = &device->pending;
    while (*cursor) {
        native_pending *pending = *cursor;
        if (pending->kind == NATIVE_PENDING_SURFACE) {
            /* This also covers an acquisition/discard with no command object.
             * Zero timeout polls existing native fences; it never starts a frame. */
            if (!pending->ready)
                pending->ready = doe_vk_surface_retire(pending->frame, 0, NULL) == DOE_VK_OK;
            /* Cleanup-only WSI/frame leases do not describe unfinished queue
             * commands. Keep polling them, without delaying WorkDone or creating
             * a command error. Actual command records below retain their proof. */
            cursor = &pending->next;
            continue;
        }
        native_failure failure = background ? native_command_poll(&pending->native) :
            native_command_recover(&pending->native);
        /* A terminal render fence does not retire presentation. Poll the same
         * backend frame before release, including discard after enqueue failure. */
        if (pending->frame) (void)doe_vk_surface_retire_frame(pending->frame, 0, NULL);
        native_result released = native_command_release(&pending->native);
        if (result.failure.status == NATIVE_OK && failure.status != NATIVE_OK) result.failure = failure;
        if (released == NATIVE_BUSY) {
            device->queue_failed = true;
            result.complete = false;
            if (result.failure.status == NATIVE_OK) result.failure = native_failure_status(NATIVE_PENDING);
            cursor = &pending->next;
            continue;
        }
        if (released != NATIVE_OK) abort();
        *cursor = pending->next;
        if (failure.status != NATIVE_OK) {
            if (device->retired_failure.status == NATIVE_OK) device->retired_failure = failure;
            device->queue_failed = true;
            /* Proven terminal failure may poison private helper scratch. Later
             * operations reconstruct helpers, never reuse failed storage. */
            doe_vk_timestamp_normalizer_release(device->timestamp_normalizer);
            device->timestamp_normalizer = NULL;
            native_indirect_guards_release(device);
        }
        settle_resources(pending, failure.status == NATIVE_OK);
    }
    return result;
}

native_queue_settlement native_settle_pending(native_device *device)
{
    native_queue_settlement result = settle_pending(device, false);
    if (device->retired_failure.status != NATIVE_OK) {
        result.failure = device->retired_failure;
        device->retired_failure = native_failure_status(NATIVE_OK);
    }
    return result;
}

void native_progress_pending(native_device *device)
{
    /* Preserve the first terminal failure until a public operation observes it.
     * Background retirement cannot silently turn its next write into success. */
    (void)settle_pending(device, true);
}

void native_surface_retirement_arm(native_surface *surface)
{
    native_pending *pending = surface->retirement;
    if (pending->armed) return;
    pending->armed = true;
    native_retain_count(&surface->leases);
    native_device_retain(pending->device);
    pending->next = pending->device->pending;
    pending->device->pending = pending;
    native_pending_progress_wake(pending->device);
}

native_surface *native_surface_retirement_peek(native_device *device)
{
    for (native_pending *pending = device->pending; pending; pending = pending->next) {
        if (pending->kind == NATIVE_PENDING_SURFACE && pending->ready) {
            native_retain_count(&pending->surface->leases);
            return pending->surface;
        }
    }
    return NULL;
}

native_pending *native_surface_retirement_take(native_device *device, native_pending *only)
{
    native_pending **cursor = &device->pending;
    while (*cursor) {
        native_pending *pending = *cursor;
        if (pending->kind == NATIVE_PENDING_SURFACE && pending->ready && (!only || only == pending)) {
            *cursor = pending->next;
            pending->next = NULL;
            return pending;
        }
        cursor = &pending->next;
    }
    return NULL;
}

static native_failure submit_pending(native_device *device, native_pending *pending)
{
    pending->next = device->pending;
    device->pending = pending;
    native_pending_progress_wake(device);
    native_failure failure = native_command_submit(&pending->native);
    if (failure.status == NATIVE_OK) failure = native_command_wait(&pending->native);
    if (failure.status != NATIVE_OK) device->queue_failed = true;
    native_queue_settlement settled = native_settle_pending(device);
    return failure.status == NATIVE_OK ? settled.failure : failure;
}

native_failure native_submit_program(native_device *device, native_command_handle native,
    doe_resource_commands *commands)
{
    native_pending *pending = calloc(1, sizeof(*pending));
    doe_resource_commands **resources = pending ? calloc(1, sizeof(*resources)) : NULL;
    if (!resources) {
        free(pending);
        if (native_command_release(&native) != NATIVE_OK) abort();
        if (doe_resource_commands_settle_replay(commands, DOE_RESOURCE_EXECUTION_FAILED) !=
                DOE_RESOURCE_EXECUTION_FAILED) abort();
        return native_failure_status(NATIVE_OUT_OF_MEMORY);
    }
    resources[0] = commands;
    *pending = (native_pending){.device=device,.native=native,.resources=resources,.count=1,.replay=true};
    native_device_retain(device);
    return submit_pending(device, pending);
}

native_failure native_submit_commands(native_device *device, doe_vk_commands *commands)
{
    native_pending *pending = calloc(1, sizeof(*pending));
    if (!pending) {
        if (doe_vk_commands_release(commands) != DOE_VK_OK) abort();
        return native_failure_status(NATIVE_OUT_OF_MEMORY);
    }
    pending->device = device;
    native_device_retain(device);
    pending->native = (native_command_handle){.kind=WGPUBackendType_Vulkan,.value.vulkan=commands};
    return submit_pending(device, pending);
}

native_presentation native_submit_present(native_device *device, doe_vk_commands *commands,
    doe_vk_surface *surface)
{
    native_presentation result = {.presentation = {.status = NATIVE_INVALID_STATE}};
    native_pending *pending = calloc(1, sizeof(*pending));
    if (!pending) {
        /* Unsubmitted commands detach their present lease on release. */
        if (doe_vk_commands_release(commands) != DOE_VK_OK) abort();
        result.submission = native_failure_status(NATIVE_OUT_OF_MEMORY);
        return result;
    }
    pending->device = device;
    native_device_retain(device);
    pending->native = (native_command_handle){.kind=WGPUBackendType_Vulkan,.value.vulkan=commands};
    pending->frame = surface;
    pending->next = device->pending;
    device->pending = pending;
    native_pending_progress_wake(device);
    native_failure failure = native_command_submit(&pending->native);
    if (failure.status == NATIVE_OK) {
        bool suboptimal = false;
        doe_vk_error detail = {0};
        /* Waits the submit's render-finished semaphore on the same queue. */
        doe_vk_status status = doe_vk_surface_present(surface, commands, &suboptimal, &detail);
        result.presentation = native_failure_vulkan(status, &detail);
        failure = native_command_wait(&pending->native);
    }
    if (failure.status != NATIVE_OK) device->queue_failed = true;
    /* The presentation fence (or maintenance1 discard when present was never
     * enqueued) retires the frame; command completion never does. Settlement
     * must follow it: commands stay BUSY while their frame is unretired. */
    doe_vk_error detail = {0};
    doe_vk_status retired = doe_vk_surface_retire_frame(surface, NATIVE_SURFACE_TIMEOUT_NS, &detail);
    if (result.presentation.status == NATIVE_OK) result.presentation = native_failure_vulkan(retired, &detail);
    native_queue_settlement settled = native_settle_pending(device);
    result.submission = failure.status == NATIVE_OK ? settled.failure : failure;
    return result;
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
    native_failure failure = native_failure_status(native_device_result(device));
    if (failure.status == NATIVE_OK) failure = native_settle_pending(device).failure;
    for (size_t i = 0; failure.status == NATIVE_OK && i < count; ++i) {
        pending->resources[i] = (doe_resource_commands *)handles[i];
        if (doe_resource_commands_device(pending->resources[i]) != device)
            failure = native_failure_status(NATIVE_INVALID_ARGUMENT);
        for (size_t j = 0; j < i; ++j)
            if (handles[i] == handles[j]) failure = native_failure_status(NATIVE_INVALID_ARGUMENT);
    }
    doe_resource_status resource_status = DOE_RESOURCE_OK;
    if (failure.status == NATIVE_OK) failure = native_command_create(&device->backend, &pending->native);
    for (size_t i = 0; failure.status == NATIVE_OK && i < count; ++i)
        failure = native_record_resource_commands(&pending->native, pending->resources[i]);
    for (size_t i = 0; failure.status == NATIVE_OK && i < count; ++i) {
        resource_status = doe_resource_commands_begin_submit(pending->resources[i]);
        if (resource_status != DOE_RESOURCE_OK) failure = native_failure_status(NATIVE_INVALID_ARGUMENT);
        else ++pending->count;
    }
    if (failure.status == NATIVE_OK) {
        /* Recovery preserves the original submission failure; successful release
         * is the backend's proof of terminal or never-dispatched ownership. */
        failure = submit_pending(device, pending);
    } else {
        if (native_command_release(&pending->native) != NATIVE_OK) abort();
        settle_resources(pending, false);
    }
    native_unlock(device);
    if (failure.status != NATIVE_OK) {
        char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
        native_failure_format(&failure, "queue submission failed; unresolved work stays retained", message);
        native_error(device, resource_status != DOE_RESOURCE_OK ?
            native_resource_error(resource_status) : native_result_error(failure.status), message);
    }
}

typedef struct queue_delivery {
    WGPUQueueWorkDoneCallbackInfo callback;
    WGPUQueueWorkDoneStatus status;
    char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
} queue_delivery;
static void copy_queue(void *destination, const void *source)
{
    *(queue_delivery *)destination = *(const queue_delivery *)source;
}
static void deliver_queue(void *context)
{
    queue_delivery value = *(queue_delivery *)context;
    if (value.callback.callback)
        value.callback.callback(value.status, native_string(value.message),
            value.callback.userdata1, value.callback.userdata2);
}
WGPUFuture wgpuQueueOnSubmittedWorkDone(WGPUQueue handle, WGPUQueueWorkDoneCallbackInfo callback)
{
    native_device *device = (native_device *)handle;
    if (!device || callback.nextInChain) return (WGPUFuture){0};
    native_device_retain(device);
    native_lock(device);
    native_queue_settlement settled = native_settle_pending(device);
    queue_delivery delivery = {.callback = callback,
        .status = device->queue_failed ? WGPUQueueWorkDoneStatus_Error : WGPUQueueWorkDoneStatus_Success};
    native_unlock(device);
    WGPUFuture future = {0};
    native_failure failure = settled.failure;
    if (delivery.status == WGPUQueueWorkDoneStatus_Error && failure.status == NATIVE_OK)
        failure = native_failure_status(NATIVE_FAILURE);
    native_failure_format(&failure, "queue work failed", delivery.message);
    if (settled.complete) {
        future = native_callback(device->instance, callback.mode, &delivery,
            sizeof(delivery), _Alignof(queue_delivery), copy_queue, deliver_queue);
        /* No callback was admitted. Preserve the consumed receipt through the
         * existing error channel while this call still owns its device lease. */
        if (!future.id && settled.failure.status != NATIVE_OK)
            native_error(device, native_result_error(settled.failure.status), delivery.message);
    } else {
        char message[NATIVE_FAILURE_MESSAGE_CAPACITY];
        native_failure_format(&settled.failure, "queue completion remains unknown; no completion future published", message);
        native_error(device, native_result_error(settled.failure.status), message);
    }
    native_device_release(device);
    return future;
}
